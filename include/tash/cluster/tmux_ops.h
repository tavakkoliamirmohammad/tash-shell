// tmux orchestration seam. Composes `ssh <host> tmux <subcmd>` via an
// ISshClient for the non-exec methods; exec_attach spawns ssh directly
// (no ControlMaster fan-out — attach replaces the current process image
// so the TTY is handed over to tmux/claude).
//
// Real impl in src/cluster/tmux_ops.cpp (lands in M2); test fake in
// tests/unit/cluster/fakes/fake_tmux_ops.h.

#ifndef TASH_CLUSTER_TMUX_OPS_H
#define TASH_CLUSTER_TMUX_OPS_H

#include "tash/cluster/ssh_client.h"
#include "tash/cluster/types.h"

#include <string>
#include <vector>

namespace tash::cluster {

class ITmuxOps {
public:
    virtual ~ITmuxOps() = default;

    virtual void new_session(const RemoteTarget& target,
                              const std::string& session,
                              const std::string& cwd,
                              ISshClient& ssh) = 0;

    virtual void new_window(const RemoteTarget& target,
                             const std::string& session,
                             const std::string& window,
                             const std::string& cwd,
                             const std::string& cmd,
                             ISshClient& ssh) = 0;

    virtual std::vector<SessionInfo> list_sessions(const RemoteTarget& target,
                                                     ISshClient& ssh) = 0;

    virtual void kill_window(const RemoteTarget& target,
                              const std::string& session,
                              const std::string& window,
                              ISshClient& ssh) = 0;

    // Returns true if the window exists AND its pid is alive. Used by
    // launch() after `new_window` to detect commands that exit almost
    // immediately (typo in --cmd, bad preset, etc.). Real impl uses
    // `tmux list-windows -F '#{pane_pid}'` + kill(0, pid).
    virtual bool is_window_alive(const RemoteTarget& target,
                                   const std::string& session,
                                   const std::string& window,
                                   ISshClient& ssh) = 0;

    // exec-style: replaces the current process image with ssh -t <host>
    // tmux attach-session -t <session>. Returns only on spawn failure.
    virtual void exec_attach(const RemoteTarget& target,
                              const std::string& session,
                              const std::string& window) = 0;
};

}  // namespace tash::cluster

#endif  // TASH_CLUSTER_TMUX_OPS_H
