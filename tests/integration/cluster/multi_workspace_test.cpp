// Tier-2: two workspaces under one allocation; each gets its own tmux
// session.

#include "integration_engine_helper.h"

using namespace tash::cluster;
using namespace tash::cluster::testing;

TEST_F(EngineIntegrationFixture, TwoWorkspacesOneAllocation) {
    Allocation a;
    a.id = "c1:5001"; a.cluster = "c1"; a.jobid = "5001"; a.node = "n1";
    a.state = AllocationState::Running;
    reg.add_allocation(a);

    set_scenario(R"BASH(
ssh_stdout_tmux=""
ssh_exit_tmux=0
SSH_EXIT=0
)BASH");

    auto eng = engine();
    {
        LaunchSpec ls; ls.workspace = "repoA"; ls.preset = "claude";
        const auto r = eng.launch(ls);
        ASSERT_NE(std::get_if<Instance>(&r), nullptr);
    }
    {
        LaunchSpec ls; ls.workspace = "repoB"; ls.preset = "claude";
        const auto r = eng.launch(ls);
        ASSERT_NE(std::get_if<Instance>(&r), nullptr);
    }

    auto* ap = reg.find_allocation("c1:5001");
    ASSERT_NE(ap, nullptr);
    ASSERT_EQ(ap->workspaces.size(), 2u);
    EXPECT_EQ(ap->workspaces[0].name, "repoA");
    EXPECT_EQ(ap->workspaces[1].name, "repoB");

    // Two `tmux new-session` calls should have fired — one per workspace.
    const auto log = read_log();
    std::size_t count = 0;
    for (std::size_t pos = 0; (pos = log.find("tmux new-session", pos)) != std::string::npos; ++pos) ++count;
    EXPECT_EQ(count, 2u) << log;
}
