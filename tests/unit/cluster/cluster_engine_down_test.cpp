// ClusterEngine::down — scancel + registry cleanup.

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

TEST(ClusterEngineDown, BasicDownScancelsAndPurges) {
    Harness h;
    h.reg.add_allocation(alloc("c1", "100"));
    h.reg.add_allocation(alloc("c1", "200"));

    DownSpec ds; ds.alloc_id = "c1:100";
    auto r = h.engine().down(ds);
    auto* terminated = std::get_if<Allocation>(&r);
    ASSERT_NE(terminated, nullptr);
    EXPECT_EQ(terminated->state, AllocationState::Ended);

    ASSERT_EQ(h.slurm.scancel_calls.size(), 1u);
    EXPECT_EQ(h.slurm.scancel_calls[0].jobid, "100");

    // Registry purged the one allocation; other survives.
    ASSERT_EQ(h.reg.allocations.size(), 1u);
    EXPECT_EQ(h.reg.allocations[0].id, "c1:200");
}

TEST(ClusterEngineDown, UnknownAllocIdErrors) {
    Harness h;
    DownSpec ds; ds.alloc_id = "c1:999";
    auto r = h.engine().down(ds);
    auto* err = std::get_if<EngineError>(&r);
    ASSERT_NE(err, nullptr);
    EXPECT_NE(err->message.find("c1:999"), std::string::npos) << err->message;
    EXPECT_EQ(h.slurm.scancel_calls.size(), 0u);
}

TEST(ClusterEngineDown, AlreadyEndedAllocationSkipsScancel) {
    Harness h;
    h.reg.add_allocation(alloc("c1", "100", AllocationState::Ended));

    DownSpec ds; ds.alloc_id = "c1:100";
    auto r = h.engine().down(ds);
    ASSERT_NE(std::get_if<Allocation>(&r), nullptr);
    EXPECT_EQ(h.slurm.scancel_calls.size(), 0u);       // no-op on SLURM
    EXPECT_EQ(h.reg.allocations.size(), 0u);            // still purged locally
}

TEST(ClusterEngineDown, EmptyAllocIdRejected) {
    Harness h;
    DownSpec ds; // alloc_id empty
    auto r = h.engine().down(ds);
    auto* err = std::get_if<EngineError>(&r);
    ASSERT_NE(err, nullptr);
    EXPECT_NE(err->message.find("alloc"), std::string::npos) << err->message;
}
