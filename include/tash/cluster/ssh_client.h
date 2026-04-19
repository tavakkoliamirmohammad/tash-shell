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

}  // namespace tash::cluster

#endif  // TASH_CLUSTER_SSH_CLIENT_H
