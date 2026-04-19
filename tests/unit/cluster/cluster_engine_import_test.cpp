// ClusterEngine::import — adopt an externally-submitted SLURM job.

#include <gtest/gtest.h>

#include "tash/cluster/cluster_engine.h"
#include "fakes/fake_ssh_client.h"
#include "fakes/fake_slurm_ops.h"
#include "fakes/fake_tmux_ops.h"
#include "fakes/fake_notifier.h"
#include "fakes/fake_prompt.h"
#include "fakes/fake_clock.h"

using namespace tash::cluster;
using namespace tash::cluster::testing;

namespace {
struct Harness {
    Config cfg;
    Registry reg;
    FakeSshClient ssh; FakeSlurmOps slurm; FakeTmuxOps tmux;
    FakeNotifier notify; FakePrompt prompt; FakeClock clock;
    ClusterEngine engine() {
        return ClusterEngine(cfg, reg, ssh, slurm, tmux, notify, prompt, clock);
    }
};
}  // namespace

TEST(ClusterEngineImport, ImportsRunningJob) {
    Harness h;
    h.slurm.queue_squeue({JobState{"9999", "R", "n42", "03:00:00"}});

    ImportSpec is; is.jobid = "9999"; is.cluster = "c1"; is.resource = "a100";
    auto r = h.engine().import(is);
    auto* a = std::get_if<Allocation>(&r);
    ASSERT_NE(a, nullptr) << std::get<EngineError>(r).message;

    EXPECT_EQ(a->id,       "c1:9999");
    EXPECT_EQ(a->cluster,  "c1");
    EXPECT_EQ(a->jobid,    "9999");
    EXPECT_EQ(a->node,     "n42");
    EXPECT_EQ(a->state,    AllocationState::Running);
    EXPECT_EQ(a->resource, "a100");

    ASSERT_EQ(h.reg.allocations.size(), 1u);
    EXPECT_EQ(h.reg.allocations[0].id, "c1:9999");
}

TEST(ClusterEngineImport, ImportsPendingJob) {
    Harness h;
    h.slurm.queue_squeue({JobState{"9999", "PD", "", ""}});

    ImportSpec is; is.jobid = "9999"; is.cluster = "c1";
    auto r = h.engine().import(is);
    auto* a = std::get_if<Allocation>(&r);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->state, AllocationState::Pending);
}

TEST(ClusterEngineImport, JobIdNotInSqueueErrors) {
    Harness h;
    h.slurm.queue_squeue({JobState{"111", "R", "", ""}});

    ImportSpec is; is.jobid = "222"; is.cluster = "c1";
    auto r = h.engine().import(is);
    auto* err = std::get_if<EngineError>(&r);
    ASSERT_NE(err, nullptr);
    EXPECT_NE(err->message.find("222"), std::string::npos);
}

TEST(ClusterEngineImport, AlreadyImportedErrors) {
    Harness h;
    Allocation prior; prior.id = "c1:555"; prior.cluster = "c1"; prior.jobid = "555";
    h.reg.add_allocation(prior);

    h.slurm.queue_squeue({JobState{"555", "R", "n1", "01:00:00"}});
    ImportSpec is; is.jobid = "555"; is.cluster = "c1";
    auto r = h.engine().import(is);
    auto* err = std::get_if<EngineError>(&r);
    ASSERT_NE(err, nullptr);
    EXPECT_NE(err->message.find("already"), std::string::npos);
}

TEST(ClusterEngineImport, MissingJobIdRejected) {
    Harness h;
    ImportSpec is; is.jobid = ""; is.cluster = "c1";
    auto r = h.engine().import(is);
    auto* err = std::get_if<EngineError>(&r);
    ASSERT_NE(err, nullptr);
}
