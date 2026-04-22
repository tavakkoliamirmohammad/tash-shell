// SLURM command seam. Parses + emits sbatch / squeue / sinfo / scancel
// via an ISshClient. Real impl in src/cluster/slurm_ops.cpp; test fake
// in tests/unit/cluster/fakes/fake_slurm_ops.h.
//
// `squeue` and `sinfo` return std::optional so callers can distinguish
// "probe succeeded, no results" (`some(empty)`) from "probe failed —
// ssh down, Duo expired, remote error" (`nullopt`). Treating the two
// the same has historically caused `cluster sync` to flip every
// Running allocation to Ended on a single transient ssh failure.

#ifndef TASH_CLUSTER_SLURM_OPS_H
#define TASH_CLUSTER_SLURM_OPS_H

#include "tash/cluster/ssh_client.h"
#include "tash/cluster/types.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tash::cluster {

class ISlurmOps {
public:
    virtual ~ISlurmOps() = default;

    virtual SubmitResult sbatch(const SubmitSpec&, ISshClient&) = 0;

    // nullopt = probe failed (ssh error, non-zero exit). Callers MUST
    // NOT interpret that as "no jobs" / "no idle capacity" — it means
    // "we don't know."
    virtual std::optional<std::vector<JobState>>
        squeue(const std::string& cluster, ISshClient&) = 0;
    virtual std::optional<std::vector<PartitionState>>
        sinfo (const std::string& cluster,
                const std::string& partition,
                ISshClient&) = 0;

    // Returns true iff scancel exits 0. When false the caller MUST NOT
    // mutate its own registry for this job — the cluster-side state
    // wasn't confirmed to have changed.
    virtual bool scancel(const std::string& cluster,
                          const std::string& jobid,
                          ISshClient&) = 0;
};

// Construct the production ISlurmOps backed by SLURM's command-line tools.
std::unique_ptr<ISlurmOps> make_slurm_ops();

}  // namespace tash::cluster

#endif  // TASH_CLUSTER_SLURM_OPS_H
