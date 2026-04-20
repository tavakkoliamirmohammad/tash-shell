// ClusterEngine — orchestration for `cluster <cmd>` verbs. See
// include/tash/cluster/cluster_engine.h for the contract.
//
// Keep I/O out of this file: all spawning, sleeping, prompting, and
// persistence goes through injected seams (ISshClient, ISlurmOps,
// ITmuxOps, INotifier, IPrompt, IClock) or the Registry.

#include "tash/cluster/cluster_engine.h"
#include "tash/cluster/presets.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace tash::cluster {

// ══════════════════════════════════════════════════════════════════════════════
// RealClock
// ══════════════════════════════════════════════════════════════════════════════

std::chrono::steady_clock::time_point RealClock::now() {
    return std::chrono::steady_clock::now();
}
void RealClock::sleep_for(std::chrono::milliseconds d) {
    std::this_thread::sleep_for(d);
}

// ══════════════════════════════════════════════════════════════════════════════
// Helpers (anonymous)
// ══════════════════════════════════════════════════════════════════════════════

namespace {

// UTC-ISO-8601 of std::chrono::system_clock::now(); informational, not used
// for any logic (the engine's timeout decisions go through IClock).
std::string iso8601_utc_now() {
    const auto tp = std::chrono::system_clock::now();
    const auto tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

// Build a SubmitSpec from the resource + route + per-call overrides.
SubmitSpec build_submit_spec(const Resource& res, const Route& route, const UpSpec& spec) {
    SubmitSpec s;
    s.cluster   = route.cluster;
    s.account   = route.account;
    s.partition = route.partition;
    s.qos       = route.qos;
    s.gres      = route.gres;
    s.time      = spec.time.empty()  ? res.default_time : spec.time;
    s.cpus      = spec.cpus.has_value() ? *spec.cpus   : res.default_cpus;
    s.mem       = spec.mem.empty()   ? res.default_mem : spec.mem;
    s.job_name  = spec.name.value_or("tash-" + res.name);
    // Placeholder work for the allocation — real instances run in tmux
    // sessions independently. `sleep infinity` keeps the allocation alive
    // until scancel or time limit.
    s.wrap = "sleep infinity";
    return s;
}

// A route qualifies if sinfo reports at least one idle node whose
// idle_gres list contains the route's gres. CPU-only routes (gres empty)
// qualify whenever any idle node exists.
bool route_has_idle(ISlurmOps& slurm, ISshClient& ssh, const Route& r) {
    const auto states = slurm.sinfo(r.cluster, r.partition, ssh);
    for (const auto& ps : states) {
        if (ps.idle_nodes <= 0) continue;
        if (r.gres.empty()) return true;
        for (const auto& g : ps.idle_gres) {
            if (g == r.gres) return true;
        }
    }
    return false;
}

}  // namespace

// ══════════════════════════════════════════════════════════════════════════════
// ClusterEngine
// ══════════════════════════════════════════════════════════════════════════════

ClusterEngine::ClusterEngine(const Config& cfg,
                               Registry&    reg,
                               ISshClient&  ssh,
                               ISlurmOps&   slurm,
                               ITmuxOps&    tmux,
                               INotifier&   notify,
                               IPrompt&     prompt,
                               IClock&      clock)
    : cfg_(cfg), reg_(reg), ssh_(ssh), slurm_(slurm), tmux_(tmux),
      notify_(notify), prompt_(prompt), clock_(clock) {}

// ══════════════════════════════════════════════════════════════════════════════
// list / down / kill / sync / probe / import
// ══════════════════════════════════════════════════════════════════════════════

ClusterResult<std::vector<Allocation>> ClusterEngine::list(const ListSpec& spec) {
    auto lk = reg_.lock();
    std::vector<Allocation> out;
    for (const auto& a : reg_.allocations) {
        if (spec.cluster && a.cluster != *spec.cluster) continue;
        out.push_back(a);
    }
    return out;
}

ClusterResult<Allocation> ClusterEngine::down(const DownSpec& spec) {
    if (spec.alloc_id.empty()) {
        return EngineError{"--alloc-id is required for `cluster down`"};
    }
    auto lk = reg_.lock();
    auto* a = reg_.find_allocation(spec.alloc_id);
    if (!a) {
        return EngineError{"no allocation with id: " + spec.alloc_id};
    }

    Allocation snapshot = *a;      // copy before removal
    if (a->state != AllocationState::Ended) {
        if (!slurm_.scancel(a->cluster, a->jobid, ssh_)) {
            // Don't desync: the job may still be alive on the cluster.
            // Leave the allocation in place so the user can retry / inspect.
            return EngineError{"scancel refused job " + a->jobid +
                                " on " + a->cluster +
                                "; allocation left in registry"};
        }
    }
    snapshot.state = AllocationState::Ended;
    reg_.remove_allocation(spec.alloc_id);
    return snapshot;
}

ClusterResult<Instance> ClusterEngine::kill(const KillSpec& spec) {
    if (spec.workspace.empty() || spec.instance.empty()) {
        return EngineError{"--workspace and --instance are required for `cluster kill`"};
    }

    auto lk = reg_.lock();
    struct Match { Allocation* a; Workspace* w; std::size_t idx; };
    std::vector<Match> matches;
    for (auto& a : reg_.allocations) {
        if (spec.alloc_id && a.id != *spec.alloc_id) continue;
        for (auto& w : a.workspaces) {
            if (w.name != spec.workspace) continue;
            for (std::size_t i = 0; i < w.instances.size(); ++i) {
                const auto& inst = w.instances[i];
                const bool id_match   = (inst.id == spec.instance);
                const bool name_match = inst.name.has_value() && *inst.name == spec.instance;
                if (id_match || name_match) matches.push_back({&a, &w, i});
            }
        }
    }

    if (matches.empty()) {
        return EngineError{"no instance '" + spec.instance +
                            "' in workspace '" + spec.workspace + "'"};
    }
    if (matches.size() > 1u) {
        std::string msg = "ambiguous: '" + spec.workspace + "/" + spec.instance +
                           "' present in multiple allocations (";
        for (std::size_t i = 0; i < matches.size(); ++i) {
            if (i) msg += ", ";
            msg += matches[i].a->id;
        }
        msg += "); pass --alloc to pick one";
        return EngineError{std::move(msg)};
    }

    auto& m = matches.front();
    Instance killed = m.w->instances[m.idx];

    const RemoteTarget target{m.a->cluster, m.a->node};
    tmux_.kill_window(target, m.w->tmux_session, killed.tmux_window, ssh_);
    // Confirm the kill took effect before mutating registry. If tmux
    // refused (permission, stale pid, session gone), leave the instance
    // so the user can retry or inspect rather than silently forgetting.
    if (tmux_.is_window_alive(target, m.w->tmux_session, killed.tmux_window, ssh_)) {
        return EngineError{"tmux refused kill-window for " + spec.workspace +
                            "/" + spec.instance +
                            "; instance left in registry"};
    }

    m.w->instances.erase(m.w->instances.begin() + static_cast<std::ptrdiff_t>(m.idx));
    return killed;
}

ClusterResult<ClusterEngine::ProbeReport> ClusterEngine::probe(const ProbeSpec& spec) {
    const Resource* res = find_resource(cfg_, spec.resource);
    if (!res) {
        return EngineError{"unknown resource: " + spec.resource};
    }

    ProbeReport rep;
    rep.resource = res->name;
    for (const auto& r : res->routes) {
        RouteStatus st;
        st.cluster   = r.cluster;
        st.partition = r.partition;
        const auto states = slurm_.sinfo(r.cluster, r.partition, ssh_);
        for (const auto& ps : states) {
            st.idle_nodes    += ps.idle_nodes;
            if (!st.partition_state.empty() && st.partition_state != ps.state) {
                st.partition_state = "mixed";
            } else {
                st.partition_state = ps.state;
            }
            if (r.gres.empty()) {
                st.idle_matching_gres += ps.idle_nodes;
            } else {
                for (const auto& g : ps.idle_gres) {
                    if (g == r.gres) ++st.idle_matching_gres;
                }
            }
        }
        rep.routes.push_back(std::move(st));
    }
    return rep;
}

ClusterResult<ClusterEngine::SyncReport> ClusterEngine::sync(const SyncSpec& spec) {
    auto lk = reg_.lock();
    // Build the set of clusters to probe: each distinct cluster in the
    // registry, optionally filtered to just spec.cluster.
    std::vector<std::string> clusters;
    for (const auto& a : reg_.allocations) {
        if (spec.cluster && a.cluster != *spec.cluster) continue;
        if (std::find(clusters.begin(), clusters.end(), a.cluster) == clusters.end()) {
            clusters.push_back(a.cluster);
        }
    }

    SyncReport rep;
    for (const auto& c : clusters) {
        const auto snap = slurm_.squeue(c, ssh_);
        rep.transitions += reg_.reconcile(c, snap);
        ++rep.clusters_probed;
    }
    return rep;
}

// ── doctor ─────────────────────────────────────────────────────────

namespace {

// Non-whitespace output from `which <bin>` is our proxy for "binary exists".
bool looks_like_path(const std::string& s) {
    for (char c : s) if (c != ' ' && c != '\n' && c != '\r' && c != '\t') return true;
    return false;
}

ClusterEngine::DoctorCheck run_check(ClusterEngine::DoctorCheck::Level lvl,
                                         std::string name,
                                         std::string msg) {
    ClusterEngine::DoctorCheck c;
    c.level = lvl; c.name = std::move(name); c.message = std::move(msg);
    return c;
}

}  // namespace

ClusterResult<ClusterEngine::DoctorReport>
ClusterEngine::doctor(const DoctorSpec& spec) {
    // Pick the clusters to probe.
    std::vector<const Cluster*> targets;
    if (spec.cluster) {
        const Cluster* c = find_cluster(cfg_, *spec.cluster);
        if (!c) {
            return EngineError{"unknown cluster: " + *spec.cluster};
        }
        targets.push_back(c);
    } else {
        for (const auto& c : cfg_.clusters) targets.push_back(&c);
    }

    DoctorReport rep;

    for (const auto* c : targets) {
        DoctorReport::ClusterBlock blk;
        blk.cluster = c->name;

        // 1. SSH reach — "true" is a no-op; exit 0 means we got through.
        const auto reach = ssh_.run(c->name, {"true"}, std::chrono::seconds{5});
        if (reach.exit_code != 0) {
            blk.checks.push_back(run_check(
                DoctorCheck::FAIL,
                "SSH reach: " + c->ssh_host,
                "`ssh " + c->ssh_host + " true` failed (exit "
                    + std::to_string(reach.exit_code) +
                    "). Try `cluster connect " + c->name +
                    "` or check ~/.ssh/config."));
            // Skip follow-up checks (both need a working ssh).
            blk.checks.push_back(run_check(
                DoctorCheck::WARN,
                "sbatch on " + c->ssh_host, "skipped (ssh unreachable)"));
            blk.checks.push_back(run_check(
                DoctorCheck::WARN,
                "tmux on "   + c->ssh_host, "skipped (ssh unreachable)"));
            rep.clusters.push_back(std::move(blk));
            continue;
        }
        blk.checks.push_back(run_check(
            DoctorCheck::OK,
            "SSH reach: " + c->ssh_host,
            "ssh works (password+Duo may still prompt once per session)"));

        // 2. sbatch presence via `which sbatch`.
        const auto sb = ssh_.run(c->name, {"which", "sbatch"}, std::chrono::seconds{5});
        if (sb.exit_code == 0 && looks_like_path(sb.out)) {
            blk.checks.push_back(run_check(
                DoctorCheck::OK,
                "sbatch on " + c->ssh_host,
                sb.out.substr(0, sb.out.find('\n'))));
        } else {
            blk.checks.push_back(run_check(
                DoctorCheck::WARN,
                "sbatch on " + c->ssh_host,
                "`which sbatch` came up empty; is SLURM in PATH on the login node?"));
        }

        // 3. tmux presence via `which tmux`.
        const auto tm = ssh_.run(c->name, {"which", "tmux"}, std::chrono::seconds{5});
        if (tm.exit_code == 0 && looks_like_path(tm.out)) {
            blk.checks.push_back(run_check(
                DoctorCheck::OK,
                "tmux on " + c->ssh_host,
                tm.out.substr(0, tm.out.find('\n'))));
        } else {
            blk.checks.push_back(run_check(
                DoctorCheck::WARN,
                "tmux on " + c->ssh_host,
                "`which tmux` came up empty; install tmux on the cluster to use "
                "cluster launch / attach."));
        }

        rep.clusters.push_back(std::move(blk));
    }

    return rep;
}

ClusterResult<Allocation> ClusterEngine::import(const ImportSpec& spec) {
    if (spec.jobid.empty() || spec.cluster.empty()) {
        return EngineError{"import: jobid and --via <cluster> are required"};
    }
    auto lk = reg_.lock();
    const std::string id = spec.cluster + ":" + spec.jobid;
    if (reg_.find_allocation(id)) {
        return EngineError{"allocation " + id + " is already tracked"};
    }

    const auto snap = slurm_.squeue(spec.cluster, ssh_);
    const JobState* js = nullptr;
    for (const auto& j : snap) {
        if (j.jobid == spec.jobid) { js = &j; break; }
    }
    if (!js) {
        return EngineError{"jobid " + spec.jobid +
                            " not found in squeue for cluster " + spec.cluster};
    }

    Allocation a;
    a.id       = id;
    a.cluster  = spec.cluster;
    a.jobid    = spec.jobid;
    a.resource = spec.resource.value_or("");
    a.node     = js->node;
    a.state    = (js->state == "R") ? AllocationState::Running
                                      : AllocationState::Pending;
    reg_.add_allocation(a);
    return a;
}

// ── launch helpers ─────────────────────────────────────────────

namespace {

// Resolve the allocation to launch into / attach to:
//   - If alloc_id is provided, look it up; error if missing.
//   - Else collect every Running allocation: 0 -> "no running allocation"
//     1 -> use it; >1 -> ambiguity error listing the candidates.
struct AllocPick {
    Allocation* alloc;
    std::string error;
};
AllocPick pick_allocation(Registry& reg,
                            const std::optional<std::string>& alloc_id,
                            std::string_view verb) {
    if (alloc_id) {
        auto* a = reg.find_allocation(*alloc_id);
        if (!a) return {nullptr, "no allocation with id: " + *alloc_id};
        return {a, {}};
    }
    std::vector<Allocation*> running;
    for (auto& a : reg.allocations) {
        if (a.state == AllocationState::Running) running.push_back(&a);
    }
    if (running.empty()) return {nullptr, "no running allocation to " + std::string(verb)};
    if (running.size() == 1u) return {running[0], {}};
    std::string msg = "ambiguous: multiple running allocations (";
    for (std::size_t i = 0; i < running.size(); ++i) {
        if (i) msg += ", ";
        msg += running[i]->id;
    }
    msg += "); pass --alloc to pick one";
    return {nullptr, std::move(msg)};
}

// Find or compute the next instance id ("1", "2", …) for a workspace.
std::string next_instance_id(const Workspace& ws) {
    int max_n = 0;
    for (const auto& i : ws.instances) {
        try { max_n = std::max(max_n, std::stoi(i.id)); } catch (...) {}
    }
    return std::to_string(max_n + 1);
}

// Build the tmux window command: if env_vars are non-empty, prepend
// `env KEY='VAL' ...` so the real tmux_ops (M2) can pass-through, or
// send-keys-style exec picks them up. Values are single-quoted; embedded
// single quotes get the usual '"'"' escape treatment.
std::string build_window_cmd(const std::string& cmd,
                               const std::map<std::string, std::string>& env) {
    if (env.empty()) return cmd;
    std::string out = "env";
    for (const auto& [k, v] : env) {
        std::string q;
        q.reserve(v.size() + 2);
        q += '\'';
        for (char c : v) {
            if (c == '\'') q += "'\"'\"'"; else q += c;
        }
        q += '\'';
        out += ' ';
        out += k;
        out += '=';
        out += q;
    }
    out += ' ';
    out += cmd;
    return out;
}

}  // namespace

// ── launch ─────────────────────────────────────────────────────────

ClusterResult<Instance> ClusterEngine::launch(const LaunchSpec& spec) {
    if (spec.workspace.empty()) {
        return EngineError{"--workspace is required"};
    }
    auto lk = reg_.lock();
    // If both --cmd and --preset are set, --cmd wins silently. This
    // matches the plan's "ad-hoc --cmd bypasses preset" wording.

    // 1. Pick the allocation.
    auto pick = pick_allocation(reg_, spec.alloc_id, "launch into");
    if (!pick.alloc) return EngineError{pick.error};
    Allocation& alloc = *pick.alloc;

    // 2. Resolve the command (preset or ad-hoc).
    std::string cmd;
    std::map<std::string, std::string> env_vars;
    if (spec.cmd) {
        cmd = *spec.cmd;
    } else {
        const std::string preset_name =
            spec.preset.value_or(cfg_.defaults.default_preset);
        const Preset* p = find_preset(cfg_, preset_name);
        if (!p) return EngineError{"unknown preset: " + preset_name};
        auto rr = resolve_preset(*p);
        if (auto* err = std::get_if<PresetResolveError>(&rr)) {
            return EngineError{err->message};
        }
        const auto& rp = std::get<ResolvedPreset>(rr);
        cmd      = rp.command;
        env_vars = rp.env_vars;
    }

    // 3. Find or create the workspace.
    Workspace* ws = nullptr;
    for (auto& w : alloc.workspaces) {
        if (w.name == spec.workspace) { ws = &w; break; }
    }

    const RemoteTarget target{alloc.cluster, alloc.node};
    const std::string cwd = spec.cwd.empty()
        ? cfg_.defaults.workspace_base + "/" + spec.workspace
        : spec.cwd;
    const std::string session_name =
        "tash-" + alloc.cluster + "-" + alloc.jobid + "-" + spec.workspace;

    if (!ws) {
        // New workspace — create tmux session (real impl in M2 treats
        // "duplicate session" as success; fake just records).
        tmux_.new_session(target, session_name, cwd, ssh_);
        Workspace new_ws;
        new_ws.name         = spec.workspace;
        new_ws.cwd          = cwd;
        new_ws.tmux_session = session_name;
        alloc.workspaces.push_back(std::move(new_ws));
        ws = &alloc.workspaces.back();
    }

    // 4. Allocate instance id / window name.
    Instance inst;
    inst.id          = next_instance_id(*ws);
    if (spec.name) {
        inst.name        = *spec.name;
        inst.tmux_window = *spec.name;
    } else {
        inst.tmux_window = inst.id;
    }
    inst.state = InstanceState::Running;

    // 5. Spawn the window.
    const std::string window_cmd = build_window_cmd(cmd, env_vars);
    tmux_.new_window(target, session_name, inst.tmux_window, cwd, window_cmd, ssh_);

    // 6. Liveness check after a short settle window.
    clock_.sleep_for(std::chrono::seconds(2));
    if (!tmux_.is_window_alive(target, session_name, inst.tmux_window, ssh_)) {
        inst.state = InstanceState::Exited;
        notify_.desktop(
            "Instance exited immediately",
            alloc.cluster + " · " + spec.workspace + "/" + inst.tmux_window +
                " — command exited right after launch");
    }

    ws->instances.push_back(inst);
    return inst;
}

// ── attach ─────────────────────────────────────────────────────────

ClusterResult<Instance> ClusterEngine::attach(const AttachSpec& spec) {
    if (spec.workspace.empty() || spec.instance.empty()) {
        return EngineError{"--workspace and --instance are required"};
    }

    auto lk = reg_.lock();
    // Collect every (allocation, workspace, instance) that matches.
    struct Match { Allocation* a; Workspace* w; Instance* i; };
    std::vector<Match> matches;
    for (auto& a : reg_.allocations) {
        if (spec.alloc_id && a.id != *spec.alloc_id) continue;
        for (auto& w : a.workspaces) {
            if (w.name != spec.workspace) continue;
            for (auto& i : w.instances) {
                const bool id_match   = (i.id == spec.instance);
                const bool name_match = i.name.has_value() && *i.name == spec.instance;
                if (id_match || name_match) matches.push_back({&a, &w, &i});
            }
        }
    }

    if (spec.alloc_id && matches.empty()) {
        // Could be: alloc id doesn't exist, or it does but has no such ws/inst.
        if (!reg_.find_allocation(*spec.alloc_id)) {
            return EngineError{"no allocation with id: " + *spec.alloc_id};
        }
        return EngineError{"allocation " + *spec.alloc_id +
                            " has no workspace/instance matching '" +
                            spec.workspace + "/" + spec.instance + "'"};
    }

    if (matches.empty()) {
        // Distinguish missing-workspace from missing-instance for a
        // clearer error message.
        bool any_workspace = false;
        for (auto& a : reg_.allocations) {
            for (auto& w : a.workspaces) {
                if (w.name == spec.workspace) { any_workspace = true; break; }
            }
            if (any_workspace) break;
        }
        if (!any_workspace) {
            return EngineError{"no workspace named '" + spec.workspace + "'"};
        }
        return EngineError{"no instance '" + spec.instance +
                            "' in workspace '" + spec.workspace + "'"};
    }

    if (matches.size() > 1u) {
        std::string msg = "ambiguous: '" + spec.workspace + "/" + spec.instance +
                           "' present in multiple allocations (";
        for (std::size_t i = 0; i < matches.size(); ++i) {
            if (i) msg += ", ";
            msg += matches[i].a->id;
        }
        msg += "); pass --alloc to pick one";
        return EngineError{std::move(msg)};
    }

    // Exactly one — dispatch.
    const auto& m = matches.front();
    const RemoteTarget target{m.a->cluster, m.a->node};
    tmux_.exec_attach(target, m.w->tmux_session, m.i->tmux_window);
    return *m.i;
}

// ── up ─────────────────────────────────────────────────────────────

ClusterResult<Allocation> ClusterEngine::up(const UpSpec& spec) {
    // 1. Resolve resource.
    const Resource* res = find_resource(cfg_, spec.resource);
    if (!res) {
        return EngineError{"unknown resource: " + spec.resource};
    }
    if (res->routes.empty()) {
        return EngineError{"resource '" + spec.resource + "' has no routes declared"};
    }
    auto lk = reg_.lock();

    // 2. Pick candidate routes.
    std::vector<const Route*> candidates;
    if (spec.via) {
        for (const auto& r : res->routes) {
            if (r.cluster == *spec.via) { candidates.push_back(&r); break; }
        }
        if (candidates.empty()) {
            return EngineError{"resource '" + spec.resource +
                                "' has no route via cluster '" + *spec.via + "'"};
        }
    } else {
        for (const auto& r : res->routes) candidates.push_back(&r);
    }

    // 3. Probe each candidate's sinfo; pick first with idle capacity.
    //    Fall back to the first declared candidate if none have idle.
    const Route* chosen = candidates.front();
    for (const auto* r : candidates) {
        if (route_has_idle(slurm_, ssh_, *r)) { chosen = r; break; }
    }

    // 4. Build & submit.
    const SubmitSpec sub = build_submit_spec(*res, *chosen, spec);
    const std::string submitted_at = iso8601_utc_now();
    SubmitResult sr = slurm_.sbatch(sub, ssh_);
    if (sr.jobid.empty()) {
        return EngineError{"sbatch rejected: " +
                            (sr.raw_stdout.empty() ? std::string("<empty response>")
                                                    : sr.raw_stdout)};
    }

    // 5. Poll squeue until R or wait-timeout.
    auto   deadline          = clock_.now() + spec.wait_timeout;
    bool   running           = false;
    bool   detach_and_keep   = false;
    std::string node;

    while (true) {
        const auto jobs = slurm_.squeue(chosen->cluster, ssh_);
        const JobState* mine = nullptr;
        for (const auto& j : jobs) {
            if (j.jobid == sr.jobid) { mine = &j; break; }
        }
        if (mine && mine->state == "R") {
            running = true;
            node    = mine->node;
            break;
        }

        if (clock_.now() >= deadline) {
            const char c = prompt_.choice(
                "job " + sr.jobid + " still queued — [c]ancel [k]eep [d]etach?",
                "ckd");
            if (c == 'c') {
                slurm_.scancel(chosen->cluster, sr.jobid, ssh_);
                return EngineError{"cancelled while queued (job " + sr.jobid + ")"};
            }
            if (c == 'd') {
                detach_and_keep = true;
                break;
            }
            // 'k' or \0 → keep waiting; extend deadline by one full interval.
            deadline = clock_.now() + spec.wait_timeout;
        }

        clock_.sleep_for(std::chrono::milliseconds(250));
    }

    // 6. Build the Allocation record.
    Allocation alloc;
    alloc.id           = chosen->cluster + ":" + sr.jobid;
    alloc.cluster      = chosen->cluster;
    alloc.jobid        = sr.jobid;
    alloc.resource     = res->name;
    alloc.node         = node;                    // empty when detached-pending
    alloc.submitted_at = submitted_at;
    alloc.started_at   = running ? iso8601_utc_now() : std::string{};
    alloc.state        = running         ? AllocationState::Running
                          : detach_and_keep ? AllocationState::Pending
                                            : AllocationState::Pending;
    // 7. Persist.
    reg_.add_allocation(alloc);

    return alloc;
}

}  // namespace tash::cluster
