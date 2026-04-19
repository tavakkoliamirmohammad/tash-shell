// Tests for ClusterEngine::up — route selection, sbatch submission,
// squeue polling, wait-timeout / detach / cancel.
//
// Every test constructs a fresh Config + Registry + fakes; no real ssh,
// no real slurm, no wall-clock sleep.

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

// ── Helper: build a two-route A100 config ──────────────────────────

Config two_route_a100_config() {
    Config c;
    c.defaults.workspace_base = "/tmp";
    c.defaults.default_preset = "claude";

    c.clusters.push_back({"c1", "ssh-c1", ""});
    c.clusters.push_back({"c2", "ssh-c2", ""});

    Resource a100;
    a100.name         = "a100";
    a100.kind         = ResourceKind::Gpu;
    a100.default_time = "4:00:00";
    a100.default_cpus = 8;
    a100.default_mem  = "32G";
    a100.routes.push_back({"c1", "acc1", "p1", "q1", "gpu:a100:1"});
    a100.routes.push_back({"c2", "acc2", "p2", "q2", "gpu:a100:1"});
    c.resources.push_back(a100);

    return c;
}

struct Harness {
    Config        cfg;
    Registry      reg;
    FakeSshClient ssh;
    FakeSlurmOps  slurm;
    FakeTmuxOps   tmux;
    FakeNotifier  notify;
    FakePrompt    prompt;
    FakeClock     clock;

    Harness() : cfg(two_route_a100_config()) {}

    ClusterEngine engine() {
        return ClusterEngine(cfg, reg, ssh, slurm, tmux, notify, prompt, clock);
    }
};

// Idle-gres PartitionState helper.
PartitionState idle(std::string part, int n, std::vector<std::string> g) {
    return PartitionState{std::move(part), "up", n, std::move(g)};
}
PartitionState busy(std::string part) {
    return PartitionState{std::move(part), "up", 0, {}};
}

// ── 1. All-routes-idle → first declared wins ───────────────────────

TEST(ClusterEngineUp, AllRoutesIdleFirstDeclaredWins) {
    Harness h;
    // sinfo probes on c1, then c2 if needed. First idle wins.
    h.slurm.queue_sinfo({idle("p1", 2, {"gpu:a100:1"})});
    h.slurm.queue_sbatch({"1001", "Submitted batch job 1001"});
    // squeue starts in R immediately
    h.slurm.queue_squeue({JobState{"1001", "R", "n1", "04:00:00"}});

    UpSpec spec;
    spec.resource = "a100";

    auto r = h.engine().up(spec);
    auto* alloc = std::get_if<Allocation>(&r);
    ASSERT_NE(alloc, nullptr) << std::get<EngineError>(r).message;

    EXPECT_EQ(alloc->cluster,  "c1");
    EXPECT_EQ(alloc->jobid,    "1001");
    EXPECT_EQ(alloc->resource, "a100");
    EXPECT_EQ(alloc->node,     "n1");
    EXPECT_EQ(alloc->state,    AllocationState::Running);

    ASSERT_EQ(h.slurm.sbatch_calls.size(), 1u);
    EXPECT_EQ(h.slurm.sbatch_calls[0].spec.cluster,   "c1");
    EXPECT_EQ(h.slurm.sbatch_calls[0].spec.partition, "p1");
    EXPECT_EQ(h.slurm.sbatch_calls[0].spec.account,   "acc1");

    // Persisted to registry
    ASSERT_EQ(h.reg.allocations.size(), 1u);
    EXPECT_EQ(h.reg.allocations[0].id, "c1:1001");
}

// ── 2. First route busy, second idle → second picked ───────────────

TEST(ClusterEngineUp, FirstBusySecondIdlePicksSecond) {
    Harness h;
    h.slurm.queue_sinfo({busy("p1")});                        // probe c1 -> 0 idle
    h.slurm.queue_sinfo({idle("p2", 1, {"gpu:a100:1"})});     // probe c2 -> idle
    h.slurm.queue_sbatch({"2002", ""});
    h.slurm.queue_squeue({JobState{"2002", "R", "n2", "04:00:00"}});

    UpSpec spec; spec.resource = "a100";
    auto r = h.engine().up(spec);
    auto* alloc = std::get_if<Allocation>(&r);
    ASSERT_NE(alloc, nullptr);
    EXPECT_EQ(alloc->cluster, "c2");
    EXPECT_EQ(alloc->node,    "n2");
}

// ── 3. All routes busy → first declared (falls through to queue) ────

TEST(ClusterEngineUp, AllBusyFallsBackToFirstRouteAndQueues) {
    Harness h;
    h.slurm.queue_sinfo({busy("p1")});
    h.slurm.queue_sinfo({busy("p2")});
    h.slurm.queue_sbatch({"3003", ""});
    // squeue shows PD first, then R
    h.slurm.queue_squeue({JobState{"3003", "PD", "", ""}});
    h.slurm.queue_squeue({JobState{"3003", "R", "n1", "04:00:00"}});

    UpSpec spec; spec.resource = "a100";
    auto r = h.engine().up(spec);
    auto* alloc = std::get_if<Allocation>(&r);
    ASSERT_NE(alloc, nullptr) << std::get<EngineError>(r).message;
    EXPECT_EQ(alloc->cluster, "c1");            // first declared
    EXPECT_EQ(alloc->state,   AllocationState::Running);
}

// ── 4. sbatch rejected → no registry write ─────────────────────────

TEST(ClusterEngineUp, SbatchRejectedLeavesRegistryEmpty) {
    Harness h;
    h.slurm.queue_sinfo({idle("p1", 1, {"gpu:a100:1"})});
    h.slurm.queue_sbatch(SubmitResult{/*jobid*/"", /*raw*/"sbatch: error: account not permitted"});

    UpSpec spec; spec.resource = "a100";
    auto r = h.engine().up(spec);
    auto* err = std::get_if<EngineError>(&r);
    ASSERT_NE(err, nullptr);
    EXPECT_NE(err->message.find("sbatch"), std::string::npos) << err->message;

    EXPECT_EQ(h.reg.allocations.size(), 0u);
    EXPECT_EQ(h.slurm.squeue_calls.size(), 0u);   // never polled
}

// ── 5. squeue polling transitions PD → R → success ─────────────────

TEST(ClusterEngineUp, PollingTransitionsFromPendingToRunning) {
    Harness h;
    h.slurm.queue_sinfo({idle("p1", 1, {"gpu:a100:1"})});
    h.slurm.queue_sbatch({"5005", ""});
    h.slurm.queue_squeue({JobState{"5005", "PD", "", ""}});
    h.slurm.queue_squeue({JobState{"5005", "PD", "", ""}});
    h.slurm.queue_squeue({JobState{"5005", "R", "n1", "04:00:00"}});

    UpSpec spec; spec.resource = "a100";
    auto r = h.engine().up(spec);
    auto* alloc = std::get_if<Allocation>(&r);
    ASSERT_NE(alloc, nullptr);
    EXPECT_EQ(alloc->state, AllocationState::Running);
    EXPECT_EQ(h.slurm.squeue_calls.size(), 3u);
}

// ── 6a. wait-timeout → user chooses 'c' → cancel + error ───────────

TEST(ClusterEngineUp, WaitTimeoutCancelScancelsAndReturnsError) {
    Harness h;
    h.slurm.queue_sinfo({idle("p1", 1, {"gpu:a100:1"})});
    h.slurm.queue_sbatch({"6006", ""});
    // Any number of PDs — fake clock will advance past the deadline.
    for (int i = 0; i < 20; ++i) {
        h.slurm.queue_squeue({JobState{"6006", "PD", "", ""}});
    }
    h.prompt.queue_answer('c');

    UpSpec spec;
    spec.resource     = "a100";
    spec.wait_timeout = std::chrono::seconds(1);

    auto r = h.engine().up(spec);
    auto* err = std::get_if<EngineError>(&r);
    ASSERT_NE(err, nullptr);
    EXPECT_NE(err->message.find("cancel"), std::string::npos) << err->message;

    ASSERT_EQ(h.slurm.scancel_calls.size(), 1u);
    EXPECT_EQ(h.slurm.scancel_calls[0].jobid, "6006");
    EXPECT_EQ(h.reg.allocations.size(), 0u);
}

// ── 6b. wait-timeout → user chooses 'd' → pending allocation returned ───

TEST(ClusterEngineUp, WaitTimeoutDetachReturnsPendingAllocation) {
    Harness h;
    h.slurm.queue_sinfo({idle("p1", 1, {"gpu:a100:1"})});
    h.slurm.queue_sbatch({"7007", ""});
    for (int i = 0; i < 20; ++i) {
        h.slurm.queue_squeue({JobState{"7007", "PD", "", ""}});
    }
    h.prompt.queue_answer('d');

    UpSpec spec; spec.resource = "a100"; spec.wait_timeout = std::chrono::seconds(1);
    auto r = h.engine().up(spec);
    auto* alloc = std::get_if<Allocation>(&r);
    ASSERT_NE(alloc, nullptr);
    EXPECT_EQ(alloc->state, AllocationState::Pending);
    EXPECT_EQ(alloc->jobid, "7007");
    ASSERT_EQ(h.reg.allocations.size(), 1u);
    EXPECT_EQ(h.reg.allocations[0].state, AllocationState::Pending);
    EXPECT_EQ(h.slurm.scancel_calls.size(), 0u);
}

// ── 7. --via forces a specific route ───────────────────────────────

TEST(ClusterEngineUp, ViaForcesSpecificRoute) {
    Harness h;
    // If --via works correctly, we should NOT probe c1 — only c2.
    h.slurm.queue_sinfo({idle("p2", 1, {"gpu:a100:1"})});
    h.slurm.queue_sbatch({"8008", ""});
    h.slurm.queue_squeue({JobState{"8008", "R", "n2", "04:00:00"}});

    UpSpec spec; spec.resource = "a100"; spec.via = "c2";
    auto r = h.engine().up(spec);
    auto* alloc = std::get_if<Allocation>(&r);
    ASSERT_NE(alloc, nullptr);
    EXPECT_EQ(alloc->cluster, "c2");

    // sinfo must have been called exactly once, against c2
    ASSERT_EQ(h.slurm.sinfo_calls.size(), 1u);
    EXPECT_EQ(h.slurm.sinfo_calls[0].cluster, "c2");
}

TEST(ClusterEngineUp, ViaWithUnknownClusterFails) {
    Harness h;
    UpSpec spec; spec.resource = "a100"; spec.via = "no-such-cluster";

    auto r = h.engine().up(spec);
    auto* err = std::get_if<EngineError>(&r);
    ASSERT_NE(err, nullptr);
    EXPECT_NE(err->message.find("no-such-cluster"), std::string::npos) << err->message;
}

// ── 8. invalid resource name → error ───────────────────────────────

TEST(ClusterEngineUp, InvalidResourceNameIsRejected) {
    Harness h;
    UpSpec spec; spec.resource = "h100";    // not in config
    auto r = h.engine().up(spec);
    auto* err = std::get_if<EngineError>(&r);
    ASSERT_NE(err, nullptr);
    EXPECT_NE(err->message.find("h100"), std::string::npos) << err->message;
}

// ── 9. user-supplied time/cpus/mem override resource defaults ──────

TEST(ClusterEngineUp, UserSpecOverridesResourceDefaults) {
    Harness h;
    h.slurm.queue_sinfo({idle("p1", 1, {"gpu:a100:1"})});
    h.slurm.queue_sbatch({"9009", ""});
    h.slurm.queue_squeue({JobState{"9009", "R", "n1", "02:00:00"}});

    UpSpec spec;
    spec.resource = "a100";
    spec.time     = "02:00:00";
    spec.cpus     = 16;
    spec.mem      = "128G";

    auto r = h.engine().up(spec);
    ASSERT_NE(std::get_if<Allocation>(&r), nullptr);
    ASSERT_EQ(h.slurm.sbatch_calls.size(), 1u);
    EXPECT_EQ(h.slurm.sbatch_calls[0].spec.time, "02:00:00");
    EXPECT_EQ(h.slurm.sbatch_calls[0].spec.cpus, 16);
    EXPECT_EQ(h.slurm.sbatch_calls[0].spec.mem,  "128G");
}
