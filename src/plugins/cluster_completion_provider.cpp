// Tab completion for the `cluster` builtin.
// See include/tash/plugins/cluster_completion_provider.h for the contract.

#include "tash/plugins/cluster_completion_provider.h"

#include "tash/cluster/builtin_dispatch.h"
#include "tash/cluster/cluster_engine.h"
#include "tash/cluster/config.h"
#include "tash/cluster/registry.h"

#include <string>
#include <string_view>
#include <vector>

namespace tash::cluster {

// ══════════════════════════════════════════════════════════════════════════════
// Static subcommand + per-sub flag tables
// ══════════════════════════════════════════════════════════════════════════════

namespace {

constexpr const char* kSubcommands[] = {
    "up", "launch", "attach", "list", "down", "kill",
    "sync", "probe", "import", "doctor", "help",
};

const std::vector<std::string>& flags_for(std::string_view sub) {
    static const std::vector<std::string> up = {
        "--resource", "--time", "--cpus", "--mem", "--via", "--name",
    };
    static const std::vector<std::string> launch = {
        "--workspace", "--cwd", "--name", "--preset", "--cmd", "--alloc",
    };
    static const std::vector<std::string> attach = { "--alloc" };
    static const std::vector<std::string> down   = { "--yes", "-y" };
    static const std::vector<std::string> kill_  = { "--alloc", "--yes", "-y" };
    static const std::vector<std::string> sync;
    static const std::vector<std::string> probe  = { "--resource" };
    static const std::vector<std::string> imp    = { "--via", "--resource" };
    static const std::vector<std::string> none;

    if (sub == "up")     return up;
    if (sub == "launch") return launch;
    if (sub == "attach") return attach;
    if (sub == "down")   return down;
    if (sub == "kill")   return kill_;
    if (sub == "sync")   return sync;
    if (sub == "probe")  return probe;
    if (sub == "import") return imp;
    return none;
}

Completion make(std::string text, std::string desc,
                  Completion::Type type = Completion::OPTION_LONG) {
    return Completion(std::move(text), std::move(desc), type);
}

std::vector<Completion> filter_prefix(std::vector<Completion> v,
                                         const std::string& prefix) {
    if (prefix.empty()) return v;
    std::vector<Completion> out;
    out.reserve(v.size());
    for (auto& c : v) {
        if (c.text.rfind(prefix, 0) == 0) out.push_back(std::move(c));
    }
    return out;
}

}  // namespace

// ══════════════════════════════════════════════════════════════════════════════
// ClusterCompletionProvider
// ══════════════════════════════════════════════════════════════════════════════

bool ClusterCompletionProvider::can_complete(const std::string& command) const {
    return command == "cluster";
}

std::vector<Completion> ClusterCompletionProvider::complete(
    const std::string& /*command*/,
    const std::string& current_word,
    const std::vector<std::string>& args,
    const ShellState& /*state*/) const {

    // Subcommand slot.
    if (args.empty()) {
        std::vector<Completion> out;
        for (const auto* s : kSubcommands) {
            out.push_back(make(s, "cluster " + std::string(s),
                                 Completion::SUBCOMMAND));
        }
        return filter_prefix(std::move(out), current_word);
    }

    const std::string& sub  = args.front();
    const std::string& prev = args.back();
    auto*              eng  = active_engine();

    // ── Value-for-flag slots (previous arg tells us what to offer) ──
    if (prev == "-r" || prev == "--resource") {
        std::vector<Completion> out;
        if (eng) for (const auto& r : eng->config().resources)
            out.push_back(make(r.name,
                r.description.empty() ? "resource" : r.description,
                Completion::ARGUMENT));
        return filter_prefix(std::move(out), current_word);
    }

    if (prev == "--via") {
        std::vector<Completion> out;
        if (eng) for (const auto& c : eng->config().clusters)
            out.push_back(make(c.name,
                c.description.empty() ? "cluster" : c.description,
                Completion::ARGUMENT));
        return filter_prefix(std::move(out), current_word);
    }

    if (prev == "--preset") {
        std::vector<Completion> out;
        if (eng) for (const auto& p : eng->config().presets)
            out.push_back(make(p.name, "preset", Completion::ARGUMENT));
        return filter_prefix(std::move(out), current_word);
    }

    if (prev == "--workspace") {
        std::vector<Completion> out;
        if (eng) for (const auto& a : eng->registry().allocations)
            for (const auto& w : a.workspaces) {
                bool dup = false;
                for (const auto& c : out) if (c.text == w.name) { dup = true; break; }
                if (!dup) out.push_back(make(w.name, "workspace",
                                              Completion::ARGUMENT));
            }
        return filter_prefix(std::move(out), current_word);
    }

    if (prev == "--alloc") {
        std::vector<Completion> out;
        if (eng) for (const auto& a : eng->registry().allocations)
            out.push_back(make(a.id,
                a.resource.empty() ? "allocation" : a.resource,
                Completion::ARGUMENT));
        return filter_prefix(std::move(out), current_word);
    }

    // ── Positional slots ──────────────────────────────────────
    if (sub == "attach" || sub == "kill") {
        std::vector<Completion> out;
        if (eng) for (const auto& a : eng->registry().allocations)
            for (const auto& w : a.workspaces)
                for (const auto& i : w.instances)
                    out.push_back(make(w.name + "/" + i.tmux_window,
                                        "instance", Completion::ARGUMENT));
        return filter_prefix(std::move(out), current_word);
    }

    if (sub == "down") {
        std::vector<Completion> out;
        if (eng) for (const auto& a : eng->registry().allocations)
            out.push_back(make(a.id,
                a.resource.empty() ? "allocation" : a.resource,
                Completion::ARGUMENT));
        return filter_prefix(std::move(out), current_word);
    }

    // ── Fallback: offer the subcommand's flag set ─────────────
    std::vector<Completion> out;
    for (const auto& f : flags_for(sub)) {
        out.push_back(make(f, "flag", Completion::OPTION_LONG));
    }
    return filter_prefix(std::move(out), current_word);
}

}  // namespace tash::cluster
