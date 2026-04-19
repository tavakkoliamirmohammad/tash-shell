// Tests for ClusterEngine::attach — resolves <workspace>/<instance>
// within the correct allocation, calls exec_attach on success.

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

namespace {

Config empty_config() {
    Config c;
    c.defaults.workspace_base = "/scratch";
    return c;
}

// Build an allocation with a workspace and N instances (ids "1"…"N").
Allocation alloc_with_instances(const std::string& cluster,
                                  const std::string& jobid,
                                  const std::string& node,
                                  const std::string& ws_name,
                                  const std::vector<std::string>& instance_ids,
                                  const std::vector<std::string>& instance_names = {}) {
    Allocation a;
    a.id      = cluster + ":" + jobid;
    a.cluster = cluster;
    a.jobid   = jobid;
    a.node    = node;
    a.state   = AllocationState::Running;

    Workspace ws;
    ws.name         = ws_name;
    ws.cwd          = "/scratch/" + ws_name;
    ws.tmux_session = "tash-" + cluster + "-" + jobid + "-" + ws_name;
    for (std::size_t i = 0; i < instance_ids.size(); ++i) {
        Instance inst;
        inst.id          = instance_ids[i];
        inst.tmux_window = instance_ids[i];
        if (i < instance_names.size()) {
            inst.name        = instance_names[i];
            inst.tmux_window = instance_names[i];
        }
        ws.instances.push_back(inst);
    }
    a.workspaces.push_back(ws);
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

    Harness() : cfg(empty_config()) {}
    ClusterEngine engine() {
        return ClusterEngine(cfg, reg, ssh, slurm, tmux, notify, prompt, clock);
    }
};

}  // namespace

// ── 1. Basic attach resolves and calls exec_attach ─────────────────

TEST(ClusterEngineAttach, BasicAttachCallsExecAttach) {
    Harness h;
    h.reg.add_allocation(alloc_with_instances("c1", "1001", "n1", "repoA", {"1"}));

    AttachSpec as;
    as.workspace = "repoA";
    as.instance  = "1";

    auto r = h.engine().attach(as);
    ASSERT_NE(std::get_if<Instance>(&r), nullptr) << std::get<EngineError>(r).message;

    ASSERT_EQ(h.tmux.exec_attach_calls.size(), 1u);
    const auto& ec = h.tmux.exec_attach_calls[0];
    EXPECT_EQ(ec.target.cluster, "c1");
    EXPECT_EQ(ec.target.node,    "n1");
    EXPECT_EQ(ec.session,        "tash-c1-1001-repoA");
    EXPECT_EQ(ec.window,         "1");
}

// ── 2. Resolve instance by user-supplied --name ────────────────────

TEST(ClusterEngineAttach, ResolvesInstanceByName) {
    Harness h;
    h.reg.add_allocation(alloc_with_instances(
        "c1", "1001", "n1", "repoA", {"1", "2"}, {"", "feature-x"}));

    AttachSpec as; as.workspace = "repoA"; as.instance = "feature-x";

    auto r = h.engine().attach(as);
    ASSERT_NE(std::get_if<Instance>(&r), nullptr);
    ASSERT_EQ(h.tmux.exec_attach_calls.size(), 1u);
    EXPECT_EQ(h.tmux.exec_attach_calls[0].window, "feature-x");
}

// ── 3. Missing workspace → error ───────────────────────────────────

TEST(ClusterEngineAttach, MissingWorkspaceErrors) {
    Harness h;
    h.reg.add_allocation(alloc_with_instances("c1", "1001", "n1", "repoA", {"1"}));

    AttachSpec as; as.workspace = "nope"; as.instance = "1";
    auto r = h.engine().attach(as);
    auto* err = std::get_if<EngineError>(&r);
    ASSERT_NE(err, nullptr);
    EXPECT_NE(err->message.find("workspace"), std::string::npos) << err->message;
    EXPECT_NE(err->message.find("nope"),      std::string::npos);
    EXPECT_EQ(h.tmux.exec_attach_calls.size(), 0u);
}

// ── 4. Missing instance → error ────────────────────────────────────

TEST(ClusterEngineAttach, MissingInstanceErrors) {
    Harness h;
    h.reg.add_allocation(alloc_with_instances("c1", "1001", "n1", "repoA", {"1"}));

    AttachSpec as; as.workspace = "repoA"; as.instance = "999";
    auto r = h.engine().attach(as);
    auto* err = std::get_if<EngineError>(&r);
    ASSERT_NE(err, nullptr);
    EXPECT_NE(err->message.find("instance"), std::string::npos) << err->message;
    EXPECT_NE(err->message.find("999"),      std::string::npos);
    EXPECT_EQ(h.tmux.exec_attach_calls.size(), 0u);
}

// ── 5. Ambiguous (two allocations share workspace+instance) ────────

TEST(ClusterEngineAttach, AmbiguousAcrossAllocationsErrors) {
    Harness h;
    h.reg.add_allocation(alloc_with_instances("c1", "1001", "n1", "repoA", {"1"}));
    h.reg.add_allocation(alloc_with_instances("c1", "2002", "n2", "repoA", {"1"}));

    AttachSpec as; as.workspace = "repoA"; as.instance = "1";
    auto r = h.engine().attach(as);
    auto* err = std::get_if<EngineError>(&r);
    ASSERT_NE(err, nullptr);
    EXPECT_NE(err->message.find("ambiguous"), std::string::npos) << err->message;
    EXPECT_NE(err->message.find("1001"),      std::string::npos);
    EXPECT_NE(err->message.find("2002"),      std::string::npos);
    EXPECT_EQ(h.tmux.exec_attach_calls.size(), 0u);
}

// ── 6. --alloc disambiguates ───────────────────────────────────────

TEST(ClusterEngineAttach, AllocIdDisambiguatesAcrossAllocations) {
    Harness h;
    h.reg.add_allocation(alloc_with_instances("c1", "1001", "n1", "repoA", {"1"}));
    h.reg.add_allocation(alloc_with_instances("c1", "2002", "n2", "repoA", {"1"}));

    AttachSpec as;
    as.workspace = "repoA"; as.instance = "1"; as.alloc_id = "c1:2002";

    auto r = h.engine().attach(as);
    ASSERT_NE(std::get_if<Instance>(&r), nullptr);
    ASSERT_EQ(h.tmux.exec_attach_calls.size(), 1u);
    EXPECT_EQ(h.tmux.exec_attach_calls[0].target.node, "n2");
}

// ── 7. --alloc points at an allocation that lacks the workspace ────

TEST(ClusterEngineAttach, AllocIdWithoutMatchingWorkspaceErrors) {
    Harness h;
    h.reg.add_allocation(alloc_with_instances("c1", "1001", "n1", "repoA", {"1"}));

    AttachSpec as; as.workspace = "repoA"; as.instance = "1"; as.alloc_id = "c1:9999";

    auto r = h.engine().attach(as);
    auto* err = std::get_if<EngineError>(&r);
    ASSERT_NE(err, nullptr);
    EXPECT_NE(err->message.find("c1:9999"), std::string::npos) << err->message;
}
