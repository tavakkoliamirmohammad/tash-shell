// ClusterEngine — orchestration for `cluster <cmd>` verbs. See
// include/tash/cluster/cluster_engine.h for the contract.
//
// Keep I/O out of this file: all spawning, sleeping, prompting, and
// persistence goes through injected seams (ISshClient, ISlurmOps,
// ITmuxOps, INotifier, IPrompt, IClock) or the Registry.

#include "tash/cluster/cluster_engine.h"

#include <chrono>
#include <ctime>
#include <sstream>
#include <string>
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
