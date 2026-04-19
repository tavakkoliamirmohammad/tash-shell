// Tier-2 smoke: drives the real ClusterEngine (via make_ssh_client,
// make_slurm_ops, make_tmux_ops) through the fake-bin stubs and
// verifies end-to-end that `cluster up` submits and reaches Running.
//
// Hits every real-code path of M2.1 / M2.2 / M2.3 — SshClient
// fork/exec/capture, SlurmOps parse_squeue / parse_sbatch_jobid,
// TmuxOps compose_remote_cmd — without touching real SSH / SLURM.

#include "integration_fixture.h"

#include "tash/cluster/cluster_engine.h"
#include "tash/cluster/config.h"
#include "tash/cluster/registry.h"
#include "tash/cluster/slurm_ops.h"
#include "tash/cluster/ssh_client.h"
#include "tash/cluster/tmux_ops.h"
#include "tash/cluster/notifier.h"

using namespace tash::cluster;
using namespace tash::cluster::testing;

namespace {

// Minimal notifier/prompt/clock that just satisfies the engine's
// dependencies for this integration test — no scripting needed.
class SilentNotifier : public INotifier {
public:
    void desktop(const std::string&, const std::string&) override {}
    void bell() override {}
};
class KeepPrompt : public IPrompt {
public:
    char choice(const std::string&, const std::string&) override { return 'k'; }
};

Config one_route_a100() {
    Config c;
    c.defaults.workspace_base = "/tmp";
    c.defaults.default_preset = "claude";
    c.clusters.push_back({"c1", "stub-host", ""});
    Resource r;
    r.name = "a100"; r.kind = ResourceKind::Gpu;
    r.routes.push_back({"c1", "acc", "p", "q", "gpu:a100:1"});
    c.resources.push_back(r);
    Preset p; p.name = "claude"; p.command = "claude";
    c.presets.push_back(p);
    return c;
}

}  // namespace

TEST_F(IntegrationFixture, UpEndToEndAgainstStubs) {
    // Scenario: sinfo reports an idle a100; sbatch accepts and returns
    // jobid 20001; the follow-up squeue shows the job running.
    set_scenario(R"BASH(
ssh_stdout_sinfo="p|idle|1|gpu:a100:1
"
ssh_exit_sinfo=0
ssh_stdout_sbatch="Submitted batch job 20001
"
ssh_exit_sbatch=0
ssh_stdout_squeue="20001|R|n1|01:00:00
"
ssh_exit_squeue=0
)BASH");

    Config   cfg = one_route_a100();
    Registry reg;

    auto ssh   = make_ssh_client(
        [&cfg](const std::string& name) {
            for (const auto& c : cfg.clusters) {
                if (c.name == name) return c.ssh_host;
            }
            return name;
        },
        tmp_dir / "sockets");
    auto slurm = make_slurm_ops();
    auto tmux  = make_tmux_ops();
    SilentNotifier notify;
    KeepPrompt     prompt;
    RealClock      clock;

    ClusterEngine eng(cfg, reg, *ssh, *slurm, *tmux, notify, prompt, clock);

    UpSpec spec; spec.resource = "a100"; spec.time = "01:00:00";
    const auto r = eng.up(spec);
    auto* alloc = std::get_if<Allocation>(&r);
    ASSERT_NE(alloc, nullptr) << std::get<EngineError>(r).message;

    EXPECT_EQ(alloc->jobid,    "20001");
    EXPECT_EQ(alloc->node,     "n1");
    EXPECT_EQ(alloc->state,    AllocationState::Running);
    EXPECT_EQ(alloc->cluster,  "c1");

    // Log should include at least one ssh invocation for each of the
    // three SLURM commands we touched.
    const auto log = read_log();
    EXPECT_NE(log.find("sinfo"),  std::string::npos) << log;
    EXPECT_NE(log.find("sbatch"), std::string::npos);
    EXPECT_NE(log.find("squeue"), std::string::npos);
}

TEST_F(IntegrationFixture, SshStubRespectsExitCode) {
    // Scenario: sbatch rejects with non-zero exit.
    set_scenario(R"BASH(
ssh_stdout_sinfo="p|idle|1|gpu:a100:1
"
ssh_exit_sinfo=0
ssh_stdout_sbatch=""
ssh_exit_sbatch=1
SSH_STDERR="sbatch: error: account denied"
)BASH");

    Config   cfg = one_route_a100();
    Registry reg;

    auto ssh   = make_ssh_client(
        [&cfg](const std::string& n) {
            for (const auto& c : cfg.clusters) if (c.name == n) return c.ssh_host;
            return n;
        },
        tmp_dir / "sockets");
    auto slurm = make_slurm_ops();
    auto tmux  = make_tmux_ops();
    SilentNotifier notify;
    KeepPrompt     prompt;
    RealClock      clock;

    ClusterEngine eng(cfg, reg, *ssh, *slurm, *tmux, notify, prompt, clock);
    UpSpec spec; spec.resource = "a100";
    const auto r = eng.up(spec);
    const auto* err = std::get_if<EngineError>(&r);
    ASSERT_NE(err, nullptr);
    EXPECT_NE(err->message.find("sbatch"), std::string::npos) << err->message;
    EXPECT_EQ(reg.allocations.size(), 0u);
}
