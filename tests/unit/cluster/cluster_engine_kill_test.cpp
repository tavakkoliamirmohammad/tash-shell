// ClusterEngine::kill — terminate a single instance (tmux window).

#include <gtest/gtest.h>

#include "tash/cluster/cluster_engine.h"
#include "fakes/fake_ssh_client.h"
#include "fakes/fake_slurm_ops.h"
#include "fakes/fake_tmux_ops.h"
#include "fakes/fake_notifier.h"
#include "fakes/fake_prompt.h"
#include "fakes/fake_clock.h"

using namespace tash::cluster;
using namespace tash::cluster::testing;

namespace {
struct Harness {
    Config cfg;
    Registry reg;
    FakeSshClient ssh; FakeSlurmOps slurm; FakeTmuxOps tmux;
    FakeNotifier notify; FakePrompt prompt; FakeClock clock;
    ClusterEngine engine() {
        return ClusterEngine(cfg, reg, ssh, slurm, tmux, notify, prompt, clock);
    }
};

Allocation alloc_with_ws_inst(const std::string& cluster,
                                const std::string& jobid,
                                const std::string& node,
                                const std::string& ws_name,
                                const std::vector<std::string>& ids) {
    Allocation a;
    a.id = cluster + ":" + jobid;
    a.cluster = cluster; a.jobid = jobid; a.node = node;
    a.state = AllocationState::Running;
    Workspace ws;
    ws.name = ws_name; ws.cwd = "/scratch/" + ws_name;
    ws.tmux_session = "tash-" + cluster + "-" + jobid + "-" + ws_name;
    for (const auto& id : ids) {
        Instance i; i.id = id; i.tmux_window = id;
        ws.instances.push_back(i);
    }
    a.workspaces.push_back(ws);
    return a;
}
}  // namespace

TEST(ClusterEngineKill, BasicKillRemovesInstance) {
    Harness h;
    h.reg.add_allocation(alloc_with_ws_inst("c1", "100", "n1", "repoA", {"1", "2"}));

    KillSpec ks; ks.workspace = "repoA"; ks.instance = "1";
    auto r = h.engine().kill(ks);
    ASSERT_NE(std::get_if<Instance>(&r), nullptr) << std::get<EngineError>(r).message;

    ASSERT_EQ(h.tmux.kill_window_calls.size(), 1u);
    EXPECT_EQ(h.tmux.kill_window_calls[0].window, "1");
    EXPECT_EQ(h.tmux.kill_window_calls[0].session, "tash-c1-100-repoA");

    auto* a = h.reg.find_allocation("c1:100");
    ASSERT_NE(a, nullptr);
    ASSERT_EQ(a->workspaces[0].instances.size(), 1u);
    EXPECT_EQ(a->workspaces[0].instances[0].id, "2");
}

TEST(ClusterEngineKill, WorkspaceLingersAfterLastInstanceKilled) {
    Harness h;
    h.reg.add_allocation(alloc_with_ws_inst("c1", "100", "n1", "repoA", {"1"}));

    KillSpec ks; ks.workspace = "repoA"; ks.instance = "1";
    auto r = h.engine().kill(ks);
    ASSERT_NE(std::get_if<Instance>(&r), nullptr);

    auto* a = h.reg.find_allocation("c1:100");
    ASSERT_NE(a, nullptr);
    ASSERT_EQ(a->workspaces.size(), 1u);               // retained
    EXPECT_EQ(a->workspaces[0].instances.size(), 0u);  // but empty
}

TEST(ClusterEngineKill, MissingInstanceErrors) {
    Harness h;
    h.reg.add_allocation(alloc_with_ws_inst("c1", "100", "n1", "repoA", {"1"}));

    KillSpec ks; ks.workspace = "repoA"; ks.instance = "999";
    auto r = h.engine().kill(ks);
    auto* err = std::get_if<EngineError>(&r);
    ASSERT_NE(err, nullptr);
    EXPECT_NE(err->message.find("999"), std::string::npos) << err->message;
    EXPECT_EQ(h.tmux.kill_window_calls.size(), 0u);
}

TEST(ClusterEngineKill, AmbiguousAcrossAllocsErrors) {
    Harness h;
    h.reg.add_allocation(alloc_with_ws_inst("c1", "100", "n1", "repoA", {"1"}));
    h.reg.add_allocation(alloc_with_ws_inst("c1", "200", "n2", "repoA", {"1"}));

    KillSpec ks; ks.workspace = "repoA"; ks.instance = "1";
    auto r = h.engine().kill(ks);
    auto* err = std::get_if<EngineError>(&r);
    ASSERT_NE(err, nullptr);
    EXPECT_NE(err->message.find("ambiguous"), std::string::npos) << err->message;
}

// Regression: if tmux refuses to kill the window (permission error,
// stale pid), the engine must leave the instance in registry so the
// user can retry, not silently forget about a running process.
TEST(ClusterEngineKill, TmuxRefusesKillLeavesInstanceIntact) {
    Harness h;
    h.reg.add_allocation(alloc_with_ws_inst("c1", "100", "n1", "repoA", {"1"}));
    h.tmux.kill_window_refuses = true;

    KillSpec ks; ks.workspace = "repoA"; ks.instance = "1";
    auto r = h.engine().kill(ks);

    auto* err = std::get_if<EngineError>(&r);
    ASSERT_NE(err, nullptr);
    EXPECT_NE(err->message.find("refused"), std::string::npos) << err->message;

    // Instance still present — the next retry will find it.
    ASSERT_EQ(h.reg.allocations.size(), 1u);
    ASSERT_EQ(h.reg.allocations[0].workspaces.size(), 1u);
    ASSERT_EQ(h.reg.allocations[0].workspaces[0].instances.size(), 1u);
}

// Regression: an ssh-level probe failure during kill verification must
// NOT be treated as "kill succeeded". Previously is_window_alive
// returning false (due to ssh failure, not a dead window) caused the
// engine to drop the instance from the registry and silently orphan
// the still-live tmux window.
TEST(ClusterEngineKill, LivenessUnknownAfterKillLeavesInstanceIntact) {
    Harness h;
    h.reg.add_allocation(alloc_with_ws_inst("c1", "100", "n1", "repoA", {"1"}));

    // Configure the fake so kill_window DOESN'T mark-dead, AND the
    // subsequent liveness probe returns Unknown (ssh went flaky right
    // after the kill command).
    h.tmux.kill_window_refuses = true;   // window stays alive
    h.tmux.mark_unknown("tash-c1-100-repoA", "1");

    KillSpec ks; ks.workspace = "repoA"; ks.instance = "1";
    auto r = h.engine().kill(ks);

    auto* err = std::get_if<EngineError>(&r);
    ASSERT_NE(err, nullptr)
        << "Unknown liveness after kill must surface as an error, not silent success";
    EXPECT_NE(err->message.find("unable to confirm"), std::string::npos)
        << err->message;

    // Instance MUST still be in the registry.
    ASSERT_EQ(h.reg.allocations[0].workspaces[0].instances.size(), 1u);
}
