// FakeSlurmOps — header-only test double for ISlurmOps.
//
// Each ISlurmOps method has (a) a public vector of recorded calls and
// (b) a FIFO queue of scripted return values. Void methods (scancel) just
// record.
//
// squeue/sinfo return std::optional. Queue a std::nullopt (via
// queue_squeue_fail / queue_sinfo_fail) to model a probe failure —
// empty-vector return means "probe succeeded, no results". Empty queue
// falls back to some({}) so existing tests that never explicitly queue
// keep their old semantics.

#ifndef TASH_CLUSTER_FAKE_SLURM_OPS_H
#define TASH_CLUSTER_FAKE_SLURM_OPS_H

#include "tash/cluster/slurm_ops.h"

#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace tash::cluster::testing {

class FakeSlurmOps : public ISlurmOps {
public:
    struct SbatchCall  { SubmitSpec spec; };
    struct SqueueCall  { std::string cluster; };
    struct SinfoCall   { std::string cluster; std::string partition; };
    struct ScancelCall { std::string cluster; std::string jobid; };

    std::vector<SbatchCall>  sbatch_calls;
    std::vector<SqueueCall>  squeue_calls;
    std::vector<SinfoCall>   sinfo_calls;
    std::vector<ScancelCall> scancel_calls;

    std::deque<SubmitResult>                              sbatch_queue;
    std::deque<std::optional<std::vector<JobState>>>      squeue_queue;
    std::deque<std::optional<std::vector<PartitionState>>> sinfo_queue;

    void queue_sbatch(SubmitResult r)                 { sbatch_queue.push_back(std::move(r)); }
    void queue_squeue(std::vector<JobState> r)        { squeue_queue.push_back(std::move(r)); }
    void queue_sinfo (std::vector<PartitionState> r)  { sinfo_queue .push_back(std::move(r)); }
    void queue_squeue_fail()                          { squeue_queue.push_back(std::nullopt); }
    void queue_sinfo_fail ()                          { sinfo_queue .push_back(std::nullopt); }

    SubmitResult sbatch(const SubmitSpec& s, ISshClient&) override {
        sbatch_calls.push_back({s});
        if (sbatch_queue.empty()) return SubmitResult{};
        auto r = sbatch_queue.front(); sbatch_queue.pop_front();
        return r;
    }

    std::optional<std::vector<JobState>>
    squeue(const std::string& cluster, ISshClient&) override {
        squeue_calls.push_back({cluster});
        if (squeue_queue.empty()) return std::vector<JobState>{};
        auto r = squeue_queue.front(); squeue_queue.pop_front();
        return r;
    }

    std::optional<std::vector<PartitionState>>
    sinfo(const std::string& cluster,
           const std::string& partition,
           ISshClient&) override {
        sinfo_calls.push_back({cluster, partition});
        if (sinfo_queue.empty()) return std::vector<PartitionState>{};
        auto r = sinfo_queue.front(); sinfo_queue.pop_front();
        return r;
    }

    // Default: scancel succeeds. Tests that want to exercise the
    // "SLURM refused cancel" path set scancel_result = false.
    bool scancel_result = true;

    bool scancel(const std::string& cluster, const std::string& jobid, ISshClient&) override {
        scancel_calls.push_back({cluster, jobid});
        return scancel_result;
    }

    void reset() {
        sbatch_calls.clear(); squeue_calls.clear();
        sinfo_calls.clear();  scancel_calls.clear();
        sbatch_queue.clear(); squeue_queue.clear(); sinfo_queue.clear();
    }
};

}  // namespace tash::cluster::testing

#endif  // TASH_CLUSTER_FAKE_SLURM_OPS_H
