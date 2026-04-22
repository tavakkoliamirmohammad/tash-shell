// Tier-2: `cluster` CLI help renders, unknown subcommand errors, and
// SLURM errors propagate all the way to "tash: cluster: …" on stderr.

#include "integration_engine_helper.h"

#include "tash/cluster/builtin_dispatch.h"

#include <sstream>

using namespace tash::cluster;
using namespace tash::cluster::testing;

TEST_F(EngineIntegrationFixture, TopLevelHelpRendersViaDispatch) {
    auto eng = engine();
    std::ostringstream out, err;
    const int rc = dispatch_cluster({"cluster", "--help"}, eng, out, err);
    EXPECT_EQ(rc, 0);
    EXPECT_NE(out.str().find("cluster — SLURM-backed"), std::string::npos) << out.str();
    EXPECT_NE(out.str().find("up"),     std::string::npos);
    EXPECT_NE(out.str().find("launch"), std::string::npos);
    EXPECT_NE(out.str().find("attach"), std::string::npos);
}

TEST_F(EngineIntegrationFixture, SbatchErrorPropagatesThroughStubs) {
    // sbatch exits non-zero; stderr has the real SLURM error message.
    set_scenario(R"BASH(
ssh_stdout_sinfo="p|idle|1|gpu:a100:1
"
ssh_exit_sinfo=0
ssh_stdout_sbatch=""
ssh_exit_sbatch=1
SSH_STDERR="sbatch: error: invalid qos specified"
)BASH");

    auto eng = engine();
    std::ostringstream out, err;
    const int rc = dispatch_cluster({"cluster", "up", "-r", "a100"}, eng, out, err);
    EXPECT_NE(rc, 0);
    EXPECT_NE(err.str().find("tash: cluster:"),          std::string::npos) << err.str();
    EXPECT_NE(err.str().find("invalid qos"),              std::string::npos);
    EXPECT_EQ(reg.allocations().size(), 0u);
}
