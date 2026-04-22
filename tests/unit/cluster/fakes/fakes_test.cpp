// Sanity tests for the FakeSshClient / FakeSlurmOps / FakeTmuxOps /
// FakeNotifier — test doubles that later cluster-engine tests rely on.
// Each fake should:
//   - record every invocation in a public vector
//   - pull return values from a FIFO queue
//   - survive reset() wiping state clean

#include <gtest/gtest.h>

#include "fake_ssh_client.h"
#include "fake_slurm_ops.h"
#include "fake_tmux_ops.h"
#include "fake_notifier.h"

using namespace tash::cluster;
using namespace tash::cluster::testing;

// ══════════════════════════════════════════════════════════════════════════════
// FakeSshClient
// ══════════════════════════════════════════════════════════════════════════════

TEST(FakeSshClient, RecordsInvocations) {
    FakeSshClient ssh;
    ssh.run("c1", {"squeue", "-u", "me"}, std::chrono::milliseconds(500));
    ssh.run("c2", {"echo", "hi"},          std::chrono::milliseconds(100));

    ASSERT_EQ(ssh.run_calls.size(), 2u);
    EXPECT_EQ(ssh.run_calls[0].cluster,     "c1");
    EXPECT_EQ(ssh.run_calls[0].argv.front(), "squeue");
    EXPECT_EQ(ssh.run_calls[0].timeout.count(), 500);
    EXPECT_EQ(ssh.run_calls[1].cluster,     "c2");
}

TEST(FakeSshClient, ReturnsQueuedResultsFifo) {
    FakeSshClient ssh;
    SshResult a; a.exit_code = 0; a.out = "first";
    SshResult b; b.exit_code = 1; b.err = "second";
    ssh.queue_run(a);
    ssh.queue_run(b);

    EXPECT_EQ(ssh.run("c1", {"x"}, std::chrono::milliseconds(0)).out, "first");
    EXPECT_EQ(ssh.run("c1", {"x"}, std::chrono::milliseconds(0)).err, "second");
    // Empty queue → default SshResult (exit_code=0, empty out/err)
    EXPECT_EQ(ssh.run("c1", {"x"}, std::chrono::milliseconds(0)).exit_code, 0);
}

TEST(FakeSshClient, ConnectAndDisconnectToggleMasterState) {
    FakeSshClient ssh;
    EXPECT_FALSE(ssh.master_alive("u"));

    ssh.connect("u");
    EXPECT_TRUE(ssh.master_alive("u"));
    EXPECT_EQ(ssh.connect_calls, (std::vector<std::string>{"u"}));

    ssh.disconnect("u");
    EXPECT_FALSE(ssh.master_alive("u"));
    EXPECT_EQ(ssh.disconnect_calls, (std::vector<std::string>{"u"}));
}

TEST(FakeSshClient, SetMasterAliveWithoutConnectCallWorks) {
    FakeSshClient ssh;
    ssh.set_master_alive("preloaded", true);
    EXPECT_TRUE(ssh.master_alive("preloaded"));
    EXPECT_TRUE(ssh.connect_calls.empty());
}

TEST(FakeSshClient, ResetClearsEverything) {
    FakeSshClient ssh;
    ssh.queue_run(SshResult{});
    ssh.run("c", {"x"}, std::chrono::milliseconds(0));
    ssh.connect("c");
    ssh.reset();

    EXPECT_TRUE(ssh.run_calls.empty());
    EXPECT_TRUE(ssh.connect_calls.empty());
    EXPECT_TRUE(ssh.run_queue.empty());
    EXPECT_FALSE(ssh.master_alive("c"));
}

// ══════════════════════════════════════════════════════════════════════════════
// FakeSlurmOps
// ══════════════════════════════════════════════════════════════════════════════

TEST(FakeSlurmOps, SbatchRecordsSpecAndReturnsQueued) {
    FakeSlurmOps slurm;
    FakeSshClient ssh;

    SubmitResult ret; ret.jobid = "12345"; ret.raw_stdout = "Submitted batch job 12345";
    slurm.queue_sbatch(ret);

    SubmitSpec spec;
    spec.cluster = "u"; spec.account = "acc"; spec.partition = "p";
    auto result = slurm.sbatch(spec, ssh);

    EXPECT_EQ(result.jobid, "12345");
    ASSERT_EQ(slurm.sbatch_calls.size(), 1u);
    EXPECT_EQ(slurm.sbatch_calls[0].spec.cluster, "u");
    EXPECT_EQ(slurm.sbatch_calls[0].spec.partition, "p");
}

TEST(FakeSlurmOps, SqueueFifoSequence) {
    FakeSlurmOps slurm;
    FakeSshClient ssh;

    slurm.queue_squeue({JobState{"1", "PD", "",  ""}});
    slurm.queue_squeue({JobState{"1", "R",  "n1", "01:00:00"}});
    slurm.queue_squeue({});
    slurm.queue_squeue_fail();   // probe failure — distinguishable from empty

    auto r1 = slurm.squeue("u", ssh);
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(r1->at(0).state, "PD");

    auto r2 = slurm.squeue("u", ssh);
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r2->at(0).state, "R");

    auto r3 = slurm.squeue("u", ssh);
    ASSERT_TRUE(r3.has_value());
    EXPECT_TRUE(r3->empty());

    auto r4 = slurm.squeue("u", ssh);
    EXPECT_FALSE(r4.has_value()) << "queue_squeue_fail should produce nullopt";

    ASSERT_EQ(slurm.squeue_calls.size(), 4u);
    EXPECT_EQ(slurm.squeue_calls[0].cluster, "u");
}

TEST(FakeSlurmOps, SinfoAndScancelRecorded) {
    FakeSlurmOps slurm;
    FakeSshClient ssh;

    slurm.queue_sinfo({PartitionState{"p1", "up", 3, {"gpu:a100:1"}}});
    auto ps = slurm.sinfo("u", "p1", ssh);
    ASSERT_TRUE(ps.has_value());
    ASSERT_EQ(ps->size(), 1u);
    EXPECT_EQ((*ps)[0].idle_nodes, 3);

    slurm.queue_sinfo_fail();
    auto ps_fail = slurm.sinfo("u", "p1", ssh);
    EXPECT_FALSE(ps_fail.has_value())
        << "queue_sinfo_fail should surface a probe failure, not an empty vector";

    slurm.scancel("u", "9999", ssh);
    ASSERT_EQ(slurm.scancel_calls.size(), 1u);
    EXPECT_EQ(slurm.scancel_calls[0].jobid, "9999");
}

// ══════════════════════════════════════════════════════════════════════════════
// FakeTmuxOps
// ══════════════════════════════════════════════════════════════════════════════

TEST(FakeTmuxOps, RecordsSessionAndWindowCalls) {
    FakeTmuxOps tmux;
    FakeSshClient ssh;

    RemoteTarget rt{"u", "notch1"};
    tmux.new_session(rt, "s", "/cwd", ssh);
    tmux.new_window (rt, "s", "w1", "/cwd", "claude", ssh);
    tmux.new_window (rt, "s", "w2", "/cwd", "python train.py", ssh);

    ASSERT_EQ(tmux.new_session_calls.size(), 1u);
    EXPECT_EQ(tmux.new_session_calls[0].session, "s");
    ASSERT_EQ(tmux.new_window_calls.size(), 2u);
    EXPECT_EQ(tmux.new_window_calls[1].cmd, "python train.py");
}

TEST(FakeTmuxOps, ListSessionsFifo) {
    FakeTmuxOps tmux;
    FakeSshClient ssh;

    tmux.queue_list_sessions({SessionInfo{"a", 1, false}, SessionInfo{"b", 2, true}});
    auto s = tmux.list_sessions(RemoteTarget{"u", "n"}, ssh);
    ASSERT_EQ(s.size(), 2u);
    EXPECT_EQ(s[0].name, "a");
    EXPECT_EQ(s[1].attached, true);

    EXPECT_TRUE(tmux.list_sessions(RemoteTarget{"u", "n"}, ssh).empty());
}

TEST(FakeTmuxOps, KillAndExecAttachRecorded) {
    FakeTmuxOps tmux;
    FakeSshClient ssh;
    RemoteTarget rt{"u", "n"};
    tmux.kill_window(rt, "s", "w", ssh);
    tmux.exec_attach(rt, "s", "w");

    ASSERT_EQ(tmux.kill_window_calls.size(), 1u);
    EXPECT_EQ(tmux.kill_window_calls[0].window, "w");
    ASSERT_EQ(tmux.exec_attach_calls.size(), 1u);
    EXPECT_EQ(tmux.exec_attach_calls[0].session, "s");
}

// ══════════════════════════════════════════════════════════════════════════════
// FakeNotifier
// ══════════════════════════════════════════════════════════════════════════════

TEST(FakeNotifier, RecordsDesktopAndBell) {
    FakeNotifier n;
    n.desktop("title-1", "body-1");
    n.desktop("title-2", "body-2");
    n.bell();
    n.bell();
    n.bell();

    ASSERT_EQ(n.desktop_calls.size(), 2u);
    EXPECT_EQ(n.desktop_calls[0].title, "title-1");
    EXPECT_EQ(n.desktop_calls[1].body,  "body-2");
    EXPECT_EQ(n.bell_count, 3);
}

TEST(FakeNotifier, ResetClears) {
    FakeNotifier n;
    n.desktop("t", "b");
    n.bell();
    n.reset();
    EXPECT_TRUE(n.desktop_calls.empty());
    EXPECT_EQ(n.bell_count, 0);
}
