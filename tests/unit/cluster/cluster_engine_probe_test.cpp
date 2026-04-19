// ClusterEngine::probe — show current state of each route for a resource.

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

Config a100_two_routes() {
    Config c;
    c.clusters.push_back({"c1", "ssh-c1", ""});
    c.clusters.push_back({"c2", "ssh-c2", ""});
    Resource r; r.name = "a100"; r.kind = ResourceKind::Gpu;
    r.routes.push_back({"c1", "acc", "p1", "q1", "gpu:a100:1"});
    r.routes.push_back({"c2", "acc", "p2", "q2", "gpu:a100:1"});
    c.resources.push_back(r);
    return c;
}
}  // namespace

TEST(ClusterEngineProbe, UnknownResourceErrors) {
    Harness h;
    h.cfg = a100_two_routes();
    ProbeSpec ps; ps.resource = "h100";
    auto r = h.engine().probe(ps);
    auto* err = std::get_if<EngineError>(&r);
    ASSERT_NE(err, nullptr);
    EXPECT_NE(err->message.find("h100"), std::string::npos);
}

TEST(ClusterEngineProbe, ReportsEachRoute) {
    Harness h;
    h.cfg = a100_two_routes();
    // c1/p1: 3 idle nodes, all have a100
    h.slurm.queue_sinfo({
        PartitionState{"p1", "up", 3, {"gpu:a100:1", "gpu:a100:1", "gpu:a100:1"}}
    });
    // c2/p2: 2 idle nodes, none matching (different gpu type)
    h.slurm.queue_sinfo({
        PartitionState{"p2", "up", 2, {"gpu:h100:1", "gpu:h100:1"}}
    });

    ProbeSpec ps; ps.resource = "a100";
    auto r = h.engine().probe(ps);
    auto* rep = std::get_if<ClusterEngine::ProbeReport>(&r);
    ASSERT_NE(rep, nullptr);
    EXPECT_EQ(rep->resource, "a100");
    ASSERT_EQ(rep->routes.size(), 2u);

    EXPECT_EQ(rep->routes[0].cluster,             "c1");
    EXPECT_EQ(rep->routes[0].partition,           "p1");
    EXPECT_EQ(rep->routes[0].idle_nodes,          3);
    EXPECT_EQ(rep->routes[0].idle_matching_gres,  3);
    EXPECT_EQ(rep->routes[0].partition_state,     "up");

    EXPECT_EQ(rep->routes[1].cluster,             "c2");
    EXPECT_EQ(rep->routes[1].idle_nodes,          2);
    EXPECT_EQ(rep->routes[1].idle_matching_gres,  0);   // h100 != a100
}

TEST(ClusterEngineProbe, HandlesEmptySinfoResponse) {
    Harness h;
    h.cfg = a100_two_routes();
    // both partitions return empty sinfo
    auto r = h.engine().probe({"a100"});
    auto* rep = std::get_if<ClusterEngine::ProbeReport>(&r);
    ASSERT_NE(rep, nullptr);
    ASSERT_EQ(rep->routes.size(), 2u);
    EXPECT_EQ(rep->routes[0].idle_nodes, 0);
    EXPECT_EQ(rep->routes[1].idle_nodes, 0);
}
