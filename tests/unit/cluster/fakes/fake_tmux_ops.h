// FakeTmuxOps — header-only test double for ITmuxOps.
//
// Void methods just record; list_sessions pulls from a FIFO queue.
// exec_attach (exec-style) also just records — the test never actually
// replaces the process; we assert the intent.

#ifndef TASH_CLUSTER_FAKE_TMUX_OPS_H
#define TASH_CLUSTER_FAKE_TMUX_OPS_H

#include "tash/cluster/tmux_ops.h"

#include <deque>
#include <string>
#include <vector>

namespace tash::cluster::testing {

class FakeTmuxOps : public ITmuxOps {
public:
    struct NewSessionCall {
        RemoteTarget target;
        std::string  session;
        std::string  cwd;
    };
    struct NewWindowCall {
        RemoteTarget target;
        std::string  session;
        std::string  window;
        std::string  cwd;
        std::string  cmd;
    };
    struct ListSessionsCall { RemoteTarget target; };
    struct KillWindowCall   { RemoteTarget target; std::string session; std::string window; };
    struct ExecAttachCall   { RemoteTarget target; std::string session; std::string window; };

    std::vector<NewSessionCall>    new_session_calls;
    std::vector<NewWindowCall>     new_window_calls;
    std::vector<ListSessionsCall>  list_sessions_calls;
    std::vector<KillWindowCall>    kill_window_calls;
    std::vector<ExecAttachCall>    exec_attach_calls;

    std::deque<std::vector<SessionInfo>> list_sessions_queue;

    void queue_list_sessions(std::vector<SessionInfo> s) { list_sessions_queue.push_back(std::move(s)); }

    void new_session(const RemoteTarget& t, const std::string& s,
                      const std::string& cwd, ISshClient&) override {
        new_session_calls.push_back({t, s, cwd});
    }

    void new_window(const RemoteTarget& t, const std::string& s,
                     const std::string& w, const std::string& cwd,
                     const std::string& cmd, ISshClient&) override {
        new_window_calls.push_back({t, s, w, cwd, cmd});
    }

    std::vector<SessionInfo> list_sessions(const RemoteTarget& t, ISshClient&) override {
        list_sessions_calls.push_back({t});
        if (list_sessions_queue.empty()) return {};
        auto r = list_sessions_queue.front(); list_sessions_queue.pop_front();
        return r;
    }

    void kill_window(const RemoteTarget& t, const std::string& s,
                      const std::string& w, ISshClient&) override {
        kill_window_calls.push_back({t, s, w});
    }

    void exec_attach(const RemoteTarget& t, const std::string& s,
                      const std::string& w) override {
        exec_attach_calls.push_back({t, s, w});
    }

    void reset() {
        new_session_calls.clear();
        new_window_calls.clear();
        list_sessions_calls.clear();
        kill_window_calls.clear();
        exec_attach_calls.clear();
        list_sessions_queue.clear();
    }
};

}  // namespace tash::cluster::testing

#endif  // TASH_CLUSTER_FAKE_TMUX_OPS_H
