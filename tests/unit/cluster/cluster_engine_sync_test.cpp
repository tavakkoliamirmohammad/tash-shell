// ClusterEngine::sync — reconcile registry against live squeue snapshots.

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
Allocation alloc(std::string c, std::string j) {
    Allocation a; a.id = c + ":" + j; a.cluster = c; a.jobid = j;
    a.state = AllocationState::Running; return a;
}
}  // namespace

TEST(ClusterEngineSync, EmptyRegistryYieldsZeroTransitions) {
    Harness h;
    auto r = h.engine().sync({});
    auto* s = std::get_if<ClusterEngine::SyncReport>(&r);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->clusters_probed, 0);
    EXPECT_EQ(s->transitions,     0);
}

TEST(ClusterEngineSync, AllJobsPresentNoTransitions) {
    Harness h;
    h.reg.add_allocation(alloc("c1", "100"));
    h.reg.add_allocation(alloc("c1", "200"));
    h.slurm.queue_squeue({
        JobState{"100", "R", "n1", "02:00:00"},
        JobState{"200", "R", "n2", "02:00:00"}
    });

    auto r = h.engine().sync({});
    auto* s = std::get_if<ClusterEngine::SyncReport>(&r);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->clusters_probed, 1);
    EXPECT_EQ(s->transitions,     0);
}

TEST(ClusterEngineSync, GhostJobsTransitionToEnded) {
    Harness h;
    h.reg.add_allocation(alloc("c1", "100"));
    h.reg.add_allocation(alloc("c1", "200"));
    // Only 100 still in squeue.
    h.slurm.queue_squeue({JobState{"100", "R", "n1", "02:00:00"}});

    auto r = h.engine().sync({});
    auto* s = std::get_if<ClusterEngine::SyncReport>(&r);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->clusters_probed, 1);
    EXPECT_EQ(s->transitions,     1);
    EXPECT_EQ(h.reg.find_allocation("c1:100")->state, AllocationState::Running);
    EXPECT_EQ(h.reg.find_allocation("c1:200")->state, AllocationState::Ended);
}

TEST(ClusterEngineSync, ProbesOneSqueuePerCluster) {
    Harness h;
    h.reg.add_allocation(alloc("c1", "100"));
    h.reg.add_allocation(alloc("c1", "200"));   // same cluster as above
    h.reg.add_allocation(alloc("c2", "300"));

    h.slurm.queue_squeue({JobState{"100", "R", "", ""}, JobState{"200", "R", "", ""}});
    h.slurm.queue_squeue({JobState{"300", "R", "", ""}});

    auto r = h.engine().sync({});
    auto* s = std::get_if<ClusterEngine::SyncReport>(&r);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->clusters_probed, 2);
    EXPECT_EQ(h.slurm.squeue_calls.size(), 2u);
}

TEST(ClusterEngineSync, FilterToOneClusterSkipsOthers) {
    Harness h;
    h.reg.add_allocation(alloc("c1", "100"));
    h.reg.add_allocation(alloc("c2", "300"));
    h.slurm.queue_squeue({JobState{"100", "R", "", ""}});

    SyncSpec ss; ss.cluster = "c1";
    auto r = h.engine().sync(ss);
    auto* s = std::get_if<ClusterEngine::SyncReport>(&r);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->clusters_probed, 1);
    ASSERT_EQ(h.slurm.squeue_calls.size(), 1u);
    EXPECT_EQ(h.slurm.squeue_calls[0].cluster, "c1");
    // c2's state untouched
    EXPECT_EQ(h.reg.find_allocation("c2:300")->state, AllocationState::Running);
}
