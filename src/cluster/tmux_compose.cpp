// Pure tmux command + parser helpers. See header for the contract.

#include "tash/cluster/tmux_compose.h"

#include <cctype>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tash::cluster::tmux_compose {

// ══════════════════════════════════════════════════════════════════════════════
// shell_quote — '…' with '\\'' for embedded single quotes
// ══════════════════════════════════════════════════════════════════════════════

std::string shell_quote(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '\'';
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else           out += c;
    }
    out += '\'';
    return out;
}

// ══════════════════════════════════════════════════════════════════════════════
// tmux command builders
// ══════════════════════════════════════════════════════════════════════════════

std::string tmux_new_session(const std::string& session, const std::string& cwd) {
    // Run on the LOGIN node (see compose_remote_cmd for the
    // architecture rationale). mkdir -p in case the cwd is a fresh
    // scratch path the user never touched before.
    return "mkdir -p " + shell_quote(cwd) +
           " && tmux new-session -d -s " + shell_quote(session) +
             " -c " + shell_quote(cwd);
}

std::string tmux_new_window(const std::string& session, const std::string& window,
                              const std::string& cwd,    const std::string& cmd) {
    return "tmux new-window -t " + shell_quote(session) +
             " -n " + shell_quote(window) +
             " -c " + shell_quote(cwd) +
             " "    + shell_quote(cmd);
}

std::string tmux_list_sessions() {
    return "tmux list-sessions -F "
           "'#{session_name}|#{session_windows}|#{?session_attached,1,0}'";
}

std::string tmux_list_windows(const std::string& session) {
    return "tmux list-windows -t " + shell_quote(session) +
             " -F '#{window_name}|#{pane_pid}'";
}

std::string tmux_kill_window(const std::string& session, const std::string& window) {
    return "tmux kill-window -t " + shell_quote(session + ":" + window);
}

std::string tmux_is_alive(const std::string& session, const std::string& window) {
    return "tmux list-windows -t " + shell_quote(session + ":" + window) +
             " -F '#{pane_pid}'";
}

// ══════════════════════════════════════════════════════════════════════════════
// compose_remote_cmd — optional compute-node hop
// ══════════════════════════════════════════════════════════════════════════════

std::string compose_remote_cmd(const RemoteTarget& target, const std::string& inner) {
    // Architecture (for both jobid and login-only targets): the tmux
    // *server* runs on the cluster's login node, because SLURM's
    // cgroup-based proctrack kills everything — including supposedly-
    // detached tmux servers — in a step's cgroup when the step ends.
    // `tmux new-session -d` from inside any srun step would have its
    // forked server killed immediately.
    //
    // The workload commands that want to run on the compute node get
    // srun-wrapped INSIDE the tmux window's command string, not at
    // this outer layer. See wrap_for_compute() in cluster_engine.cpp.
    //
    // Node-only targets (legacy ssh-to-compute path, kept for sites
    // that permit it and don't provide a jobid) still get the ssh hop.
    if (target.jobid.empty() && !target.node.empty()) {
        return "ssh " + shell_quote(target.node) + " " + shell_quote(inner);
    }
    return inner;
}

// ══════════════════════════════════════════════════════════════════════════════
// Attach argv — exec form for ssh -t …
// ══════════════════════════════════════════════════════════════════════════════

std::vector<std::string> build_attach_argv(const RemoteTarget& target,
                                              const std::string& session,
                                              const std::string& window) {
    // tmux server lives on the login node (see compose_remote_cmd).
    // Attach is therefore just `ssh -t <login> tmux attach-session`;
    // the srun step inside the window connects us to whatever workload
    // is running on the compute side automatically.
    const std::string attach_cmd =
        "tmux attach-session -t " + shell_quote(session + ":" + window);

    std::vector<std::string> argv = {"ssh", "-t", target.cluster};
    // Legacy compute-node-hosted path: only taken when explicitly
    // directed at a compute node with no jobid.
    if (target.jobid.empty() && !target.node.empty()) {
        argv.push_back("ssh");
        argv.push_back("-t");
        argv.push_back(target.node);
    }
    argv.push_back(attach_cmd);
    return argv;
}

// ══════════════════════════════════════════════════════════════════════════════
// Parsers
// ══════════════════════════════════════════════════════════════════════════════

namespace {
std::vector<std::string> split(std::string_view s, char delim) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == delim) {
            out.emplace_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    out.emplace_back(s.substr(start));
    return out;
}
}  // namespace

std::vector<SessionInfo> parse_list_sessions(std::string_view output) {
    std::vector<SessionInfo> out;
    std::istringstream ss{std::string(output)};
    std::string line;
    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        const auto parts = split(line, '|');
        if (parts.size() < 3) continue;
        SessionInfo s;
        s.name         = parts[0];
        try { s.window_count = std::stoi(parts[1]); } catch (...) { s.window_count = 0; }
        s.attached     = (parts[2] == "1");
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<std::pair<std::string, long>>
parse_list_windows(std::string_view output) {
    std::vector<std::pair<std::string, long>> out;
    std::istringstream ss{std::string(output)};
    std::string line;
    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        const auto parts = split(line, '|');
        if (parts.size() < 2) continue;
        long pid = 0;
        try { pid = std::stol(parts[1]); } catch (...) { pid = 0; }
        out.emplace_back(parts[0], pid);
    }
    return out;
}

}  // namespace tash::cluster::tmux_compose
