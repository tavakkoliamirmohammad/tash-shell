// Tier-2: full cluster up → cluster down round-trip through real
// SshClient / SlurmOps against stub binaries.

#include "integration_engine_helper.h"

using namespace tash::cluster;
using namespace tash::cluster::testing;

TEST_F(EngineIntegrationFixture, UpThenDownRoundtrip) {
    set_scenario(R"BASH(
ssh_stdout_sinfo="p|idle|1|gpu:a100:1
"
ssh_exit_sinfo=0
ssh_stdout_sbatch="Submitted batch job 30001
"
ssh_exit_sbatch=0
ssh_stdout_squeue="30001|R|n9|01:00:00
"
ssh_exit_squeue=0
ssh_stdout_scancel=""
ssh_exit_scancel=0
)BASH");

    auto eng = engine();
    UpSpec us; us.resource = "a100"; us.time = "01:00:00";
    const auto u = eng.up(us);
    const auto* a = std::get_if<Allocation>(&u);
    ASSERT_NE(a, nullptr) << std::get<EngineError>(u).message;
    EXPECT_EQ(a->jobid, "30001");

    // Down: scancel through stubs, registry purged.
    DownSpec ds; ds.alloc_id = a->id;
    const auto d = eng.down(ds);
    ASSERT_NE(std::get_if<Allocation>(&d), nullptr);

    EXPECT_EQ(reg.allocations().size(), 0u);
    const auto log = read_log();
    EXPECT_NE(log.find("scancel"), std::string::npos) << log;
    EXPECT_NE(log.find("30001"),   std::string::npos);
}
