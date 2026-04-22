// Tier-2: launch an instance through real SshClient / TmuxOps with
// stub tmux + ssh. (Attach is exec-style so we don't invoke it from
// tests; the argv shape is covered in tmux_ops_test.cpp.)

#include "integration_engine_helper.h"

using namespace tash::cluster;
using namespace tash::cluster::testing;

TEST_F(EngineIntegrationFixture, LaunchCreatesSessionAndWindowThroughStubs) {
    // Preload an allocation so launch doesn't have to submit.
    Allocation a;
    a.id = "c1:4001"; a.cluster = "c1"; a.jobid = "4001"; a.node = "n1";
    a.state = AllocationState::Running;
    reg.add_allocation(a);

    set_scenario(R"BASH(
# tmux/ssh calls all succeed silently.
ssh_stdout_tmux=""
ssh_exit_tmux=0
SSH_EXIT=0
)BASH");

    auto eng = engine();
    LaunchSpec ls; ls.workspace = "repoA"; ls.preset = "claude";
    const auto r = eng.launch(ls);
    ASSERT_NE(std::get_if<Instance>(&r), nullptr) << std::get<EngineError>(r).message;

    const auto log = read_log();
    // Workspace creation → tmux new-session, then launch → tmux new-window,
    // then is_window_alive → tmux list-windows. Each arrives as one [ssh]
    // line with "tmux" in the remote-command argv.
    EXPECT_NE(log.find("tmux new-session"), std::string::npos) << log;
    EXPECT_NE(log.find("tmux new-window"),  std::string::npos);
    EXPECT_NE(log.find("tmux list-windows"),std::string::npos);

    auto* ap = reg.find_allocation("c1:4001");
    ASSERT_NE(ap, nullptr);
    ASSERT_EQ(ap->workspaces.size(), 1u);
    EXPECT_EQ(ap->workspaces[0].instances.size(), 1u);
}
