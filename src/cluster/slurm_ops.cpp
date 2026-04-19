// Real ISlurmOps — composes slurm_parse::build_*_argv and parse_* with
// an injected ISshClient. All I/O happens inside ssh.run(); this file
// is pure glue.

#include "tash/cluster/slurm_ops.h"
#include "tash/cluster/slurm_parse.h"

#include <chrono>
#include <memory>

namespace tash::cluster {

namespace {

class SlurmOpsReal : public ISlurmOps {
public:
    SubmitResult sbatch(const SubmitSpec& spec, ISshClient& ssh) override {
        const auto argv = slurm_parse::build_sbatch_argv(spec);
        const auto r    = ssh.run(spec.cluster, argv, std::chrono::seconds{30});
        if (r.exit_code != 0) {
            // Surface whichever of stderr/stdout contained the error
            // for the engine's "sbatch rejected" error path.
            return SubmitResult{/*jobid*/"", r.err.empty() ? r.out : r.err};
        }
        return SubmitResult{slurm_parse::parse_sbatch_jobid(r.out), r.out};
    }

    std::vector<JobState> squeue(const std::string& cluster, ISshClient& ssh) override {
        const auto argv = slurm_parse::build_squeue_argv();
        const auto r    = ssh.run(cluster, argv, std::chrono::seconds{10});
        if (r.exit_code != 0) return {};
        return slurm_parse::parse_squeue(r.out);
    }

    std::vector<PartitionState> sinfo(const std::string& cluster,
                                        const std::string& partition,
                                        ISshClient& ssh) override {
        const auto argv = slurm_parse::build_sinfo_argv(partition);
        const auto r    = ssh.run(cluster, argv, std::chrono::seconds{10});
        if (r.exit_code != 0) return {};
        return slurm_parse::parse_sinfo(r.out);
    }

    void scancel(const std::string& cluster,
                  const std::string& jobid,
                  ISshClient& ssh) override {
        (void)ssh.run(cluster,
                       slurm_parse::build_scancel_argv(jobid),
                       std::chrono::seconds{10});
    }
};

}  // namespace

std::unique_ptr<ISlurmOps> make_slurm_ops() {
    return std::make_unique<SlurmOpsReal>();
}

}  // namespace tash::cluster
