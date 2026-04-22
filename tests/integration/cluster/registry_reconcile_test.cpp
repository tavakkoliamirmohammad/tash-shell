// Tier-2: sync reconciles registry state against squeue.

#include "integration_engine_helper.h"

using namespace tash::cluster;
using namespace tash::cluster::testing;

TEST_F(EngineIntegrationFixture, SyncMarksGhostJobEnded) {
    // Two pre-existing running allocations on the same cluster.
    Allocation a; a.id = "c1:101"; a.cluster = "c1"; a.jobid = "101";
    a.state = AllocationState::Running;
    reg.add_allocation(a);
    Allocation b; b.id = "c1:102"; b.cluster = "c1"; b.jobid = "102";
    b.state = AllocationState::Running;
    reg.add_allocation(b);

    // squeue reports only 101; 102 has vanished.
    set_scenario(R"BASH(
ssh_stdout_squeue="101|R|n1|01:00:00
"
ssh_exit_squeue=0
)BASH");

    auto eng = engine();
    const auto r = eng.sync(SyncSpec{});
    const auto* s = std::get_if<ClusterEngine::SyncReport>(&r);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->clusters_probed, 1);
    EXPECT_EQ(s->transitions,     1);

    EXPECT_EQ(reg.find_allocation("c1:101")->state, AllocationState::Running);
    EXPECT_EQ(reg.find_allocation("c1:102")->state, AllocationState::Ended);
}
