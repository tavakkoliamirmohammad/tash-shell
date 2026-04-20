// Tests for ClusterEngine::launch — preset resolution, tmux session/window
// orchestration, liveness detection, allocation disambiguation.

#include <gtest/gtest.h>

#include "tash/cluster/cluster_engine.h"
#include "tash/cluster/config.h"

#include "fakes/fake_ssh_client.h"
#include "fakes/fake_slurm_ops.h"
#include "fakes/fake_tmux_ops.h"
#include "fakes/fake_notifier.h"
#include "fakes/fake_prompt.h"
#include "fakes/fake_clock.h"

using namespace tash::cluster;
using namespace tash::cluster::testing;

// ── Helper: build a config with 1 cluster + 1 resource + 1 preset ──

Config basic_config() {
    Config c;
    c.defaults.workspace_base = "/scratch";
    c.defaults.default_preset = "claude";

    c.clusters.push_back({"c1", "ssh-c1", ""});

    Resource r;
    r.name = "a100";
    r.kind = ResourceKind::Gpu;
    r.routes.push_back({"c1", "acc", "p", "q", "gpu:a100:1"});
    c.resources.push_back(r);

    Preset p;
    p.name = "claude"; p.command = "claude";
    c.presets.push_back(p);

    return c;
}

Allocation running_alloc(const std::string& cluster, const std::string& jobid,
                           const std::string& node) {
    Allocation a;
    a.id = cluster + ":" + jobid;
    a.cluster = cluster;
    a.jobid = jobid;
    a.resource = "a100";
    a.node = node;
    a.state = AllocationState::Running;
    return a;
}

struct Harness {
    Config cfg;
    Registry reg;
    FakeSshClient ssh;
    FakeSlurmOps  slurm;
    FakeTmuxOps   tmux;
    FakeNotifier  notify;
    FakePrompt    prompt;
    FakeClock     clock;

    Harness() : cfg(basic_config()) {}
    ClusterEngine engine() {
        return ClusterEngine(cfg, reg, ssh, slurm, tmux, notify, prompt, clock);
    }
};

// ── 1. New workspace → creates tmux session + first window ─────────

TEST(ClusterEngineLaunch, NewWorkspaceCreatesSessionAndWindow) {
    Harness h;
    h.reg.add_allocation(running_alloc("c1", "1001", "n1"));

    LaunchSpec ls;
    ls.workspace = "repoA";
    ls.preset    = "claude";

    auto r = h.engine().launch(ls);
    auto* inst = std::get_if<Instance>(&r);
    ASSERT_NE(inst, nullptr) << std::get<EngineError>(r).message;

    ASSERT_EQ(h.tmux.new_session_calls.size(), 1u);
    EXPECT_EQ(h.tmux.new_session_calls[0].session, "tash-c1-1001-repoA");
    EXPECT_EQ(h.tmux.new_session_calls[0].target.cluster, "c1");
    EXPECT_EQ(h.tmux.new_session_calls[0].target.node,    "n1");

    ASSERT_EQ(h.tmux.new_window_calls.size(), 1u);
    // cmd is srun-wrapped so it runs on the compute node even though
    // the tmux window itself lives in the login-node tmux server.
    EXPECT_EQ(h.tmux.new_window_calls[0].cmd,
              "srun --jobid=1001 --overlap bash -c 'claude'");

    // Registry updated
    auto* a = h.reg.find_allocation("c1:1001");
    ASSERT_NE(a, nullptr);
    ASSERT_EQ(a->workspaces.size(), 1u);
    EXPECT_EQ(a->workspaces[0].name, "repoA");
    EXPECT_EQ(a->workspaces[0].tmux_session, "tash-c1-1001-repoA");
    ASSERT_EQ(a->workspaces[0].instances.size(), 1u);
    EXPECT_EQ(a->workspaces[0].instances[0].id, "1");
    EXPECT_EQ(a->workspaces[0].instances[0].state, InstanceState::Running);
}

// ── 2. Existing workspace → skips new_session, adds window ─────────

TEST(ClusterEngineLaunch, ExistingWorkspaceReusesSessionAddsWindow) {
    Harness h;
    Allocation a = running_alloc("c1", "1001", "n1");
    Workspace ws;
    ws.name = "repoA"; ws.cwd = "/scratch/repoA"; ws.tmux_session = "tash-c1-1001-repoA";
    Instance inst; inst.id = "1"; inst.tmux_window = "1";
    ws.instances.push_back(inst);
    a.workspaces.push_back(ws);
    h.reg.add_allocation(a);

    LaunchSpec ls;
    ls.workspace = "repoA";
    ls.preset    = "claude";

    auto r = h.engine().launch(ls);
    auto* added = std::get_if<Instance>(&r);
    ASSERT_NE(added, nullptr);

    EXPECT_EQ(h.tmux.new_session_calls.size(), 0u);   // reuse
    ASSERT_EQ(h.tmux.new_window_calls.size(), 1u);

    // Two instances now
    auto* ap = h.reg.find_allocation("c1:1001");
    ASSERT_EQ(ap->workspaces[0].instances.size(), 2u);
    EXPECT_EQ(ap->workspaces[0].instances[1].id, "2");   // auto-incremented
}

// ── 3. Preset resolution — command flows to new_window ─────────────

TEST(ClusterEngineLaunch, PresetCommandPropagates) {
    Harness h;
    // Replace the preset with a distinctive command
    h.cfg.presets.clear();
    Preset p; p.name = "trainer"; p.command = "python train.py --batch 32";
    h.cfg.presets.push_back(p);

    h.reg.add_allocation(running_alloc("c1", "1001", "n1"));

    LaunchSpec ls;
    ls.workspace = "workA";
    ls.preset    = "trainer";

    auto r = h.engine().launch(ls);
    ASSERT_NE(std::get_if<Instance>(&r), nullptr);
    ASSERT_EQ(h.tmux.new_window_calls.size(), 1u);
    EXPECT_EQ(h.tmux.new_window_calls[0].cmd,
              "srun --jobid=1001 --overlap bash -c 'python train.py --batch 32'");
}

// ── 4. Ad-hoc --cmd bypasses preset lookup ─────────────────────────

TEST(ClusterEngineLaunch, AdHocCmdBypassesPreset) {
    Harness h;
    h.reg.add_allocation(running_alloc("c1", "1001", "n1"));

    LaunchSpec ls;
    ls.workspace = "ws";
    ls.cmd       = "bash -i";
    // Intentionally point at a nonexistent preset — should not be consulted.
    ls.preset    = "does-not-exist";

    auto r = h.engine().launch(ls);
    ASSERT_NE(std::get_if<Instance>(&r), nullptr) << std::get<EngineError>(r).message;
    ASSERT_EQ(h.tmux.new_window_calls.size(), 1u);
    EXPECT_EQ(h.tmux.new_window_calls[0].cmd,
              "srun --jobid=1001 --overlap bash -c 'bash -i'");
}

// ── 5. Window exits within 2s → Exited + notifier fires ────────────

TEST(ClusterEngineLaunch, WindowExitsImmediatelyMarksExitedAndNotifies) {
    Harness h;
    h.reg.add_allocation(running_alloc("c1", "1001", "n1"));

    // Simulate "the command died right after launch"
    h.tmux.mark_dead("tash-c1-1001-ws", "1");

    LaunchSpec ls;
    ls.workspace = "ws";
    ls.cmd       = "mistyped-bin";

    auto r = h.engine().launch(ls);
    auto* inst = std::get_if<Instance>(&r);
    ASSERT_NE(inst, nullptr);
    EXPECT_EQ(inst->state, InstanceState::Exited);

    ASSERT_EQ(h.notify.desktop_calls.size(), 1u);
    EXPECT_NE(h.notify.desktop_calls[0].title.find("exited"), std::string::npos)
        << h.notify.desktop_calls[0].title;

    // Registry reflects Exited state.
    auto* a = h.reg.find_allocation("c1:1001");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->workspaces[0].instances[0].state, InstanceState::Exited);
}

// ── 6. Ambiguous allocation → error ────────────────────────────────

TEST(ClusterEngineLaunch, AmbiguousAllocationErrors) {
    Harness h;
    h.reg.add_allocation(running_alloc("c1", "1001", "n1"));
    h.reg.add_allocation(running_alloc("c1", "2002", "n2"));

    LaunchSpec ls;
    ls.workspace = "ws";
    ls.preset    = "claude";

    auto r = h.engine().launch(ls);
    auto* err = std::get_if<EngineError>(&r);
    ASSERT_NE(err, nullptr);
    EXPECT_NE(err->message.find("ambiguous"), std::string::npos) << err->message;
    EXPECT_NE(err->message.find("1001"),      std::string::npos);
    EXPECT_NE(err->message.find("2002"),      std::string::npos);
}

// ── 7. --alloc override selects specific running allocation ────────

TEST(ClusterEngineLaunch, AllocOverrideSelectsSpecific) {
    Harness h;
    h.reg.add_allocation(running_alloc("c1", "1001", "n1"));
    h.reg.add_allocation(running_alloc("c1", "2002", "n2"));

    LaunchSpec ls;
    ls.workspace = "ws";
    ls.preset    = "claude";
    ls.alloc_id  = "c1:2002";

    auto r = h.engine().launch(ls);
    ASSERT_NE(std::get_if<Instance>(&r), nullptr) << std::get<EngineError>(r).message;
    ASSERT_EQ(h.tmux.new_session_calls.size(), 1u);
    EXPECT_EQ(h.tmux.new_session_calls[0].target.node, "n2");
}

// ── 8. No running allocation → error ───────────────────────────────

TEST(ClusterEngineLaunch, NoRunningAllocationErrors) {
    Harness h;
    // Registry empty.

    LaunchSpec ls;
    ls.workspace = "ws";
    ls.preset    = "claude";

    auto r = h.engine().launch(ls);
    auto* err = std::get_if<EngineError>(&r);
    ASSERT_NE(err, nullptr);
    EXPECT_NE(err->message.find("no running allocation"), std::string::npos) << err->message;
}

// ── 9. Instance --name overrides auto-assigned id for window name ──

TEST(ClusterEngineLaunch, InstanceNameOverrideUsedAsWindowName) {
    Harness h;
    h.reg.add_allocation(running_alloc("c1", "1001", "n1"));

    LaunchSpec ls;
    ls.workspace = "ws";
    ls.preset    = "claude";
    ls.name      = "feature-x";

    auto r = h.engine().launch(ls);
    auto* inst = std::get_if<Instance>(&r);
    ASSERT_NE(inst, nullptr);
    EXPECT_EQ(inst->tmux_window, "feature-x");
    EXPECT_EQ(inst->name.value_or(""), "feature-x");

    ASSERT_EQ(h.tmux.new_window_calls.size(), 1u);
    EXPECT_EQ(h.tmux.new_window_calls[0].window, "feature-x");
}

// Regression: if remote tmux refuses new-session, the workspace must
// NOT be added to the registry. Otherwise the registry records a
// workspace that doesn't exist on the cluster.
TEST(ClusterEngineLaunch, NewSessionFailureLeavesRegistryIntact) {
    Harness h;
    h.cfg = basic_config();
    h.reg.add_allocation(running_alloc("c1", "100", "n1"));
    h.tmux.new_session_result = false;

    LaunchSpec ls;
    ls.workspace = "repoA";
    ls.preset    = "claude";

    auto r = h.engine().launch(ls);
    auto* err = std::get_if<EngineError>(&r);
    ASSERT_NE(err, nullptr);
    EXPECT_NE(err->message.find("new-session"), std::string::npos) << err->message;

    ASSERT_EQ(h.reg.allocations.size(), 1u);
    EXPECT_TRUE(h.reg.allocations[0].workspaces.empty());
    EXPECT_EQ(h.tmux.new_window_calls.size(), 0u);
}

// Regression: if new-window fails after a freshly-created workspace,
// we must roll back the workspace so the registry stays coherent.
TEST(ClusterEngineLaunch, NewWindowFailureRollsBackFreshWorkspace) {
    Harness h;
    h.cfg = basic_config();
    h.reg.add_allocation(running_alloc("c1", "100", "n1"));
    h.tmux.new_window_result = false;

    LaunchSpec ls;
    ls.workspace = "repoA";
    ls.preset    = "claude";

    auto r = h.engine().launch(ls);
    auto* err = std::get_if<EngineError>(&r);
    ASSERT_NE(err, nullptr);
    EXPECT_NE(err->message.find("new-window"), std::string::npos) << err->message;

    ASSERT_EQ(h.reg.allocations.size(), 1u);
    EXPECT_TRUE(h.reg.allocations[0].workspaces.empty());
}

// Regression: when launch into an existing workspace fails at
// new-window, we must NOT pop the pre-existing workspace. Rollback is
// only for the workspace we created this call.
TEST(ClusterEngineLaunch, NewWindowFailureDoesNotTouchExistingWorkspace) {
    Harness h;
    h.cfg = basic_config();
    Allocation a = running_alloc("c1", "100", "n1");
    Workspace preexisting;
    preexisting.name         = "repoA";
    preexisting.cwd          = "/scratch/repoA";
    preexisting.tmux_session = "tash-c1-100-repoA";
    a.workspaces.push_back(preexisting);
    h.reg.add_allocation(a);

    h.tmux.new_window_result = false;

    LaunchSpec ls;
    ls.workspace = "repoA";
    ls.preset    = "claude";

    auto r = h.engine().launch(ls);
    ASSERT_NE(std::get_if<EngineError>(&r), nullptr);

    // Workspace still there, instances still empty.
    ASSERT_EQ(h.reg.allocations.size(), 1u);
    ASSERT_EQ(h.reg.allocations[0].workspaces.size(), 1u);
    EXPECT_TRUE(h.reg.allocations[0].workspaces[0].instances.empty());
}
