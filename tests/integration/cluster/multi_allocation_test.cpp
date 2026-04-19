// Tier-2: two distinct allocations — verify sbatch is invoked twice
// with distinct jobids and both end up in the registry.

#include "integration_engine_helper.h"

using namespace tash::cluster;
using namespace tash::cluster::testing;

TEST_F(EngineIntegrationFixture, TwoAllocationsCoexist) {
    set_scenario(R"BASH(
ssh_stdout_sinfo="p|idle|2|gpu:a100:1
"
ssh_exit_sinfo=0
ssh_exit_sbatch=0
ssh_exit_squeue=0
)BASH");

    auto eng = engine();

    // First up: sbatch returns 6001, squeue reports R.
    ::setenv("TASH_FAKE_SCENARIO", scenario_path.c_str(), 1);
    set_scenario(R"BASH(
ssh_stdout_sinfo="p|idle|2|gpu:a100:1
"
ssh_exit_sinfo=0
ssh_stdout_sbatch="Submitted batch job 6001
"
ssh_exit_sbatch=0
ssh_stdout_squeue="6001|R|na|04:00:00
"
ssh_exit_squeue=0
)BASH");
    UpSpec s1; s1.resource = "a100";
    const auto r1 = eng.up(s1);
    ASSERT_NE(std::get_if<Allocation>(&r1), nullptr)
        << std::get<EngineError>(r1).message;

    // Second up: different jobid.
    set_scenario(R"BASH(
ssh_stdout_sinfo="p|idle|2|gpu:a100:1
"
ssh_exit_sinfo=0
ssh_stdout_sbatch="Submitted batch job 6002
"
ssh_exit_sbatch=0
ssh_stdout_squeue="6002|R|nb|04:00:00
"
ssh_exit_squeue=0
)BASH");
    UpSpec s2; s2.resource = "a100";
    const auto r2 = eng.up(s2);
    ASSERT_NE(std::get_if<Allocation>(&r2), nullptr)
        << std::get<EngineError>(r2).message;

    ASSERT_EQ(reg.allocations.size(), 2u);
    EXPECT_EQ(reg.allocations[0].jobid, "6001");
    EXPECT_EQ(reg.allocations[1].jobid, "6002");
}
