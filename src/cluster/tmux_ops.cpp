// TmuxOpsReal — stateful impl that composes tmux commands via
// tmux_compose + dispatches through an injected ISshClient. See
// include/tash/cluster/tmux_ops.h for the contract.
//
// exec_attach is the one method that doesn't go through ISshClient —
// it replaces the current process image with `ssh -t …`, so there's
// nothing to capture. Tests assert on the argv we'd have exec'd; the
// real impl actually execvp's.

#include "tash/cluster/tmux_ops.h"
#include "tash/cluster/tmux_compose.h"

#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

namespace tash::cluster {

namespace {

class TmuxOpsReal : public ITmuxOps {
public:
    bool new_session(const RemoteTarget& t, const std::string& session,
                      const std::string& cwd, ISshClient& ssh) override {
        const auto inner   = tmux_compose::tmux_new_session(session, cwd);
        const auto payload = tmux_compose::compose_remote_cmd(t, inner);
        const auto r = ssh.run(t.cluster, {payload}, std::chrono::seconds{10});
        if (r.exit_code == 0) return true;
        // tmux exit code 1 with "duplicate session" is benign — the
        // session already exists from a prior launch, and the upcoming
        // new_window call still works. Any other failure is real.
        const bool dup_ok = r.err.find("duplicate session") != std::string::npos ||
                             r.out.find("duplicate session") != std::string::npos;
        return dup_ok;
    }

    bool new_window(const RemoteTarget& t, const std::string& session,
                     const std::string& window, const std::string& cwd,
                     const std::string& cmd, ISshClient& ssh) override {
        const auto inner   = tmux_compose::tmux_new_window(session, window, cwd, cmd);
        const auto payload = tmux_compose::compose_remote_cmd(t, inner);
        const auto r = ssh.run(t.cluster, {payload}, std::chrono::seconds{10});
        return r.exit_code == 0;
    }

    std::vector<SessionInfo> list_sessions(const RemoteTarget& t, ISshClient& ssh) override {
        const auto inner   = tmux_compose::tmux_list_sessions();
        const auto payload = tmux_compose::compose_remote_cmd(t, inner);
        const auto r = ssh.run(t.cluster, {payload}, std::chrono::seconds{5});
        if (r.exit_code != 0) return {};
        return tmux_compose::parse_list_sessions(r.out);
    }

    void kill_window(const RemoteTarget& t, const std::string& session,
                      const std::string& window, ISshClient& ssh) override {
        const auto inner   = tmux_compose::tmux_kill_window(session, window);
        const auto payload = tmux_compose::compose_remote_cmd(t, inner);
        (void)ssh.run(t.cluster, {payload}, std::chrono::seconds{5});
    }

    bool is_window_alive(const RemoteTarget& t, const std::string& session,
                           const std::string& window, ISshClient& ssh) override {
        const auto inner   = tmux_compose::tmux_is_alive(session, window);
        const auto payload = tmux_compose::compose_remote_cmd(t, inner);
        const auto r = ssh.run(t.cluster, {payload}, std::chrono::seconds{5});
        if (r.exit_code != 0) return false;
        // Non-empty pid output => alive.
        for (char c : r.out) {
            if (c >= '0' && c <= '9') return true;
        }
        return false;
    }

    void exec_attach(const RemoteTarget& t, const std::string& session,
                      const std::string& window) override {
        const auto argv = tmux_compose::build_attach_argv(t, session, window);
        if (argv.empty()) return;
        std::vector<char*> c_argv;
        c_argv.reserve(argv.size() + 1);
        for (const auto& a : argv) c_argv.push_back(const_cast<char*>(a.c_str()));
        c_argv.push_back(nullptr);
        ::execvp(c_argv[0], c_argv.data());
        // Only reached on exec failure. Write a useful error to stderr.
        std::perror("tash: cluster: exec_attach execvp");
        std::exit(127);
    }
};

}  // namespace

std::unique_ptr<ITmuxOps> make_tmux_ops() {
    return std::make_unique<TmuxOpsReal>();
}

}  // namespace tash::cluster
