// ClusterEngine::list — snapshot of registered allocations with optional
// cluster filter.

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
Allocation alloc(std::string c, std::string j, AllocationState s = AllocationState::Running) {
    Allocation a; a.id = c + ":" + j; a.cluster = c; a.jobid = j; a.state = s;
    return a;
}
}  // namespace

TEST(ClusterEngineList, EmptyRegistryReturnsEmpty) {
    Harness h;
    auto r = h.engine().list({});
    auto* v = std::get_if<std::vector<Allocation>>(&r);
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(v->size(), 0u);
}

TEST(ClusterEngineList, ReturnsAllInInsertionOrder) {
    Harness h;
    h.reg.add_allocation(alloc("c1", "100"));
    h.reg.add_allocation(alloc("c2", "200"));
    h.reg.add_allocation(alloc("c1", "300"));

    auto r = h.engine().list({});
    auto* v = std::get_if<std::vector<Allocation>>(&r);
    ASSERT_NE(v, nullptr);
    ASSERT_EQ(v->size(), 3u);
    EXPECT_EQ((*v)[0].id, "c1:100");
    EXPECT_EQ((*v)[1].id, "c2:200");
    EXPECT_EQ((*v)[2].id, "c1:300");
}

TEST(ClusterEngineList, FiltersByCluster) {
    Harness h;
    h.reg.add_allocation(alloc("c1", "100"));
    h.reg.add_allocation(alloc("c2", "200"));
    h.reg.add_allocation(alloc("c1", "300"));

    ListSpec ls; ls.cluster = "c1";
    auto r = h.engine().list(ls);
    auto* v = std::get_if<std::vector<Allocation>>(&r);
    ASSERT_NE(v, nullptr);
    ASSERT_EQ(v->size(), 2u);
    EXPECT_EQ((*v)[0].id, "c1:100");
    EXPECT_EQ((*v)[1].id, "c1:300");
}

TEST(ClusterEngineList, UnknownClusterFilterReturnsEmpty) {
    Harness h;
    h.reg.add_allocation(alloc("c1", "100"));

    ListSpec ls; ls.cluster = "no-such";
    auto r = h.engine().list(ls);
    auto* v = std::get_if<std::vector<Allocation>>(&r);
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(v->size(), 0u);
}

TEST(ClusterEngineList, IncludesEndedAllocationsByDefault) {
    Harness h;
    h.reg.add_allocation(alloc("c1", "100", AllocationState::Ended));
    h.reg.add_allocation(alloc("c1", "200", AllocationState::Running));

    auto r = h.engine().list({});
    auto* v = std::get_if<std::vector<Allocation>>(&r);
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(v->size(), 2u);
}
