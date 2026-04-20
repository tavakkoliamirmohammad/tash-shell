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

#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>

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
        // 60s because srun --jobid --overlap can take 5-15s on busy
        // clusters to allocate a job step before bash even starts.
        const auto r = ssh.run(t.cluster, {payload}, std::chrono::seconds{60});
        if (r.exit_code == 0) return true;
        // tmux exit code 1 with "duplicate session" is benign — the
        // session already exists from a prior launch, and the upcoming
        // new_window call still works. Any other failure is real.
        const bool dup_ok = r.err.find("duplicate session") != std::string::npos ||
                             r.out.find("duplicate session") != std::string::npos;
        if (!dup_ok) {
            // Emit the actual remote stderr to help the caller's user
            // diagnose cluster-side issues (missing srun flag, missing
            // directory, missing tmux, etc.). write to tash's real stderr.
            std::fprintf(stderr,
                "tash: cluster: tmux new-session stderr: %s\n", r.err.c_str());
            if (!r.out.empty())
                std::fprintf(stderr,
                    "tash: cluster: tmux new-session stdout: %s\n", r.out.c_str());
        }
        return dup_ok;
    }

    bool new_window(const RemoteTarget& t, const std::string& session,
                     const std::string& window, const std::string& cwd,
                     const std::string& cmd, ISshClient& ssh) override {
        const auto inner   = tmux_compose::tmux_new_window(session, window, cwd, cmd);
        const auto payload = tmux_compose::compose_remote_cmd(t, inner);
        const auto r = ssh.run(t.cluster, {payload}, std::chrono::seconds{60});
        if (r.exit_code == 0) return true;
        std::fprintf(stderr,
            "tash: cluster: tmux new-window stderr: %s\n", r.err.c_str());
        if (!r.out.empty())
            std::fprintf(stderr,
                "tash: cluster: tmux new-window stdout: %s\n", r.out.c_str());
        return false;
    }

    std::vector<SessionInfo> list_sessions(const RemoteTarget& t, ISshClient& ssh) override {
        const auto inner   = tmux_compose::tmux_list_sessions();
        const auto payload = tmux_compose::compose_remote_cmd(t, inner);
        const auto r = ssh.run(t.cluster, {payload}, std::chrono::seconds{30});
        if (r.exit_code != 0) return {};
        return tmux_compose::parse_list_sessions(r.out);
    }

    void kill_window(const RemoteTarget& t, const std::string& session,
                      const std::string& window, ISshClient& ssh) override {
        const auto inner   = tmux_compose::tmux_kill_window(session, window);
        const auto payload = tmux_compose::compose_remote_cmd(t, inner);
        (void)ssh.run(t.cluster, {payload}, std::chrono::seconds{30});
    }

    bool is_window_alive(const RemoteTarget& t, const std::string& session,
                           const std::string& window, ISshClient& ssh) override {
        const auto inner   = tmux_compose::tmux_is_alive(session, window);
        const auto payload = tmux_compose::compose_remote_cmd(t, inner);
        const auto r = ssh.run(t.cluster, {payload}, std::chrono::seconds{30});
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
        // Fork + wait, NOT execvp-replace: we want tash to survive
        // when the user detaches from tmux (Ctrl-b d). exec-replace
        // would kill the parent shell along with the ssh-to-tmux
        // connection — surprising UX for anyone used to typing
        // `cluster attach` and returning to their shell after detach.
        //
        // Inherits stdio (tty) for the duration so ssh -t + tmux
        // get their full terminal handoff.
        pid_t pid = ::fork();
        if (pid < 0) {
            std::perror("tash: cluster: attach fork");
            return;
        }
        if (pid == 0) {
            std::vector<char*> c_argv;
            c_argv.reserve(argv.size() + 1);
            for (const auto& a : argv)
                c_argv.push_back(const_cast<char*>(a.c_str()));
            c_argv.push_back(nullptr);
            ::execvp(c_argv[0], c_argv.data());
            std::perror("tash: cluster: attach execvp");
            _exit(127);
        }
        int status = 0;
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    }
};

}  // namespace

std::unique_ptr<ITmuxOps> make_tmux_ops() {
    return std::make_unique<TmuxOpsReal>();
}

}  // namespace tash::cluster
