// Tests for the argv dispatcher that drives the `cluster` builtin.
//
// Constructs a ClusterEngine against fakes, invokes dispatch_cluster
// with handcrafted argv vectors, and asserts on exit code + captured
// stdout/stderr + fake side effects.

#include <gtest/gtest.h>

#include "tash/cluster/builtin_dispatch.h"
#include "tash/cluster/cluster_engine.h"
#include "tash/cluster/config.h"

#include "fakes/fake_ssh_client.h"
#include "fakes/fake_slurm_ops.h"
#include "fakes/fake_tmux_ops.h"
#include "fakes/fake_notifier.h"
#include "fakes/fake_prompt.h"
#include "fakes/fake_clock.h"

#include <sstream>
#include <string>
#include <vector>

using namespace tash::cluster;
using namespace tash::cluster::testing;

namespace {

Config one_route_a100() {
    Config c;
    c.defaults.workspace_base = "/scratch";
    c.defaults.default_preset = "claude";
    c.clusters.push_back({"c1", "ssh-c1", ""});
    Resource r;
    r.name = "a100"; r.kind = ResourceKind::Gpu;
    r.routes.push_back({"c1", "acc", "p", "q", "gpu:a100:1"});
    c.resources.push_back(r);
    Preset p; p.name = "claude"; p.command = "claude";
    c.presets.push_back(p);
    return c;
}

struct Harness {
    Config cfg;
    Registry reg;
    FakeSshClient ssh; FakeSlurmOps slurm; FakeTmuxOps tmux;
    FakeNotifier notify; FakePrompt prompt; FakeClock clock;

    Harness() : cfg(one_route_a100()) {}
    ClusterEngine engine() {
        return ClusterEngine(cfg, reg, ssh, slurm, tmux, notify, prompt, clock);
    }
};

std::vector<std::string> argv_of(std::initializer_list<const char*> xs) {
    return { xs.begin(), xs.end() };
}

}  // namespace

// ══════════════════════════════════════════════════════════════════════════════
// up
// ══════════════════════════════════════════════════════════════════════════════

TEST(ClusterBuiltin, UpParsesResourceAndDispatches) {
    Harness h;
    h.slurm.queue_sinfo({PartitionState{"p", "up", 1, {"gpu:a100:1"}}});
    h.slurm.queue_sbatch({"1001", ""});
    h.slurm.queue_squeue({JobState{"1001", "R", "n1", "04:00:00"}});

    auto eng = h.engine();
    std::ostringstream out, err;
    int rc = dispatch_cluster(argv_of({"cluster", "up", "-r", "a100", "-t", "04:00:00"}),
                                eng, out, err);
    EXPECT_EQ(rc, 0) << err.str();
    EXPECT_NE(out.str().find("1001"), std::string::npos) << out.str();
    EXPECT_NE(out.str().find("n1"),   std::string::npos) << out.str();
    ASSERT_EQ(h.slurm.sbatch_calls.size(), 1u);
    EXPECT_EQ(h.slurm.sbatch_calls[0].spec.time, "04:00:00");
}

TEST(ClusterBuiltin, UpMissingResourceErrors) {
    Harness h;
    auto eng = h.engine();
    std::ostringstream out, err;
    int rc = dispatch_cluster(argv_of({"cluster", "up"}), eng, out, err);
    EXPECT_NE(rc, 0);
    EXPECT_NE(err.str().find("cluster"), std::string::npos);
    EXPECT_NE(err.str().find("-r"),       std::string::npos) << err.str();
}

TEST(ClusterBuiltin, UpSurfaceEngineError) {
    Harness h;
    h.slurm.queue_sinfo({PartitionState{"p", "up", 1, {"gpu:a100:1"}}});
    h.slurm.queue_sbatch(SubmitResult{/*jobid*/"", "sbatch: account denied"});

    auto eng = h.engine();
    std::ostringstream out, err;
    int rc = dispatch_cluster(argv_of({"cluster", "up", "-r", "a100"}), eng, out, err);
    EXPECT_NE(rc, 0);
    EXPECT_NE(err.str().find("tash: cluster:"), std::string::npos) << err.str();
    EXPECT_NE(err.str().find("account denied"), std::string::npos) << err.str();
}

// ══════════════════════════════════════════════════════════════════════════════
// list
// ══════════════════════════════════════════════════════════════════════════════

TEST(ClusterBuiltin, ListEmptyPrintsHeaderOrEmpty) {
    Harness h;
    auto eng = h.engine();
    std::ostringstream out, err;
    int rc = dispatch_cluster(argv_of({"cluster", "list"}), eng, out, err);
    EXPECT_EQ(rc, 0);
    // Output is either "(no allocations)" or some explicit empty hint.
    EXPECT_NE(out.str().find("no allocations"), std::string::npos) << out.str();
}

TEST(ClusterBuiltin, ListPrintsAllocations) {
    Harness h;
    Allocation a;
    a.id = "c1:100"; a.cluster = "c1"; a.jobid = "100"; a.node = "n1";
    a.resource = "a100"; a.state = AllocationState::Running;
    Workspace ws; ws.name = "repoA";
    Instance inst; inst.id = "1"; inst.tmux_window = "1"; inst.state = InstanceState::Running;
    ws.instances.push_back(inst);
    a.workspaces.push_back(ws);
    h.reg.add_allocation(a);

    auto eng = h.engine();
    std::ostringstream out, err;
    int rc = dispatch_cluster(argv_of({"cluster", "list"}), eng, out, err);
    EXPECT_EQ(rc, 0);
    const std::string s = out.str();
    EXPECT_NE(s.find("c1:100"), std::string::npos) << s;
    EXPECT_NE(s.find("a100"),   std::string::npos) << s;
    EXPECT_NE(s.find("repoA"),  std::string::npos) << s;
}

// ══════════════════════════════════════════════════════════════════════════════
// attach
// ══════════════════════════════════════════════════════════════════════════════

TEST(ClusterBuiltin, AttachPassesWorkspaceInstance) {
    Harness h;
    Allocation a;
    a.id = "c1:100"; a.cluster = "c1"; a.jobid = "100"; a.node = "n1";
    a.state = AllocationState::Running;
    Workspace ws; ws.name = "repoA"; ws.tmux_session = "tash-c1-100-repoA";
    Instance inst; inst.id = "1"; inst.tmux_window = "1";
    ws.instances.push_back(inst);
    a.workspaces.push_back(ws);
    h.reg.add_allocation(a);

    auto eng = h.engine();
    std::ostringstream out, err;
    int rc = dispatch_cluster(argv_of({"cluster", "attach", "repoA/1"}), eng, out, err);
    EXPECT_EQ(rc, 0) << err.str();
    ASSERT_EQ(h.tmux.exec_attach_calls.size(), 1u);
    EXPECT_EQ(h.tmux.exec_attach_calls[0].window, "1");
    EXPECT_EQ(h.tmux.exec_attach_calls[0].session, "tash-c1-100-repoA");
}

TEST(ClusterBuiltin, AttachMissingPositionalErrors) {
    Harness h;
    auto eng = h.engine();
    std::ostringstream out, err;
    int rc = dispatch_cluster(argv_of({"cluster", "attach"}), eng, out, err);
    EXPECT_NE(rc, 0);
    EXPECT_NE(err.str().find("workspace/instance"), std::string::npos) << err.str();
}

// ══════════════════════════════════════════════════════════════════════════════
// help / unknown / --help
// ══════════════════════════════════════════════════════════════════════════════

TEST(ClusterBuiltin, TopLevelHelpListsSubcommands) {
    Harness h;
    auto eng = h.engine();
    std::ostringstream out, err;
    int rc = dispatch_cluster(argv_of({"cluster", "--help"}), eng, out, err);
    EXPECT_EQ(rc, 0);
    const std::string s = out.str();
    EXPECT_NE(s.find("up"),     std::string::npos) << s;
    EXPECT_NE(s.find("launch"), std::string::npos);
    EXPECT_NE(s.find("attach"), std::string::npos);
    EXPECT_NE(s.find("list"),   std::string::npos);
    EXPECT_NE(s.find("down"),   std::string::npos);
}

TEST(ClusterBuiltin, SubcommandHelpIsRecognised) {
    Harness h;
    auto eng = h.engine();
    std::ostringstream out, err;
    int rc = dispatch_cluster(argv_of({"cluster", "up", "--help"}), eng, out, err);
    EXPECT_EQ(rc, 0);
    EXPECT_NE(out.str().find("cluster up"), std::string::npos) << out.str();
    EXPECT_NE(out.str().find("-r"),         std::string::npos);
}

TEST(ClusterBuiltin, UnknownSubcommandErrors) {
    Harness h;
    auto eng = h.engine();
    std::ostringstream out, err;
    int rc = dispatch_cluster(argv_of({"cluster", "reticulate"}), eng, out, err);
    EXPECT_NE(rc, 0);
    EXPECT_NE(err.str().find("reticulate"), std::string::npos) << err.str();
}

TEST(ClusterBuiltin, NoSubcommandPrintsHelp) {
    Harness h;
    auto eng = h.engine();
    std::ostringstream out, err;
    int rc = dispatch_cluster(argv_of({"cluster"}), eng, out, err);
    EXPECT_EQ(rc, 0);
    EXPECT_NE(out.str().find("subcommands"), std::string::npos) << out.str();
}

// ══════════════════════════════════════════════════════════════════════════════
// active_engine setter/getter
// ══════════════════════════════════════════════════════════════════════════════

TEST(ClusterBuiltin, ActiveEngineRoundTrips) {
    set_active_engine(nullptr);
    EXPECT_EQ(active_engine(), nullptr);

    Harness h;
    auto eng = h.engine();
    set_active_engine(&eng);
    EXPECT_EQ(active_engine(), &eng);
    set_active_engine(nullptr);
    EXPECT_EQ(active_engine(), nullptr);
}

// ══════════════════════════════════════════════════════════════════════════════
// Remaining subcommand dispatchers (coverage of every cmd_* branch)
// ══════════════════════════════════════════════════════════════════════════════

TEST(ClusterBuiltin, LaunchDispatches) {
    Harness h;
    Allocation a; a.id = "c1:100"; a.cluster = "c1"; a.jobid = "100"; a.node = "n1";
    a.state = AllocationState::Running;
    h.reg.add_allocation(a);

    auto eng = h.engine();
    std::ostringstream out, err;
    int rc = dispatch_cluster(argv_of({"cluster", "launch", "--workspace", "w",
                                          "--cmd", "bash"}),
                                eng, out, err);
    EXPECT_EQ(rc, 0) << err.str();
    EXPECT_NE(out.str().find("launched"), std::string::npos) << out.str();
    ASSERT_EQ(h.tmux.new_session_calls.size(), 1u);
    ASSERT_EQ(h.tmux.new_window_calls.size(), 1u);
    EXPECT_EQ(h.tmux.new_window_calls[0].cmd, "bash");
}

TEST(ClusterBuiltin, LaunchMissingWorkspaceErrors) {
    Harness h;
    auto eng = h.engine();
    std::ostringstream out, err;
    int rc = dispatch_cluster(argv_of({"cluster", "launch", "--cmd", "bash"}),
                                eng, out, err);
    EXPECT_NE(rc, 0);
    EXPECT_NE(err.str().find("workspace"), std::string::npos) << err.str();
}

TEST(ClusterBuiltin, DownDispatches) {
    Harness h;
    Allocation a; a.id = "c1:100"; a.cluster = "c1"; a.jobid = "100";
    a.state = AllocationState::Running;
    h.reg.add_allocation(a);

    auto eng = h.engine();
    std::ostringstream out, err;
    int rc = dispatch_cluster(argv_of({"cluster", "down", "c1:100"}), eng, out, err);
    EXPECT_EQ(rc, 0) << err.str();
    EXPECT_NE(out.str().find("cancelled"), std::string::npos);
    EXPECT_EQ(h.slurm.scancel_calls.size(), 1u);
}

TEST(ClusterBuiltin, DownMissingIdErrors) {
    Harness h;
    auto eng = h.engine();
    std::ostringstream out, err;
    int rc = dispatch_cluster(argv_of({"cluster", "down"}), eng, out, err);
    EXPECT_NE(rc, 0);
    EXPECT_NE(err.str().find("allocation-id"), std::string::npos);
}

TEST(ClusterBuiltin, KillDispatches) {
    Harness h;
    Allocation a; a.id = "c1:100"; a.cluster = "c1"; a.jobid = "100"; a.node = "n1";
    a.state = AllocationState::Running;
    Workspace ws; ws.name = "w"; ws.tmux_session = "tash-c1-100-w";
    Instance inst; inst.id = "1"; inst.tmux_window = "1";
    ws.instances.push_back(inst);
    a.workspaces.push_back(ws);
    h.reg.add_allocation(a);

    auto eng = h.engine();
    std::ostringstream out, err;
    int rc = dispatch_cluster(argv_of({"cluster", "kill", "w/1"}), eng, out, err);
    EXPECT_EQ(rc, 0) << err.str();
    EXPECT_NE(out.str().find("killed"), std::string::npos);
    EXPECT_EQ(h.tmux.kill_window_calls.size(), 1u);
}

TEST(ClusterBuiltin, KillMissingPositionalErrors) {
    Harness h;
    auto eng = h.engine();
    std::ostringstream out, err;
    int rc = dispatch_cluster(argv_of({"cluster", "kill"}), eng, out, err);
    EXPECT_NE(rc, 0);
}

TEST(ClusterBuiltin, SyncDispatches) {
    Harness h;
    Allocation a; a.id = "c1:100"; a.cluster = "c1"; a.jobid = "100";
    a.state = AllocationState::Running;
    h.reg.add_allocation(a);
    h.slurm.queue_squeue({JobState{"100", "R", "n1", "02:00:00"}});

    auto eng = h.engine();
    std::ostringstream out, err;
    int rc = dispatch_cluster(argv_of({"cluster", "sync"}), eng, out, err);
    EXPECT_EQ(rc, 0) << err.str();
    EXPECT_NE(out.str().find("probed"), std::string::npos);
}

TEST(ClusterBuiltin, ProbeDispatches) {
    Harness h;
    h.slurm.queue_sinfo({PartitionState{"p", "up", 1, {"gpu:a100:1"}}});

    auto eng = h.engine();
    std::ostringstream out, err;
    int rc = dispatch_cluster(argv_of({"cluster", "probe", "-r", "a100"}), eng, out, err);
    EXPECT_EQ(rc, 0) << err.str();
    EXPECT_NE(out.str().find("a100"), std::string::npos);
}

TEST(ClusterBuiltin, ProbeMissingResourceErrors) {
    Harness h;
    auto eng = h.engine();
    std::ostringstream out, err;
    int rc = dispatch_cluster(argv_of({"cluster", "probe"}), eng, out, err);
    EXPECT_NE(rc, 0);
    EXPECT_NE(err.str().find("resource"), std::string::npos);
}

TEST(ClusterBuiltin, ImportDispatches) {
    Harness h;
    h.slurm.queue_squeue({JobState{"777", "R", "n7", "01:00:00"}});

    auto eng = h.engine();
    std::ostringstream out, err;
    int rc = dispatch_cluster(argv_of({"cluster", "import", "777", "--via", "c1"}),
                                eng, out, err);
    EXPECT_EQ(rc, 0) << err.str();
    EXPECT_NE(out.str().find("imported"), std::string::npos);
    EXPECT_NE(out.str().find("c1:777"),   std::string::npos);
}

TEST(ClusterBuiltin, ImportMissingViaErrors) {
    Harness h;
    auto eng = h.engine();
    std::ostringstream out, err;
    int rc = dispatch_cluster(argv_of({"cluster", "import", "777"}), eng, out, err);
    EXPECT_NE(rc, 0);
    EXPECT_NE(err.str().find("--via"), std::string::npos);
}

TEST(ClusterBuiltin, HelpSubcommandFormRenders) {
    Harness h;
    auto eng = h.engine();
    std::ostringstream out, err;
    int rc = dispatch_cluster(argv_of({"cluster", "help", "launch"}), eng, out, err);
    EXPECT_EQ(rc, 0);
    EXPECT_NE(out.str().find("cluster launch"), std::string::npos);
}
