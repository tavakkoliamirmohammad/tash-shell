// Tests for tash::cluster::Registry.
//
// Each test gets a fresh temp directory for the registry.json + .lock +
// .bak.* sidecars, torn down in TearDown.

#include <gtest/gtest.h>

#include "tash/cluster/registry.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

using namespace tash::cluster;

namespace {

std::filesystem::path make_temp_dir() {
    const auto tag = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    auto d = std::filesystem::temp_directory_path() /
             ("tash_cluster_registry_test_" + tag);
    std::filesystem::create_directories(d);
    return d;
}

Allocation make_alloc(std::string cluster, std::string jobid,
                       AllocationState st = AllocationState::Running) {
    Allocation a;
    a.id      = cluster + ":" + jobid;
    a.cluster = std::move(cluster);
    a.jobid   = std::move(jobid);
    a.state   = st;
    return a;
}

}  // namespace

class RegistryTest : public ::testing::Test {
protected:
    std::filesystem::path dir;
    std::filesystem::path path;

    void SetUp() override {
        dir  = make_temp_dir();
        path = dir / "registry.json";
    }
    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

// ── 1. empty-registry round-trip  ───────────────────────────────────

TEST_F(RegistryTest, LoadFromNonexistentFileIsEmpty) {
    Registry r = Registry::load(path);
    EXPECT_EQ(r.schema_version, 1);
    EXPECT_EQ(r.allocations.size(), 0u);
}

TEST_F(RegistryTest, SaveThenLoadEmptyRoundTrip) {
    Registry r;
    r.save(path);
    EXPECT_TRUE(std::filesystem::exists(path));
    Registry r2 = Registry::load(path);
    EXPECT_EQ(r2.schema_version, 1);
    EXPECT_EQ(r2.allocations.size(), 0u);
}

// ── 2. allocation add/remove/find  ──────────────────────────────────

TEST_F(RegistryTest, AddAndFindAllocation) {
    Registry r;
    r.add_allocation(make_alloc("c1", "42"));
    ASSERT_EQ(r.allocations.size(), 1u);
    auto* a = r.find_allocation("c1:42");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->jobid, "42");
    EXPECT_EQ(a->cluster, "c1");
}

TEST_F(RegistryTest, RemoveAllocationReturnsTrueOnlyWhenPresent) {
    Registry r;
    r.add_allocation(make_alloc("c1", "42"));
    r.add_allocation(make_alloc("c2", "99"));
    EXPECT_TRUE(r.remove_allocation("c1:42"));
    EXPECT_EQ(r.allocations.size(), 1u);
    EXPECT_EQ(r.allocations[0].id, "c2:99");
    EXPECT_FALSE(r.remove_allocation("missing:0"));
}

// ── 3. workspace add/remove  ────────────────────────────────────────

TEST_F(RegistryTest, AddAndRemoveWorkspaceScoped) {
    Registry r;
    r.add_allocation(make_alloc("c1", "42"));

    Workspace ws;
    ws.name         = "repoA";
    ws.cwd          = "/scratch/x";
    ws.tmux_session = "tash-c1-42-repoA";
    EXPECT_TRUE(r.add_workspace("c1:42", std::move(ws)));
    EXPECT_FALSE(r.add_workspace("missing", Workspace{}));

    auto* a = r.find_allocation("c1:42");
    ASSERT_NE(a, nullptr);
    ASSERT_EQ(a->workspaces.size(), 1u);
    EXPECT_EQ(a->workspaces[0].name, "repoA");

    EXPECT_TRUE (r.remove_workspace("c1:42", "repoA"));
    EXPECT_FALSE(r.remove_workspace("c1:42", "repoA"));   // idempotent removal
    EXPECT_EQ(a->workspaces.size(), 0u);
}

// ── 4. instance add/remove  ─────────────────────────────────────────

TEST_F(RegistryTest, AddAndRemoveInstanceScoped) {
    Registry r;
    r.add_allocation(make_alloc("c1", "42"));
    Workspace ws;
    ws.name = "repoA";
    r.add_workspace("c1:42", std::move(ws));

    Instance inst;
    inst.id          = "1";
    inst.tmux_window = "1";
    EXPECT_TRUE(r.add_instance("c1:42", "repoA", std::move(inst)));
    EXPECT_FALSE(r.add_instance("c1:42", "missing", Instance{}));
    EXPECT_FALSE(r.add_instance("nope:1", "repoA",  Instance{}));

    auto* a = r.find_allocation("c1:42");
    ASSERT_NE(a, nullptr);
    ASSERT_EQ(a->workspaces[0].instances.size(), 1u);

    EXPECT_TRUE (r.remove_instance("c1:42", "repoA", "1"));
    EXPECT_FALSE(r.remove_instance("c1:42", "repoA", "1"));
    EXPECT_EQ(a->workspaces[0].instances.size(), 0u);
}

// ── 5. complex-state round-trip  ────────────────────────────────────

TEST_F(RegistryTest, ComplexStateSurvivesSaveLoadRoundTrip) {
    Registry r;
    auto a1 = make_alloc("utah-n", "123");
    a1.node         = "notch5";
    a1.submitted_at = "2026-04-18T10:00:00Z";
    a1.started_at   = "2026-04-18T10:00:05Z";
    a1.end_by       = "2026-04-18T14:00:05Z";
    a1.resource     = "a100";
    r.add_allocation(a1);

    Workspace ws;
    ws.name         = "repoA";
    ws.cwd          = "/scratch/repoA";
    ws.tmux_session = "s1";
    r.add_workspace("utah-n:123", std::move(ws));

    Instance inst;
    inst.id            = "1";
    inst.tmux_window   = "1";
    inst.name          = "featureX";
    inst.state         = InstanceState::Idle;
    inst.last_event_at = "2026-04-18T10:30:00Z";
    inst.pid           = 4211;
    r.add_instance("utah-n:123", "repoA", std::move(inst));

    r.save(path);
    Registry r2 = Registry::load(path);

    ASSERT_EQ(r2.allocations.size(), 1u);
    const auto& a = r2.allocations[0];
    EXPECT_EQ(a.cluster,      "utah-n");
    EXPECT_EQ(a.node,         "notch5");
    EXPECT_EQ(a.resource,     "a100");
    EXPECT_EQ(a.submitted_at, "2026-04-18T10:00:00Z");
    EXPECT_EQ(a.started_at,   "2026-04-18T10:00:05Z");
    EXPECT_EQ(a.end_by,       "2026-04-18T14:00:05Z");
    EXPECT_EQ(a.state,        AllocationState::Running);

    ASSERT_EQ(a.workspaces.size(), 1u);
    const auto& w = a.workspaces[0];
    EXPECT_EQ(w.name,         "repoA");
    EXPECT_EQ(w.tmux_session, "s1");

    ASSERT_EQ(w.instances.size(), 1u);
    const auto& ii = w.instances[0];
    EXPECT_EQ(ii.id,                   "1");
    EXPECT_EQ(ii.name.value_or(""),    "featureX");
    EXPECT_EQ(ii.state,                InstanceState::Idle);
    EXPECT_EQ(ii.last_event_at,        "2026-04-18T10:30:00Z");
    ASSERT_TRUE(ii.pid.has_value());
    EXPECT_EQ(*ii.pid, 4211);
}

// ── 6. reconcile drops ghost jobs  ──────────────────────────────────

TEST_F(RegistryTest, ReconcileMarksGhostJobsEnded) {
    Registry r;
    r.add_allocation(make_alloc("c1", "100"));
    r.add_allocation(make_alloc("c1", "200"));

    std::vector<JobState> snapshot;
    JobState js;
    js.jobid = "100"; js.state = "R"; js.node = "n1";
    snapshot.push_back(js);

    int transitioned = r.reconcile("c1", snapshot);
    EXPECT_EQ(transitioned, 1);
    EXPECT_EQ(r.find_allocation("c1:100")->state, AllocationState::Running);
    EXPECT_EQ(r.find_allocation("c1:200")->state, AllocationState::Ended);
}

// ── 7. reconcile doesn't touch other clusters  ──────────────────────

TEST_F(RegistryTest, ReconcileIgnoresOtherClusters) {
    Registry r;
    r.add_allocation(make_alloc("c1", "100"));
    r.add_allocation(make_alloc("c2", "200"));

    int transitioned = r.reconcile("c1", /*empty*/ {});
    EXPECT_EQ(transitioned, 1);
    EXPECT_EQ(r.find_allocation("c1:100")->state, AllocationState::Ended);
    EXPECT_EQ(r.find_allocation("c2:200")->state, AllocationState::Running);
}

// ── 8. reconcile is idempotent on already-ended allocations  ────────

TEST_F(RegistryTest, ReconcileIdempotentOnEnded) {
    Registry r;
    r.add_allocation(make_alloc("c1", "100", AllocationState::Ended));

    int transitioned = r.reconcile("c1", {});
    EXPECT_EQ(transitioned, 0);
    EXPECT_EQ(r.find_allocation("c1:100")->state, AllocationState::Ended);
}

// ── 9. corrupt file → back up + empty registry  ─────────────────────

TEST_F(RegistryTest, CorruptFileRenamedToBakAndLoadsEmpty) {
    std::ofstream(path) << "this is not json {{{{{";
    Registry r = Registry::load(path);
    EXPECT_EQ(r.allocations.size(), 0u);

    bool found_bak = false;
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        const auto name = e.path().filename().string();
        if (name.rfind("registry.json.bak.", 0) == 0) { found_bak = true; break; }
    }
    EXPECT_TRUE(found_bak);
}

// ── 10. schema v1 identity pass  ────────────────────────────────────

TEST_F(RegistryTest, SchemaV1LoadIdentityPass) {
    // Handwritten v1 JSON — proves load parses every enum properly.
    const std::string src = R"({
        "schema_version": 1,
        "allocations": [
            {
                "id": "c1:5",
                "cluster": "c1",
                "jobid": "5",
                "resource": "a100",
                "node": "n1",
                "submitted_at": "2026-04-18T00:00:00Z",
                "started_at": "2026-04-18T00:00:01Z",
                "end_by": "2026-04-18T04:00:00Z",
                "state": "pending",
                "workspaces": [
                    {
                        "name": "w",
                        "cwd": "/tmp/w",
                        "tmux_session": "s",
                        "instances": [
                            {"id":"1","name":null,"tmux_window":"1","pid":null,"state":"running","last_event_at":""},
                            {"id":"2","name":"named","tmux_window":"named","pid":777,"state":"crashed","last_event_at":""}
                        ]
                    }
                ]
            }
        ]
    })";
    std::ofstream(path) << src;

    Registry r = Registry::load(path);
    ASSERT_EQ(r.allocations.size(), 1u);
    EXPECT_EQ(r.schema_version, 1);
    EXPECT_EQ(r.allocations[0].state, AllocationState::Pending);

    const auto& inst = r.allocations[0].workspaces[0].instances;
    ASSERT_EQ(inst.size(), 2u);
    EXPECT_FALSE(inst[0].name.has_value());
    EXPECT_FALSE(inst[0].pid.has_value());
    EXPECT_EQ(inst[0].state, InstanceState::Running);
    EXPECT_EQ(inst[1].state, InstanceState::Crashed);
    EXPECT_EQ(inst[1].name.value_or(""), "named");
    ASSERT_TRUE(inst[1].pid.has_value());
    EXPECT_EQ(*inst[1].pid, 777);
}

// ── 11. lock scope holds and releases  ──────────────────────────────

TEST_F(RegistryTest, LockScopeHoldsAndReleasesLockfile) {
    const auto lockpath = dir / "registry.lock";
    {
        Registry::LockScope lk(lockpath);
        EXPECT_TRUE(lk.locked());
        EXPECT_TRUE(std::filesystem::exists(lockpath));
    }
    // Lockfile may persist as a semaphore, but the lock itself must be
    // released — a fresh scope must acquire successfully.
    Registry::LockScope lk2(lockpath);
    EXPECT_TRUE(lk2.locked());
}

// ── 12. lock_scope convenience chooses the right sidecar path  ──────

TEST_F(RegistryTest, LockScopeConvenienceUsesLockSidecar) {
    auto lk = Registry::lock_scope(path);
    EXPECT_TRUE(lk.locked());
    EXPECT_TRUE(std::filesystem::exists(
        std::filesystem::path(path).concat(".lock")));
}
