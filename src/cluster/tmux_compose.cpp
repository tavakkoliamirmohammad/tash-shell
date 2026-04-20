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
    return "tmux new-session -d -s " + shell_quote(session) +
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
    // Preferred: run inside the SLURM allocation via srun. Works on any
    // site that has a running job for this jobid; doesn't require
    // login→compute ssh to be permitted.
    if (!target.jobid.empty()) {
        return "srun --jobid=" + shell_quote(target.jobid) +
               " --overlap bash -c " + shell_quote(inner);
    }
    // Fallback: direct ssh to the compute node. Requires the site to
    // allow unauthenticated login→compute hops (most CHPC-style sites
    // do not — granite in particular rejects publickey there).
    if (!target.node.empty()) {
        return "ssh " + shell_quote(target.node) + " " + shell_quote(inner);
    }
    // Login-node target: run inner directly.
    return inner;
}

// ══════════════════════════════════════════════════════════════════════════════
// Attach argv — exec form for ssh -t …
// ══════════════════════════════════════════════════════════════════════════════

std::vector<std::string> build_attach_argv(const RemoteTarget& target,
                                              const std::string& session,
                                              const std::string& window) {
    const std::string attach_cmd =
        "tmux attach-session -t " + shell_quote(session + ":" + window);

    // ssh -t <login> is always present; TTY has to propagate end-to-end
    // for tmux to attach properly.
    std::vector<std::string> argv = {"ssh", "-t", target.cluster};
    if (!target.jobid.empty()) {
        // srun --pty preserves the tty inside the allocation; no need
        // for a second ssh hop.
        argv.push_back("srun");
        argv.push_back("--jobid=" + target.jobid);
        argv.push_back("--overlap");
        argv.push_back("--pty");
        argv.push_back(attach_cmd);
    } else if (!target.node.empty()) {
        argv.push_back("ssh");
        argv.push_back("-t");
        argv.push_back(target.node);
        argv.push_back(attach_cmd);
    } else {
        argv.push_back(attach_cmd);
    }
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
