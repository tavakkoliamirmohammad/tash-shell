// Tests for the `-y` / `--yes` safety gate on destructive
// `cluster down` / `cluster kill` commands.
//
// The confirmation happens inside the builtin dispatcher (cmd_down /
// cmd_kill in src/cluster/builtin_dispatch.cpp), not inside the engine —
// so the engine tests stay focused on their contract. When `-y` is
// absent, the dispatcher calls engine.prompt().choice("…[y/n]?", "yn");
// 'y' lets the op proceed, anything else aborts without invoking scancel
// / kill_window.

#include <gtest/gtest.h>

#include "tash/cluster/builtin_dispatch.h"
#include "tash/cluster/cluster_engine.h"
#include "tash/cluster/config.h"
#include "tash/cluster/registry.h"

#include "fakes/fake_ssh_client.h"
#include "fakes/fake_slurm_ops.h"
#include "fakes/fake_tmux_ops.h"
#include "fakes/fake_notifier.h"
#include "fakes/fake_prompt.h"
#include "fakes/fake_clock.h"

#include <sstream>

using namespace tash::cluster;
using namespace tash::cluster::testing;

namespace {

struct Harness {
    Config         cfg;
    Registry       reg;
    FakeSshClient  ssh;
    FakeSlurmOps   slurm;
    FakeTmuxOps    tmux;
    FakeNotifier   notify;
    FakePrompt     prompt;
    FakeClock      clock;

    ClusterEngine engine() {
        return ClusterEngine(cfg, reg, ssh, slurm, tmux, notify, prompt, clock);
    }
};

Allocation alloc(std::string c, std::string j,
                   AllocationState s = AllocationState::Running) {
    Allocation a; a.id = c + ":" + j; a.cluster = std::move(c);
    a.jobid = std::move(j); a.state = s;
    return a;
}

Allocation alloc_with_instance(std::string cluster, std::string jobid,
                                  std::string node, std::string ws_name,
                                  std::string inst_id) {
    Allocation a; a.id = cluster + ":" + jobid;
    a.cluster = std::move(cluster); a.jobid = std::move(jobid);
    a.node = std::move(node); a.state = AllocationState::Running;
    Workspace ws; ws.name = std::move(ws_name); ws.tmux_session = "s";
    Instance i; i.id = inst_id; i.tmux_window = inst_id;
    ws.instances.push_back(i);
    a.workspaces.push_back(ws);
    return a;
}

std::vector<std::string> argv_of(std::initializer_list<const char*> xs) {
    return {xs.begin(), xs.end()};
}

}  // namespace

// ══════════════════════════════════════════════════════════════════════════════
// down
// ══════════════════════════════════════════════════════════════════════════════

TEST(ClusterSafety, DownPromptsByDefault) {
    Harness h;
    h.reg.add_allocation(alloc("c1", "100"));
    h.prompt.queue_answer('y');

    auto eng = h.engine();
    std::ostringstream out, err;
    const int rc = dispatch_cluster(argv_of({"cluster", "down", "c1:100"}),
                                       eng, out, err);
    EXPECT_EQ(rc, 0) << err.str();
    EXPECT_EQ(h.slurm.scancel_calls.size(), 1u);
    ASSERT_EQ(h.prompt.calls.size(), 1u);
    EXPECT_NE(h.prompt.calls[0].message.find("c1:100"), std::string::npos)
        << h.prompt.calls[0].message;
}

TEST(ClusterSafety, DownAbortsWhenUserDeclines) {
    Harness h;
    h.reg.add_allocation(alloc("c1", "100"));
    h.prompt.queue_answer('n');

    auto eng = h.engine();
    std::ostringstream out, err;
    const int rc = dispatch_cluster(argv_of({"cluster", "down", "c1:100"}),
                                       eng, out, err);
    EXPECT_NE(rc, 0);
    EXPECT_EQ(h.slurm.scancel_calls.size(), 0u);
    EXPECT_EQ(h.reg.allocations().size(), 1u);
    EXPECT_NE(err.str().find("cancelled"), std::string::npos) << err.str();
}

TEST(ClusterSafety, DownDashYBypassesPrompt) {
    Harness h;
    h.reg.add_allocation(alloc("c1", "100"));
    // No answers queued — if prompt is called, FakePrompt returns '\0'
    // which we treat as non-interactive abort. So a successful -y run
    // proves the prompt was skipped.

    auto eng = h.engine();
    std::ostringstream out, err;
    const int rc = dispatch_cluster(argv_of({"cluster", "down", "c1:100", "-y"}),
                                       eng, out, err);
    EXPECT_EQ(rc, 0) << err.str();
    EXPECT_EQ(h.slurm.scancel_calls.size(), 1u);
    EXPECT_EQ(h.prompt.calls.size(), 0u);
}

TEST(ClusterSafety, DownLongFormYesBypassesPrompt) {
    Harness h;
    h.reg.add_allocation(alloc("c1", "100"));

    auto eng = h.engine();
    std::ostringstream out, err;
    const int rc = dispatch_cluster(argv_of({"cluster", "down", "c1:100", "--yes"}),
                                       eng, out, err);
    EXPECT_EQ(rc, 0) << err.str();
    EXPECT_EQ(h.prompt.calls.size(), 0u);
}

// ══════════════════════════════════════════════════════════════════════════════
// kill
// ══════════════════════════════════════════════════════════════════════════════

TEST(ClusterSafety, KillPromptsByDefault) {
    Harness h;
    h.reg.add_allocation(alloc_with_instance("c1", "100", "n1", "ws", "1"));
    h.prompt.queue_answer('y');

    auto eng = h.engine();
    std::ostringstream out, err;
    const int rc = dispatch_cluster(argv_of({"cluster", "kill", "ws/1"}),
                                       eng, out, err);
    EXPECT_EQ(rc, 0) << err.str();
    EXPECT_EQ(h.tmux.kill_window_calls.size(), 1u);
    ASSERT_EQ(h.prompt.calls.size(), 1u);
    EXPECT_NE(h.prompt.calls[0].message.find("ws/1"), std::string::npos)
        << h.prompt.calls[0].message;
}

TEST(ClusterSafety, KillAbortsWhenUserDeclines) {
    Harness h;
    h.reg.add_allocation(alloc_with_instance("c1", "100", "n1", "ws", "1"));
    h.prompt.queue_answer('n');

    auto eng = h.engine();
    std::ostringstream out, err;
    const int rc = dispatch_cluster(argv_of({"cluster", "kill", "ws/1"}),
                                       eng, out, err);
    EXPECT_NE(rc, 0);
    EXPECT_EQ(h.tmux.kill_window_calls.size(), 0u);
}

TEST(ClusterSafety, KillDashYBypassesPrompt) {
    Harness h;
    h.reg.add_allocation(alloc_with_instance("c1", "100", "n1", "ws", "1"));

    auto eng = h.engine();
    std::ostringstream out, err;
    const int rc = dispatch_cluster(argv_of({"cluster", "kill", "ws/1", "-y"}),
                                       eng, out, err);
    EXPECT_EQ(rc, 0) << err.str();
    EXPECT_EQ(h.tmux.kill_window_calls.size(), 1u);
    EXPECT_EQ(h.prompt.calls.size(), 0u);
}

// ══════════════════════════════════════════════════════════════════════════════
// Preview content is informative
// ══════════════════════════════════════════════════════════════════════════════

TEST(ClusterSafety, DownPreviewIncludesResourceAndNode) {
    Harness h;
    Allocation a = alloc("c1", "100");
    a.resource = "a100"; a.node = "notch5";
    h.reg.add_allocation(a);
    h.prompt.queue_answer('y');

    auto eng = h.engine();
    std::ostringstream out, err;
    dispatch_cluster(argv_of({"cluster", "down", "c1:100"}), eng, out, err);

    ASSERT_EQ(h.prompt.calls.size(), 1u);
    const auto& msg = h.prompt.calls[0].message;
    EXPECT_NE(msg.find("a100"),   std::string::npos) << msg;
    EXPECT_NE(msg.find("notch5"), std::string::npos) << msg;
}
