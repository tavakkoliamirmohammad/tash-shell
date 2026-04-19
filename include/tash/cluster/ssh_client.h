// SSH seam. Every operation that goes over the wire flows through this
// interface — including all SLURM invocations (ISlurmOps issues ssh <cluster>
// <cmd> via this seam) and tmux orchestration (ITmuxOps composes ssh <host>
// tmux <subcmd>). The real impl owns the per-cluster ControlMaster socket
// lifecycle; the test fake scripts canned responses.
//
// Design notes:
//
//   - run() is blocking, single-shot. Caller passes a timeout; the impl is
//     free to buffer stdout/stderr entirely before returning.
//   - cluster is a Config::Cluster::name; the impl resolves it to an
//     ssh_host internally. RemoteTarget::node is handled higher up
//     (ITmuxOps composes the full ssh-then-ssh chain as argv).
//   - connect / disconnect are for the ControlMaster master connection,
//     not per-op. A first run() against a cluster with no live master
//     triggers a connect() internally — connect() as a standalone call
//     is an explicit pre-warm (`cluster connect <cluster>` builtin).

#ifndef TASH_CLUSTER_SSH_CLIENT_H
#define TASH_CLUSTER_SSH_CLIENT_H

#include "tash/cluster/types.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tash::cluster {

class ISshClient {
public:
    virtual ~ISshClient() = default;

    virtual SshResult run(const std::string& cluster,
                          const std::vector<std::string>& argv,
                          std::chrono::milliseconds timeout) = 0;

    virtual bool master_alive(const std::string& cluster) = 0;
    virtual void connect      (const std::string& cluster) = 0;
    virtual void disconnect   (const std::string& cluster) = 0;
};

// Resolves a cluster name to the ssh_host entry that `ssh` understands.
// Production wiring consults Config; tests usually pass an identity lambda.
using HostResolver = std::function<std::string(const std::string& cluster)>;

// Production SshClient — OpenSSH wrapper with ControlMaster multiplexing.
//
// Socket path: `<socket_dir>/tash-%C` (ssh expands %C to the per-
// connection hash). Every run() invocation adds:
//    -o ControlMaster=auto -o ControlPath=… -o ControlPersist=yes
//
// Factored so argv construction is pure-string-testable (base_ssh_flags,
// build_run_argv) without needing a live ssh.
std::unique_ptr<ISshClient> make_ssh_client(HostResolver host_resolver,
                                              std::filesystem::path socket_dir);

// Pure argv helpers — exported so unit tests can exercise the structure
// of the command we hand to OpenSSH without actually spawning it.
struct SshFlags {
    std::filesystem::path socket_dir;
    std::string            ssh_host;
    bool                    batch_mode;   // true: fail fast w/o prompts
};
std::vector<std::string> build_run_argv       (const SshFlags&, const std::vector<std::string>& remote);
std::vector<std::string> build_master_check_argv (const SshFlags&);
std::vector<std::string> build_connect_argv    (const SshFlags&);   // background master
std::vector<std::string> build_disconnect_argv (const SshFlags&);   // ssh -O exit

}  // namespace tash::cluster

#endif  // TASH_CLUSTER_SSH_CLIENT_H
