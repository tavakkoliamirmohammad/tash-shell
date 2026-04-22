// Tests for ClusterCompletionProvider. Dynamic completions pull
// from the engine installed via set_active_engine(); tests install a
// test-constructed engine for each scenario and clear it on teardown.

#include <gtest/gtest.h>

#include "tash/plugins/cluster_completion_provider.h"
#include "tash/cluster/builtin_dispatch.h"
#include "tash/cluster/cluster_engine.h"
#include "tash/cluster/config.h"
#include "tash/shell.h"

#include "fakes/fake_ssh_client.h"
#include "fakes/fake_slurm_ops.h"
#include "fakes/fake_tmux_ops.h"
#include "fakes/fake_notifier.h"
#include "fakes/fake_prompt.h"
#include "fakes/fake_clock.h"

using namespace tash::cluster;
using namespace tash::cluster::testing;

namespace {

Config sample_config() {
    Config c;
    c.defaults.workspace_base = "/tmp";
    c.defaults.default_preset = "claude";
    c.clusters.push_back({"utah-n", "ssh-utah-n", ""});
    c.clusters.push_back({"utah-k", "ssh-utah-k", ""});

    Resource a100; a100.name = "a100"; a100.kind = ResourceKind::Gpu;
    a100.routes.push_back({"utah-n", "acc", "p", "q", "gpu:a100:1"});
    c.resources.push_back(a100);

    Resource h100; h100.name = "h100"; h100.kind = ResourceKind::Gpu;
    h100.routes.push_back({"utah-n", "acc", "p", "q", "gpu:h100:1"});
    c.resources.push_back(h100);

    Preset p; p.name = "claude"; p.command = "claude";
    c.presets.push_back(p);
    Preset trainer; trainer.name = "trainer"; trainer.command = "python train.py";
    c.presets.push_back(trainer);
    return c;
}

struct EngineHarness {
    Config         cfg{sample_config()};
    Registry       reg;
    FakeSshClient  ssh;
    FakeSlurmOps   slurm;
    FakeTmuxOps    tmux;
    FakeNotifier   notify;
    FakePrompt     prompt;
    FakeClock      clock;
    ClusterEngine  engine;

    EngineHarness()
        : engine(cfg, reg, ssh, slurm, tmux, notify, prompt, clock) {
        set_active_engine(&engine);
    }
    ~EngineHarness() { set_active_engine(nullptr); }
};

std::vector<std::string> texts(const std::vector<Completion>& v) {
    std::vector<std::string> out; out.reserve(v.size());
    for (const auto& c : v) out.push_back(c.text);
    return out;
}

bool contains(const std::vector<std::string>& v, const std::string& s) {
    for (const auto& x : v) if (x == s) return true;
    return false;
}

ShellState dummy_state{};

}  // namespace

// ══════════════════════════════════════════════════════════════════════════════
// Provider basics
// ══════════════════════════════════════════════════════════════════════════════

TEST(ClusterCompletion, CanCompleteOnlyCluster) {
    ClusterCompletionProvider p;
    EXPECT_TRUE (p.can_complete("cluster"));
    EXPECT_FALSE(p.can_complete("cd"));
    EXPECT_FALSE(p.can_complete("ls"));
}

TEST(ClusterCompletion, NameIsClusterCompletion) {
    ClusterCompletionProvider p;
    EXPECT_EQ(p.name(), "cluster-completion");
}

// ══════════════════════════════════════════════════════════════════════════════
// Subcommand completion
// ══════════════════════════════════════════════════════════════════════════════

TEST(ClusterCompletion, EmptyCurrentWordListsAllSubcommands) {
    EngineHarness h;
    ClusterCompletionProvider p;
    const auto c = p.complete("cluster", "", {}, dummy_state);
    const auto names = texts(c);
    // Every currently-shipping subcommand must be offered.
    EXPECT_TRUE(contains(names, "connect"));
    EXPECT_TRUE(contains(names, "disconnect"));
    EXPECT_TRUE(contains(names, "up"));
    EXPECT_TRUE(contains(names, "launch"));
    EXPECT_TRUE(contains(names, "attach"));
    EXPECT_TRUE(contains(names, "list"));
    EXPECT_TRUE(contains(names, "down"));
    EXPECT_TRUE(contains(names, "kill"));
    EXPECT_TRUE(contains(names, "sync"));
    EXPECT_TRUE(contains(names, "prune"));
    EXPECT_TRUE(contains(names, "doctor"));
    EXPECT_TRUE(contains(names, "help"));

    // Regression: `logs`, `probe`, `import` were removed in commit
    // 148c720 — ensure completion doesn't silently resurface them.
    EXPECT_FALSE(contains(names, "logs"));
    EXPECT_FALSE(contains(names, "probe"));
    EXPECT_FALSE(contains(names, "import"));
}

// Regression companion: the startup wiring must actually register the
// ClusterCompletionProvider. Before this PR the provider compiled but
// was never added to the registry — tab completion for `cluster` fell
// through to generic providers. Assert (a) it can_complete("cluster"),
// and (b) completing after `cluster connect` offers cluster names.
TEST(ClusterCompletion, ConnectPositionalOffersClusterNames) {
    EngineHarness h;
    ClusterCompletionProvider p;
    const auto names = texts(p.complete(
        "cluster", "", {"connect"}, dummy_state));
    EXPECT_TRUE(contains(names, "utah-n"));
    EXPECT_TRUE(contains(names, "utah-k"));
}

TEST(ClusterCompletion, DisconnectPositionalOffersClusterNames) {
    EngineHarness h;
    ClusterCompletionProvider p;
    const auto names = texts(p.complete(
        "cluster", "", {"disconnect"}, dummy_state));
    EXPECT_TRUE(contains(names, "utah-n"));
    EXPECT_TRUE(contains(names, "utah-k"));
}

TEST(ClusterCompletion, PrefixFiltersSubcommands) {
    EngineHarness h;
    ClusterCompletionProvider p;
    const auto names = texts(p.complete("cluster", "at", {}, dummy_state));
    EXPECT_TRUE(contains(names, "attach"));
    EXPECT_FALSE(contains(names, "up"));
}

TEST(ClusterCompletion, NoEngineStillYieldsStaticSubcommands) {
    set_active_engine(nullptr);
    ClusterCompletionProvider p;
    const auto names = texts(p.complete("cluster", "", {}, dummy_state));
    EXPECT_TRUE(contains(names, "up"));
    EXPECT_TRUE(contains(names, "list"));
}

// ══════════════════════════════════════════════════════════════════════════════
// Dynamic slots: resources / clusters / presets
// ══════════════════════════════════════════════════════════════════════════════

TEST(ClusterCompletion, AfterUpDashRListsResources) {
    EngineHarness h;
    ClusterCompletionProvider p;
    const auto names = texts(p.complete(
        "cluster", "", {"up", "-r"}, dummy_state));
    EXPECT_TRUE(contains(names, "a100"));
    EXPECT_TRUE(contains(names, "h100"));
}

TEST(ClusterCompletion, AfterDashViaListsClusters) {
    EngineHarness h;
    ClusterCompletionProvider p;
    const auto names = texts(p.complete(
        "cluster", "", {"up", "-r", "a100", "--via"}, dummy_state));
    EXPECT_TRUE(contains(names, "utah-n"));
    EXPECT_TRUE(contains(names, "utah-k"));
}

TEST(ClusterCompletion, AfterLaunchPresetListsPresets) {
    EngineHarness h;
    ClusterCompletionProvider p;
    const auto names = texts(p.complete(
        "cluster", "", {"launch", "--workspace", "wsA", "--preset"},
        dummy_state));
    EXPECT_TRUE(contains(names, "claude"));
    EXPECT_TRUE(contains(names, "trainer"));
}

// ══════════════════════════════════════════════════════════════════════════════
// Registry-driven slots: workspace/instance + allocation-id
// ══════════════════════════════════════════════════════════════════════════════

TEST(ClusterCompletion, AttachPositionalListsWorkspaceInstances) {
    EngineHarness h;
    Allocation a; a.id = "utah-n:9"; a.cluster = "utah-n"; a.jobid = "9";
    a.state = AllocationState::Running; a.node = "n1";
    Workspace ws; ws.name = "repoA";
    Instance i1; i1.id = "1"; i1.tmux_window = "1"; ws.instances.push_back(i1);
    Instance i2; i2.id = "2"; i2.tmux_window = "feature-x"; i2.name = "feature-x";
    ws.instances.push_back(i2);
    a.workspaces.push_back(ws);
    h.reg.add_allocation(a);

    ClusterCompletionProvider p;
    const auto names = texts(p.complete(
        "cluster", "", {"attach"}, dummy_state));
    EXPECT_TRUE(contains(names, "repoA/1"));
    EXPECT_TRUE(contains(names, "repoA/feature-x"));
}

TEST(ClusterCompletion, DownPositionalListsAllocationIds) {
    EngineHarness h;
    Allocation a; a.id = "utah-n:9"; a.cluster = "utah-n"; a.jobid = "9";
    a.state = AllocationState::Running;
    h.reg.add_allocation(a);

    ClusterCompletionProvider p;
    const auto names = texts(p.complete("cluster", "", {"down"}, dummy_state));
    EXPECT_TRUE(contains(names, "utah-n:9"));
}

TEST(ClusterCompletion, LaunchWorkspaceSuggestsExistingWorkspaces) {
    EngineHarness h;
    Allocation a; a.id = "utah-n:1"; a.cluster = "utah-n"; a.jobid = "1";
    a.state = AllocationState::Running;
    Workspace ws1; ws1.name = "repoA"; a.workspaces.push_back(ws1);
    Workspace ws2; ws2.name = "repoB"; a.workspaces.push_back(ws2);
    h.reg.add_allocation(a);

    ClusterCompletionProvider p;
    const auto names = texts(p.complete(
        "cluster", "", {"launch", "--workspace"}, dummy_state));
    EXPECT_TRUE(contains(names, "repoA"));
    EXPECT_TRUE(contains(names, "repoB"));
}

// ══════════════════════════════════════════════════════════════════════════════
// Flag completion (within subcommand, not after --flag)
// ══════════════════════════════════════════════════════════════════════════════

TEST(ClusterCompletion, AfterUpNameListsFlags) {
    EngineHarness h;
    ClusterCompletionProvider p;
    const auto names = texts(p.complete("cluster", "--", {"up"}, dummy_state));
    EXPECT_TRUE(contains(names, "--resource"));
    EXPECT_TRUE(contains(names, "--time"));
    EXPECT_TRUE(contains(names, "--via"));
}
