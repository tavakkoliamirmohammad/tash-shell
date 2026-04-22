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

#include <memory>
#include <string>
#include <vector>

namespace tash::cluster {

// Outcome of a tmux liveness probe. Distinguishes "window is really
// dead" from "we couldn't ask" so callers don't mistake a transient
// ssh blip for a process exit.
enum class Liveness {
    Alive,    // Window exists and its pane pid is running.
    Dead,     // Probe succeeded; window is gone or pane pid is not alive.
    Unknown,  // Probe itself failed (ssh down, permission, etc.).
};

class ITmuxOps {
public:
    virtual ~ITmuxOps() = default;

    // Creates a tmux session. Returns true iff ssh exit == 0 or the
    // session already existed (tmux signals "duplicate session" as an
    // error but we treat it as success — subsequent new_window is
    // idempotent too). On hard failure (ssh down, permission), callers
    // MUST NOT record the workspace in their registry.
    virtual bool new_session(const RemoteTarget& target,
                              const std::string& session,
                              const std::string& cwd,
                              ISshClient& ssh) = 0;

    // Creates a tmux window inside an existing session. Returns true
    // iff ssh exit == 0. On failure, callers MUST NOT record the
    // instance in their registry.
    virtual bool new_window(const RemoteTarget& target,
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

    // Tri-state liveness probe used by launch() (to detect commands
    // that exit immediately) and kill() (to confirm a kill took). The
    // caller MUST treat `Unknown` as "don't change state" — a transient
    // ssh failure historically caused false-Exited notifications on
    // launch and false-success on kill.
    virtual Liveness is_window_alive(const RemoteTarget& target,
                                       const std::string& session,
                                       const std::string& window,
                                       ISshClient& ssh) = 0;

    // exec-style: replaces the current process image with ssh -t <host>
    // tmux attach-session -t <session>. Returns only on spawn failure.
    virtual void exec_attach(const RemoteTarget& target,
                              const std::string& session,
                              const std::string& window) = 0;
};

// Production TmuxOps — composes tmux commands + shell-quotes them,
// dispatches through an ISshClient supplied at call time.
std::unique_ptr<ITmuxOps> make_tmux_ops();

}  // namespace tash::cluster

#endif  // TASH_CLUSTER_TMUX_OPS_H
