// Tests for ClusterEngine::doctor. The doctor runs three checks per
// cluster:
//   1. SSH reach — ssh returns exit 0 on a trivial remote command
//   2. sbatch presence — `which sbatch` returns a path
//   3. tmux presence   — `which tmux` returns a path
// Results are OK / WARN / FAIL with a one-line hint.

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

Config one_cluster() {
    Config c;
    c.clusters.push_back({"c1", "ssh-c1", ""});
    return c;
}

Config two_clusters() {
    Config c;
    c.clusters.push_back({"c1", "ssh-c1", ""});
    c.clusters.push_back({"c2", "ssh-c2", ""});
    return c;
}

struct Harness {
    Config         cfg;
    Registry       reg;
    FakeSshClient  ssh;
    FakeSlurmOps   slurm;
    FakeTmuxOps    tmux;
    FakeNotifier   notify;
    FakePrompt     prompt;
    FakeClock      clock;
    ClusterEngine  engine;

    explicit Harness(Config c)
        : cfg(std::move(c)),
          engine(cfg, reg, ssh, slurm, tmux, notify, prompt, clock) {}
};

SshResult ok(std::string out) {
    SshResult r; r.exit_code = 0; r.out = std::move(out); return r;
}
SshResult fail(int code = 255) {
    SshResult r; r.exit_code = code; return r;
}

const ClusterEngine::DoctorCheck* find_check(
    const ClusterEngine::DoctorReport::ClusterBlock& blk,
    const std::string& needle) {
    for (const auto& c : blk.checks) {
        if (c.name.find(needle) != std::string::npos) return &c;
    }
    return nullptr;
}

}  // namespace

// ══════════════════════════════════════════════════════════════════════════════
// Happy path + filters
// ══════════════════════════════════════════════════════════════════════════════

TEST(ClusterEngineDoctor, AllChecksOkOnReachableCluster) {
    Harness h(one_cluster());
    // Three queued ssh results in order: reach, sbatch, tmux.
    h.ssh.queue_run(ok(""));                // reach (`true`)
    h.ssh.queue_run(ok("/usr/bin/sbatch\n")); // which sbatch
    h.ssh.queue_run(ok("/usr/bin/tmux\n"));   // which tmux

    auto r = h.engine.doctor({});
    const auto* rep = std::get_if<ClusterEngine::DoctorReport>(&r);
    ASSERT_NE(rep, nullptr);
    ASSERT_EQ(rep->clusters.size(), 1u);

    const auto& blk = rep->clusters[0];
    EXPECT_EQ(blk.cluster, "c1");
    ASSERT_EQ(blk.checks.size(), 3u);
    for (const auto& c : blk.checks) {
        EXPECT_EQ(c.level, ClusterEngine::DoctorCheck::OK) << c.name << ": " << c.message;
    }
}

TEST(ClusterEngineDoctor, UnreachableClusterFailsAndSkipsRemainder) {
    Harness h(one_cluster());
    h.ssh.queue_run(fail(255));   // reach fails — subsequent checks skipped

    auto r = h.engine.doctor({});
    const auto* rep = std::get_if<ClusterEngine::DoctorReport>(&r);
    ASSERT_NE(rep, nullptr);
    const auto& blk = rep->clusters[0];
    const auto* reach = find_check(blk, "SSH");
    ASSERT_NE(reach, nullptr);
    EXPECT_EQ(reach->level, ClusterEngine::DoctorCheck::FAIL);
    EXPECT_NE(reach->message.find("ssh-c1"), std::string::npos);
    // Only the reach check runs; sbatch + tmux are marked as skipped
    // or absent — either way they're not OK.
    for (const auto& c : blk.checks) {
        if (&c == reach) continue;
        EXPECT_NE(c.level, ClusterEngine::DoctorCheck::OK) << c.name;
    }
}

TEST(ClusterEngineDoctor, MissingSbatchWarns) {
    Harness h(one_cluster());
    h.ssh.queue_run(ok(""));               // reach OK
    h.ssh.queue_run(fail(1));              // which sbatch — not found
    h.ssh.queue_run(ok("/usr/bin/tmux\n"));

    auto r = h.engine.doctor({});
    const auto& blk = std::get<ClusterEngine::DoctorReport>(r).clusters[0];
    const auto* s = find_check(blk, "sbatch");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->level, ClusterEngine::DoctorCheck::WARN);
    EXPECT_NE(s->message.find("sbatch"), std::string::npos);
}

TEST(ClusterEngineDoctor, MissingTmuxWarns) {
    Harness h(one_cluster());
    h.ssh.queue_run(ok(""));
    h.ssh.queue_run(ok("/usr/bin/sbatch\n"));
    h.ssh.queue_run(fail(1));              // which tmux — not found

    auto r = h.engine.doctor({});
    const auto& blk = std::get<ClusterEngine::DoctorReport>(r).clusters[0];
    const auto* t = find_check(blk, "tmux");
    ASSERT_NE(t, nullptr);
    EXPECT_EQ(t->level, ClusterEngine::DoctorCheck::WARN);
}

TEST(ClusterEngineDoctor, EmptyStdoutFromWhichCountsAsMissing) {
    Harness h(one_cluster());
    h.ssh.queue_run(ok(""));               // reach OK
    h.ssh.queue_run(ok(""));               // which sbatch — exit 0 but no path
    h.ssh.queue_run(ok(""));               // which tmux   — same

    auto r = h.engine.doctor({});
    const auto& blk = std::get<ClusterEngine::DoctorReport>(r).clusters[0];
    EXPECT_EQ(find_check(blk, "sbatch")->level, ClusterEngine::DoctorCheck::WARN);
    EXPECT_EQ(find_check(blk, "tmux")  ->level, ClusterEngine::DoctorCheck::WARN);
}

// ══════════════════════════════════════════════════════════════════════════════
// Multi-cluster + --cluster filter
// ══════════════════════════════════════════════════════════════════════════════

TEST(ClusterEngineDoctor, RunsAcrossAllClustersByDefault) {
    Harness h(two_clusters());
    // c1: all OK
    h.ssh.queue_run(ok(""));
    h.ssh.queue_run(ok("/b/sbatch\n"));
    h.ssh.queue_run(ok("/b/tmux\n"));
    // c2: reach fails
    h.ssh.queue_run(fail(255));

    auto r = h.engine.doctor({});
    const auto& rep = std::get<ClusterEngine::DoctorReport>(r);
    ASSERT_EQ(rep.clusters.size(), 2u);
    EXPECT_EQ(rep.clusters[0].cluster, "c1");
    EXPECT_EQ(rep.clusters[1].cluster, "c2");
}

TEST(ClusterEngineDoctor, ClusterFilterRunsOnlyThatOne) {
    Harness h(two_clusters());
    h.ssh.queue_run(ok(""));
    h.ssh.queue_run(ok("/b/sbatch\n"));
    h.ssh.queue_run(ok("/b/tmux\n"));

    ClusterEngine::DoctorSpec ds; ds.cluster = "c2";
    auto r = h.engine.doctor(ds);
    const auto& rep = std::get<ClusterEngine::DoctorReport>(r);
    ASSERT_EQ(rep.clusters.size(), 1u);
    EXPECT_EQ(rep.clusters[0].cluster, "c2");
}

TEST(ClusterEngineDoctor, UnknownFilterClusterIsAnError) {
    Harness h(two_clusters());
    ClusterEngine::DoctorSpec ds; ds.cluster = "nope";
    auto r = h.engine.doctor(ds);
    const auto* err = std::get_if<EngineError>(&r);
    ASSERT_NE(err, nullptr);
    EXPECT_NE(err->message.find("nope"), std::string::npos) << err->message;
}
