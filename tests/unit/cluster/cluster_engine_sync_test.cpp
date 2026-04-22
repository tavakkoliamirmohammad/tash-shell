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

// Regression: a transient squeue failure (ssh down, Duo expired) used to
// return an empty vector, which Registry::reconcile interpreted as "no
// jobs present" → flipped every Running allocation on that cluster to
// Ended. `squeue` now returns optional<vector>; sync() must count the
// failure and LEAVE the allocations untouched.
TEST(ClusterEngineSync, ProbeFailureDoesNotFlipRunningAllocationsToEnded) {
    Harness h;
    h.reg.add_allocation(alloc("c1", "100"));
    h.reg.add_allocation(alloc("c1", "200"));
    h.slurm.queue_squeue_fail();   // ssh went down

    auto r = h.engine().sync({});
    auto* s = std::get_if<ClusterEngine::SyncReport>(&r);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->clusters_probed, 0);
    EXPECT_EQ(s->probe_failures, 1);
    ASSERT_EQ(s->failed_clusters.size(), 1u);
    EXPECT_EQ(s->failed_clusters[0], "c1");
    EXPECT_EQ(s->transitions,    0);

    // Both allocations MUST still be Running — this is the bug.
    EXPECT_EQ(h.reg.find_allocation("c1:100")->state, AllocationState::Running);
    EXPECT_EQ(h.reg.find_allocation("c1:200")->state, AllocationState::Running);
}

// When reconcile flips an allocation to Ended, its Running / Idle
// instances must cascade to Exited. Without this, `cluster list` shows
// `running` instance rows nested under an `ended` allocation — a
// logically impossible snapshot that reliably confuses users.
TEST(ClusterEngineSync, ReconcileCascadesInstanceStateToExited) {
    Harness h;
    Allocation a = alloc("c1", "500");
    // One workspace with two instances in different states. Stopped
    // carries observed signal (stop-hook event) → must be preserved.
    Workspace w;
    w.name = "ws"; w.tmux_session = "s";
    Instance i1; i1.id = "1"; i1.tmux_window = "1"; i1.state = InstanceState::Running;
    Instance i2; i2.id = "2"; i2.tmux_window = "2"; i2.state = InstanceState::Idle;
    Instance i3; i3.id = "3"; i3.tmux_window = "3"; i3.state = InstanceState::Stopped;
    w.instances = {i1, i2, i3};
    a.workspaces.push_back(w);
    h.reg.add_allocation(a);

    // squeue says nothing — the job is gone.
    h.slurm.queue_squeue({});

    auto r = h.engine().sync({});
    auto* s = std::get_if<ClusterEngine::SyncReport>(&r);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->transitions, 1);

    const auto* aa = h.reg.find_allocation("c1:500");
    ASSERT_NE(aa, nullptr);
    EXPECT_EQ(aa->state, AllocationState::Ended);
    ASSERT_EQ(aa->workspaces.size(), 1u);
    const auto& ins = aa->workspaces[0].instances;
    ASSERT_EQ(ins.size(), 3u);

    // Running + Idle cascade to Exited.
    EXPECT_EQ(ins[0].state, InstanceState::Exited);
    EXPECT_EQ(ins[1].state, InstanceState::Exited);
    // Stopped carries real signal — must be preserved untouched.
    EXPECT_EQ(ins[2].state, InstanceState::Stopped);
}

// Partial failure across two clusters: c1 probe succeeds and transitions
// ghosts normally, c2 probe fails and leaves its allocations alone.
TEST(ClusterEngineSync, PartialProbeFailureReconcilesOnlyReachableClusters) {
    Harness h;
    h.reg.add_allocation(alloc("c1", "100"));   // will be seen in squeue
    h.reg.add_allocation(alloc("c1", "200"));   // ghost → Ended
    h.reg.add_allocation(alloc("c2", "300"));   // c2 unreachable → unchanged

    h.slurm.queue_squeue({JobState{"100", "R", "n1", ""}});    // c1 ok
    h.slurm.queue_squeue_fail();                                // c2 failed

    auto r = h.engine().sync({});
    auto* s = std::get_if<ClusterEngine::SyncReport>(&r);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->clusters_probed, 1);
    EXPECT_EQ(s->probe_failures, 1);
    EXPECT_EQ(s->transitions,    1);
    EXPECT_EQ(h.reg.find_allocation("c1:100")->state, AllocationState::Running);
    EXPECT_EQ(h.reg.find_allocation("c1:200")->state, AllocationState::Ended);
    EXPECT_EQ(h.reg.find_allocation("c2:300")->state, AllocationState::Running);
}
