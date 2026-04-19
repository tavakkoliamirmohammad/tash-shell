// Argv dispatcher for the `cluster` builtin.
// See include/tash/cluster/builtin_dispatch.h for the public contract.

#include "tash/cluster/builtin_dispatch.h"

#include <algorithm>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace tash::cluster {

// ══════════════════════════════════════════════════════════════════════════════
// Active engine slot
// ══════════════════════════════════════════════════════════════════════════════

namespace { ClusterEngine* g_engine = nullptr; }

void           set_active_engine(ClusterEngine* e) { g_engine = e; }
ClusterEngine* active_engine()                      { return g_engine; }

// ══════════════════════════════════════════════════════════════════════════════
// Helpers
// ══════════════════════════════════════════════════════════════════════════════

namespace {

void print_err(std::ostream& err, std::string_view msg) {
    err << "tash: cluster: " << msg << "\n";
}

// Fetch --flag value: if argv[i] matches any of `names`, advance i and
// return the next token. Sets found=true on match. Missing value -> nullopt
// with error().
std::optional<std::string> eat_value(std::vector<std::string>& argv,
                                       std::size_t& i,
                                       std::initializer_list<std::string_view> names,
                                       bool& found,
                                       std::ostream& err) {
    for (const auto& name : names) {
        if (argv[i] == name) {
            found = true;
            if (i + 1 >= argv.size()) {
                print_err(err, std::string(name) + " requires a value");
                return std::nullopt;
            }
            std::string v = argv[i + 1];
            argv[i] = "";
            argv[i + 1] = "";
            ++i;
            return v;
        }
    }
    found = false;
    return std::nullopt;
}

// Collect remaining non-flag positional arguments (filtering out tokens
// that argument parsing already consumed into "").
std::vector<std::string> positionals(const std::vector<std::string>& argv) {
    std::vector<std::string> out;
    for (const auto& s : argv) if (!s.empty()) out.push_back(s);
    return out;
}

// ── Help text ─────────────────────────────────────────────────

void print_toplevel_help(std::ostream& out) {
    out << "cluster — SLURM-backed remote launcher\n"
           "\n"
           "usage: cluster <subcommand> [options]\n"
           "\n"
           "subcommands:\n"
           "  up       submit a new allocation\n"
           "  launch   start a long-running instance in a workspace\n"
           "  attach   attach to a running instance\n"
           "  list     show known allocations\n"
           "  down     scancel an allocation\n"
           "  kill     terminate one instance\n"
           "  sync     reconcile against squeue\n"
           "  probe    show current capacity for a resource\n"
           "  import   adopt an externally-submitted jobid\n"
           "  doctor   diagnose ssh / sbatch / tmux on each cluster\n"
           "  help     show per-subcommand help\n"
           "\n"
           "run `cluster <subcommand> --help` for per-subcommand usage.\n";
}

void print_subcommand_help(std::string_view sub, std::ostream& out) {
    if (sub == "up") {
        out << "cluster up — submit a new SLURM allocation\n"
               "\n"
               "usage: cluster up -r <resource> [options]\n"
               "\n"
               "options:\n"
               "  -r, --resource <name>    resource type (required)\n"
               "  -t, --time <HH:MM:SS>    time limit (default: resource default)\n"
               "      --cpus <N>           cpus-per-task override\n"
               "      --mem  <M>           memory override (e.g. 64G)\n"
               "      --via <cluster>      force a specific route's cluster\n"
               "      --name <name>        human-friendly allocation name\n";
    } else if (sub == "launch") {
        out << "cluster launch — start a long-running instance in a workspace\n"
               "\n"
               "usage: cluster launch --workspace <name> [options]\n"
               "\n"
               "options:\n"
               "      --workspace <name>   workspace (required)\n"
               "      --cwd <path>         working dir on the cluster\n"
               "      --name <name>        instance name (overrides auto id)\n"
               "      --preset <name>      preset from config.toml\n"
               "      --cmd <shell-cmd>    run an ad-hoc command (bypasses preset)\n"
               "      --alloc <id>         target a specific allocation\n";
    } else if (sub == "attach") {
        out << "cluster attach — attach to an instance\n"
               "\n"
               "usage: cluster attach <workspace>/<instance> [--alloc <id>]\n";
    } else if (sub == "list") {
        out << "cluster list — show known allocations\n"
               "\n"
               "usage: cluster list [<cluster>]\n";
    } else if (sub == "down") {
        out << "cluster down — scancel an allocation and drop it from the registry\n"
               "\n"
               "usage: cluster down <allocation-id>\n";
    } else if (sub == "kill") {
        out << "cluster kill — terminate one instance (tmux kill-window)\n"
               "\n"
               "usage: cluster kill <workspace>/<instance> [--alloc <id>]\n";
    } else if (sub == "sync") {
        out << "cluster sync — reconcile the registry against squeue\n"
               "\n"
               "usage: cluster sync [<cluster>]\n";
    } else if (sub == "probe") {
        out << "cluster probe — show current capacity across a resource's routes\n"
               "\n"
               "usage: cluster probe -r <resource>\n";
    } else if (sub == "import") {
        out << "cluster import — adopt an externally-submitted SLURM jobid\n"
               "\n"
               "usage: cluster import <jobid> --via <cluster> [--resource <name>]\n";
    } else if (sub == "doctor") {
        out << "cluster doctor — diagnose ssh / sbatch / tmux presence per cluster\n"
               "\n"
               "usage: cluster doctor [<cluster>]\n"
               "\n"
               "Each check reports OK / WARN / FAIL with a one-line hint.\n";
    } else {
        print_toplevel_help(out);
    }
}

// Consume a leading --help / -h token; returns true if handled.
bool handled_help(const std::vector<std::string>& rest,
                   std::string_view sub, std::ostream& out) {
    for (const auto& s : rest) {
        if (s == "--help" || s == "-h") { print_subcommand_help(sub, out); return true; }
    }
    return false;
}

std::string state_to_str(AllocationState s) {
    switch (s) {
        case AllocationState::Pending:     return "pending";
        case AllocationState::Running:     return "running";
        case AllocationState::Ended:       return "ended";
        case AllocationState::Unreachable: return "unreachable";
    }
    return "?";
}
std::string state_to_str(InstanceState s) {
    switch (s) {
        case InstanceState::Running: return "running";
        case InstanceState::Idle:    return "idle";
        case InstanceState::Stopped: return "stopped";
        case InstanceState::Exited:  return "exited";
        case InstanceState::Crashed: return "crashed";
    }
    return "?";
}

// ══════════════════════════════════════════════════════════════════════════════
// Per-command dispatchers
// ══════════════════════════════════════════════════════════════════════════════

int cmd_up(std::vector<std::string> rest, ClusterEngine& eng,
            std::ostream& out, std::ostream& err) {
    if (handled_help(rest, "up", out)) return 0;

    UpSpec spec;
    for (std::size_t i = 0; i < rest.size(); ++i) {
        if (rest[i].empty()) continue;
        bool f;
        if (auto v = eat_value(rest, i, {"-r", "--resource"}, f, err); f)
            { if (!v) return 1; spec.resource = *v; continue; }
        if (auto v = eat_value(rest, i, {"-t", "--time"}, f, err); f)
            { if (!v) return 1; spec.time = *v; continue; }
        if (auto v = eat_value(rest, i, {"--cpus"}, f, err); f)
            { if (!v) return 1; spec.cpus = std::stoi(*v); continue; }
        if (auto v = eat_value(rest, i, {"--mem"}, f, err); f)
            { if (!v) return 1; spec.mem = *v; continue; }
        if (auto v = eat_value(rest, i, {"--via"}, f, err); f)
            { if (!v) return 1; spec.via = *v; continue; }
        if (auto v = eat_value(rest, i, {"--name"}, f, err); f)
            { if (!v) return 1; spec.name = *v; continue; }
        print_err(err, "unknown option: " + rest[i]); return 1;
    }
    if (spec.resource.empty()) {
        print_err(err, "cluster up: -r <resource> is required");
        return 1;
    }

    auto r = eng.up(spec);
    if (auto* a = std::get_if<Allocation>(&r)) {
        out << "allocated " << a->node << " on " << a->cluster
            << " (jobid " << a->jobid << ")";
        if (!a->end_by.empty()) out << " — ends by " << a->end_by;
        out << "\n";
        return 0;
    }
    print_err(err, std::get<EngineError>(r).message);
    return 1;
}

int cmd_launch(std::vector<std::string> rest, ClusterEngine& eng,
                std::ostream& out, std::ostream& err) {
    if (handled_help(rest, "launch", out)) return 0;

    LaunchSpec spec;
    for (std::size_t i = 0; i < rest.size(); ++i) {
        if (rest[i].empty()) continue;
        bool f;
        if (auto v = eat_value(rest, i, {"--workspace"}, f, err); f)
            { if (!v) return 1; spec.workspace = *v; continue; }
        if (auto v = eat_value(rest, i, {"--cwd"}, f, err); f)
            { if (!v) return 1; spec.cwd = *v; continue; }
        if (auto v = eat_value(rest, i, {"--name"}, f, err); f)
            { if (!v) return 1; spec.name = *v; continue; }
        if (auto v = eat_value(rest, i, {"--preset"}, f, err); f)
            { if (!v) return 1; spec.preset = *v; continue; }
        if (auto v = eat_value(rest, i, {"--cmd"}, f, err); f)
            { if (!v) return 1; spec.cmd = *v; continue; }
        if (auto v = eat_value(rest, i, {"--alloc"}, f, err); f)
            { if (!v) return 1; spec.alloc_id = *v; continue; }
        print_err(err, "unknown option: " + rest[i]); return 1;
    }
    if (spec.workspace.empty()) {
        print_err(err, "cluster launch: --workspace is required");
        return 1;
    }
    auto r = eng.launch(spec);
    if (auto* i = std::get_if<Instance>(&r)) {
        out << "launched instance " << i->id
            << " (window '" << i->tmux_window << "')"
            << " — state=" << state_to_str(i->state) << "\n";
        return 0;
    }
    print_err(err, std::get<EngineError>(r).message);
    return 1;
}

int cmd_attach(std::vector<std::string> rest, ClusterEngine& eng,
                std::ostream& out, std::ostream& err) {
    if (handled_help(rest, "attach", out)) return 0;

    AttachSpec spec;
    std::string positional;
    for (std::size_t i = 0; i < rest.size(); ++i) {
        if (rest[i].empty()) continue;
        bool f;
        if (auto v = eat_value(rest, i, {"--alloc"}, f, err); f)
            { if (!v) return 1; spec.alloc_id = *v; continue; }
        if (positional.empty()) { positional = rest[i]; rest[i].clear(); continue; }
        print_err(err, "cluster attach: unexpected argument: " + rest[i]);
        return 1;
    }
    if (positional.empty()) {
        print_err(err, "cluster attach: workspace/instance is required");
        return 1;
    }
    const auto slash = positional.find('/');
    if (slash == std::string::npos) {
        print_err(err, "cluster attach: expected workspace/instance, got: " + positional);
        return 1;
    }
    spec.workspace = positional.substr(0, slash);
    spec.instance  = positional.substr(slash + 1);

    auto r = eng.attach(spec);
    if (std::get_if<Instance>(&r)) { (void)out; return 0; }
    print_err(err, std::get<EngineError>(r).message);
    return 1;
}

int cmd_list(std::vector<std::string> rest, ClusterEngine& eng,
              std::ostream& out, std::ostream& err) {
    if (handled_help(rest, "list", out)) return 0;

    ListSpec spec;
    auto pos = positionals(rest);
    if (pos.size() == 1) spec.cluster = pos[0];
    else if (pos.size() > 1) {
        print_err(err, "cluster list: expected at most one cluster argument");
        return 1;
    }

    auto r = eng.list(spec);
    if (auto* v = std::get_if<std::vector<Allocation>>(&r)) {
        if (v->empty()) { out << "(no allocations)\n"; return 0; }
        for (const auto& a : *v) {
            out << a.id << "  " << a.resource << "  " << a.node
                << "  " << state_to_str(a.state) << "\n";
            for (const auto& w : a.workspaces) {
                out << "  " << w.name << "  "
                    << w.instances.size() << " instances\n";
                for (const auto& inst : w.instances) {
                    out << "    [" << inst.id << "]  "
                        << state_to_str(inst.state) << "  "
                        << inst.tmux_window << "\n";
                }
            }
        }
        return 0;
    }
    print_err(err, std::get<EngineError>(r).message);
    return 1;
}

int cmd_down(std::vector<std::string> rest, ClusterEngine& eng,
              std::ostream& out, std::ostream& err) {
    if (handled_help(rest, "down", out)) return 0;

    bool yes = false;
    for (auto& t : rest) {
        if (t == "-y" || t == "--yes") { yes = true; t.clear(); }
    }

    auto pos = positionals(rest);
    if (pos.size() != 1) {
        print_err(err, "cluster down: exactly one <allocation-id> is required");
        return 1;
    }
    const std::string alloc_id = pos[0];

    if (!yes) {
        // Build a preview. Pull allocation details if we can find it;
        // otherwise fall back to a terser prompt.
        std::string preview = "cancel allocation " + alloc_id;
        if (const Allocation* a = eng.registry().find_allocation(alloc_id)) {
            if (!a->resource.empty()) preview += " (" + a->resource + ")";
            if (!a->node.empty())     preview += " on "    + a->node;
        }
        preview += "? [y/n]";
        const char c = eng.prompt().choice(preview, "yn");
        if (c != 'y' && c != 'Y') {
            print_err(err, "cancelled by user");
            return 1;
        }
    }

    DownSpec spec; spec.alloc_id = alloc_id;
    auto r = eng.down(spec);
    if (auto* a = std::get_if<Allocation>(&r)) {
        out << "cancelled allocation " << a->id << "\n";
        return 0;
    }
    print_err(err, std::get<EngineError>(r).message);
    return 1;
}

int cmd_kill(std::vector<std::string> rest, ClusterEngine& eng,
              std::ostream& out, std::ostream& err) {
    if (handled_help(rest, "kill", out)) return 0;

    bool yes = false;
    KillSpec spec;
    std::string positional;
    for (std::size_t i = 0; i < rest.size(); ++i) {
        if (rest[i].empty()) continue;
        if (rest[i] == "-y" || rest[i] == "--yes") { yes = true; rest[i].clear(); continue; }
        bool f;
        if (auto v = eat_value(rest, i, {"--alloc"}, f, err); f)
            { if (!v) return 1; spec.alloc_id = *v; continue; }
        if (positional.empty()) { positional = rest[i]; rest[i].clear(); continue; }
        print_err(err, "cluster kill: unexpected argument: " + rest[i]);
        return 1;
    }
    if (positional.empty()) {
        print_err(err, "cluster kill: workspace/instance is required");
        return 1;
    }
    const auto slash = positional.find('/');
    if (slash == std::string::npos) {
        print_err(err, "cluster kill: expected workspace/instance");
        return 1;
    }
    spec.workspace = positional.substr(0, slash);
    spec.instance  = positional.substr(slash + 1);

    if (!yes) {
        const std::string preview =
            "kill " + spec.workspace + "/" + spec.instance + "? [y/n]";
        const char c = eng.prompt().choice(preview, "yn");
        if (c != 'y' && c != 'Y') {
            print_err(err, "cancelled by user");
            return 1;
        }
    }

    auto r = eng.kill(spec);
    if (auto* i = std::get_if<Instance>(&r)) {
        out << "killed " << spec.workspace << "/" << i->tmux_window << "\n";
        return 0;
    }
    print_err(err, std::get<EngineError>(r).message);
    return 1;
}

int cmd_sync(std::vector<std::string> rest, ClusterEngine& eng,
              std::ostream& out, std::ostream& err) {
    if (handled_help(rest, "sync", out)) return 0;

    SyncSpec spec;
    auto pos = positionals(rest);
    if (pos.size() == 1) spec.cluster = pos[0];
    else if (pos.size() > 1) {
        print_err(err, "cluster sync: expected at most one cluster argument");
        return 1;
    }
    auto r = eng.sync(spec);
    if (auto* s = std::get_if<ClusterEngine::SyncReport>(&r)) {
        out << "probed " << s->clusters_probed << " cluster(s), "
            << s->transitions << " transitions\n";
        return 0;
    }
    print_err(err, std::get<EngineError>(r).message);
    return 1;
}

int cmd_probe(std::vector<std::string> rest, ClusterEngine& eng,
               std::ostream& out, std::ostream& err) {
    if (handled_help(rest, "probe", out)) return 0;

    ProbeSpec spec;
    for (std::size_t i = 0; i < rest.size(); ++i) {
        if (rest[i].empty()) continue;
        bool f;
        if (auto v = eat_value(rest, i, {"-r", "--resource"}, f, err); f)
            { if (!v) return 1; spec.resource = *v; continue; }
        print_err(err, "unknown option: " + rest[i]); return 1;
    }
    if (spec.resource.empty()) {
        print_err(err, "cluster probe: -r <resource> is required");
        return 1;
    }
    auto r = eng.probe(spec);
    if (auto* rep = std::get_if<ClusterEngine::ProbeReport>(&r)) {
        out << "resource " << rep->resource << ":\n";
        for (const auto& rs : rep->routes) {
            out << "  " << rs.cluster << "/" << rs.partition
                << "  " << rs.partition_state
                << "  " << rs.idle_nodes << " idle, "
                << rs.idle_matching_gres << " matching\n";
        }
        return 0;
    }
    print_err(err, std::get<EngineError>(r).message);
    return 1;
}

int cmd_import(std::vector<std::string> rest, ClusterEngine& eng,
                std::ostream& out, std::ostream& err) {
    if (handled_help(rest, "import", out)) return 0;

    ImportSpec spec;
    std::string positional;
    for (std::size_t i = 0; i < rest.size(); ++i) {
        if (rest[i].empty()) continue;
        bool f;
        if (auto v = eat_value(rest, i, {"--via"}, f, err); f)
            { if (!v) return 1; spec.cluster = *v; continue; }
        if (auto v = eat_value(rest, i, {"--resource"}, f, err); f)
            { if (!v) return 1; spec.resource = *v; continue; }
        if (positional.empty()) { positional = rest[i]; rest[i].clear(); continue; }
        print_err(err, "cluster import: unexpected argument: " + rest[i]);
        return 1;
    }
    if (positional.empty()) {
        print_err(err, "cluster import: <jobid> is required");
        return 1;
    }
    if (spec.cluster.empty()) {
        print_err(err, "cluster import: --via <cluster> is required");
        return 1;
    }
    spec.jobid = positional;

    auto r = eng.import(spec);
    if (auto* a = std::get_if<Allocation>(&r)) {
        out << "imported " << a->id << "  " << state_to_str(a->state) << "\n";
        return 0;
    }
    print_err(err, std::get<EngineError>(r).message);
    return 1;
}

int cmd_doctor(std::vector<std::string> rest, ClusterEngine& eng,
                std::ostream& out, std::ostream& err) {
    if (handled_help(rest, "doctor", out)) return 0;

    ClusterEngine::DoctorSpec spec;
    auto pos = positionals(rest);
    if (pos.size() == 1)      spec.cluster = pos[0];
    else if (pos.size() > 1)  { print_err(err, "cluster doctor: expected at most one cluster argument"); return 1; }

    auto r = eng.doctor(spec);
    if (auto* err_ = std::get_if<EngineError>(&r)) {
        print_err(err, err_->message);
        return 1;
    }
    const auto& rep = std::get<ClusterEngine::DoctorReport>(r);
    bool any_fail = false;
    for (const auto& blk : rep.clusters) {
        out << blk.cluster << ":\n";
        for (const auto& c : blk.checks) {
            const char* tag = "ok  ";
            switch (c.level) {
                case ClusterEngine::DoctorCheck::OK:   tag = "ok  "; break;
                case ClusterEngine::DoctorCheck::WARN: tag = "warn"; break;
                case ClusterEngine::DoctorCheck::FAIL: tag = "fail"; any_fail = true; break;
            }
            out << "  [" << tag << "] " << c.name << " — " << c.message << "\n";
        }
    }
    return any_fail ? 1 : 0;
}

}  // namespace

// ══════════════════════════════════════════════════════════════════════════════
// dispatch_cluster
// ══════════════════════════════════════════════════════════════════════════════

int dispatch_cluster(const std::vector<std::string>& argv,
                      ClusterEngine& eng,
                      std::ostream& out,
                      std::ostream& err) {
    if (argv.size() < 2) {
        print_toplevel_help(out);
        return 0;
    }
    const std::string& sub = argv[1];
    if (sub == "--help" || sub == "-h" || sub == "help") {
        if (sub == "help" && argv.size() >= 3) {
            print_subcommand_help(argv[2], out);
        } else {
            print_toplevel_help(out);
        }
        return 0;
    }
    std::vector<std::string> rest(argv.begin() + 2, argv.end());

    if (sub == "up")     return cmd_up    (std::move(rest), eng, out, err);
    if (sub == "launch") return cmd_launch(std::move(rest), eng, out, err);
    if (sub == "attach") return cmd_attach(std::move(rest), eng, out, err);
    if (sub == "list")   return cmd_list  (std::move(rest), eng, out, err);
    if (sub == "down")   return cmd_down  (std::move(rest), eng, out, err);
    if (sub == "kill")   return cmd_kill  (std::move(rest), eng, out, err);
    if (sub == "sync")   return cmd_sync  (std::move(rest), eng, out, err);
    if (sub == "probe")  return cmd_probe (std::move(rest), eng, out, err);
    if (sub == "import") return cmd_import(std::move(rest), eng, out, err);
    if (sub == "doctor") return cmd_doctor(std::move(rest), eng, out, err);

    print_err(err, "unknown subcommand: " + sub);
    return 1;
}

}  // namespace tash::cluster
