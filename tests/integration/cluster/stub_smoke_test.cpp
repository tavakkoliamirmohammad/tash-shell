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

// Regression: `cluster sync` used to flip every Running allocation to
// Ended on a single transient squeue failure. This test drives the
// real SlurmOpsReal with the stub configured to return exit 1, then
// asserts (a) the allocation is STILL Running after sync, and (b) the
// SyncReport surfaces probe_failures + the failed cluster name.
TEST_F(IntegrationFixture, SqueueFailureLeavesRunningAllocationIntact) {
    set_scenario(R"BASH(
ssh_stdout_squeue=""
ssh_exit_squeue=1
SSH_STDERR="Host key verification failed"
)BASH");

    Config   cfg = one_route_a100();
    Registry reg;
    Allocation a;
    a.id = "c1:42"; a.cluster = "c1"; a.jobid = "42";
    a.state = AllocationState::Running; a.node = "n1"; a.resource = "a100";
    reg.add_allocation(a);

    auto ssh = make_ssh_client(
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

    const auto r = eng.sync({});
    const auto* rep = std::get_if<ClusterEngine::SyncReport>(&r);
    ASSERT_NE(rep, nullptr);

    EXPECT_EQ(rep->clusters_probed, 0);
    EXPECT_EQ(rep->probe_failures,  1);
    ASSERT_EQ(rep->failed_clusters.size(), 1u);
    EXPECT_EQ(rep->failed_clusters[0], "c1");
    EXPECT_EQ(rep->transitions,     0);

    // The allocation MUST still be Running — this is the bug that used
    // to silently corrupt the registry on ssh hiccups.
    ASSERT_EQ(reg.allocations().size(), 1u);
    EXPECT_EQ(reg.find_allocation("c1:42")->state, AllocationState::Running)
        << "sync silently flipped a Running allocation to Ended on a "
            "transient squeue failure — registry-corruption regression";

    // Stub was actually invoked.
    const auto log = read_log();
    EXPECT_NE(log.find("squeue"), std::string::npos) << log;
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
    EXPECT_EQ(reg.allocations().size(), 0u);
}

// Regression: every user-controlled sbatch field must be shell-quoted
// because ssh joins argv with spaces and the remote shell re-parses.
// An unquoted partition with whitespace used to break the submission;
// an unquoted qos/job_name with `$` or backticks could exec arbitrary
// code on the login node.
//
// This test configures a route whose partition contains a space
// ("gpu long"), then drives `cluster up` against the stub and reads
// the log to verify the sbatch argv the remote shell received quoted
// the partition as a single token (`--partition='gpu long'`).
TEST_F(IntegrationFixture, SbatchArgvQuotesPartitionWithSpace) {
    set_scenario(R"BASH(
ssh_stdout_sinfo="gpu long|idle|1|gpu:a100:1
"
ssh_exit_sinfo=0
ssh_stdout_sbatch="Submitted batch job 20042
"
ssh_exit_sbatch=0
ssh_stdout_squeue="20042|R|n1|01:00:00
"
ssh_exit_squeue=0
)BASH");

    Config c;
    c.defaults.workspace_base = "/tmp";
    c.defaults.default_preset = "claude";
    c.clusters.push_back({"c1", "stub-host", ""});
    Resource r;
    r.name = "a100"; r.kind = ResourceKind::Gpu;
    r.routes.push_back({"c1", "proj-a", /*partition=*/"gpu long",
                         "normal", "gpu:a100:1"});
    c.resources.push_back(r);
    Preset p; p.name = "claude"; p.command = "claude";
    c.presets.push_back(p);

    Registry reg;
    auto ssh = make_ssh_client(
        [&c](const std::string& n) {
            for (const auto& cl : c.clusters) if (cl.name == n) return cl.ssh_host;
            return n;
        },
        tmp_dir / "sockets");
    auto slurm = make_slurm_ops();
    auto tmux  = make_tmux_ops();
    SilentNotifier notify;
    KeepPrompt     prompt;
    RealClock      clock;
    ClusterEngine eng(c, reg, *ssh, *slurm, *tmux, notify, prompt, clock);

    UpSpec spec; spec.resource = "a100"; spec.time = "01:00:00";
    const auto res = eng.up(spec);
    ASSERT_NE(std::get_if<Allocation>(&res), nullptr)
        << "sbatch submission with a whitespace partition should succeed "
            "after the quoting fix; it broke without quoting";

    // The stub log records the literal argv it was invoked with (pipe-
    // separated). The sbatch line must contain --partition='gpu long'
    // as one argv element (not split across two).
    const auto log = read_log();
    EXPECT_NE(log.find("--partition='gpu long'"), std::string::npos)
        << "sbatch argv was not shell-quoted — the ssh re-parse on the "
            "remote shell will have split the partition into two tokens.\n"
            "log:\n" << log;
    // And the unquoted form MUST NOT appear (that would mean the
    // remote received an unquoted string).
    EXPECT_EQ(log.find("--partition=gpu|long"), std::string::npos)
        << "argv contained a split-in-two '--partition=gpu' '--long' — "
            "quoting regression.\nlog:\n" << log;
}
