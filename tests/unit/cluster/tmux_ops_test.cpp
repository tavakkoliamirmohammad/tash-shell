// Tests for tash::cluster::tmux_compose (pure helpers) and the
// stateful TmuxOpsReal built on top.
//
// TASH_CLUSTER_RECORDINGS_DIR is injected by the build system.

#include <gtest/gtest.h>

#include "tash/cluster/tmux_compose.h"
#include "tash/cluster/tmux_ops.h"

#include "fakes/fake_ssh_client.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#ifndef TASH_CLUSTER_RECORDINGS_DIR
#error "TASH_CLUSTER_RECORDINGS_DIR must be defined by the build system"
#endif

using namespace tash::cluster;
using namespace tash::cluster::testing;
using namespace tash::cluster::tmux_compose;

namespace {

std::string read_fixture(const char* relpath) {
    std::ifstream f(std::filesystem::path(TASH_CLUSTER_RECORDINGS_DIR) / relpath);
    std::ostringstream b; b << f.rdbuf(); return b.str();
}

}  // namespace

// ══════════════════════════════════════════════════════════════════════════════
// shell_quote
// ══════════════════════════════════════════════════════════════════════════════

TEST(ShellQuote, PlainStringIsSingleQuoted) {
    EXPECT_EQ(shell_quote("hello"),  "'hello'");
}

TEST(ShellQuote, EmptyStringIsEmptyQuotes) {
    EXPECT_EQ(shell_quote(""), "''");
}

TEST(ShellQuote, SpacesArePreservedInsideQuotes) {
    EXPECT_EQ(shell_quote("hello world"), "'hello world'");
}

TEST(ShellQuote, SingleQuoteIsEscapedCanonical) {
    // end-quote, escaped quote, start-quote: '\''
    EXPECT_EQ(shell_quote("it's"), "'it'\\''s'");
    EXPECT_EQ(shell_quote("'"),    "''\\'''");
}

TEST(ShellQuote, DangerousCharactersAreInert) {
    EXPECT_EQ(shell_quote("$(rm -rf)"),   "'$(rm -rf)'");
    EXPECT_EQ(shell_quote("`backtick`"),  "'`backtick`'");
    EXPECT_EQ(shell_quote("a;b|c&d"),      "'a;b|c&d'");
    EXPECT_EQ(shell_quote("a\"b"),         "'a\"b'");
    EXPECT_EQ(shell_quote("line1\nline2"), "'line1\nline2'");
}

// ══════════════════════════════════════════════════════════════════════════════
// tmux command builders
// ══════════════════════════════════════════════════════════════════════════════

TEST(TmuxCommand, NewSessionIsDetachedAndShellQuoted) {
    const auto s = tmux_new_session("my sess", "/scratch/my repo");
    EXPECT_NE(s.find("tmux new-session"),  std::string::npos) << s;
    EXPECT_NE(s.find("-d"),                std::string::npos);
    EXPECT_NE(s.find("-s 'my sess'"),      std::string::npos);
    EXPECT_NE(s.find("-c '/scratch/my repo'"), std::string::npos);
}

TEST(TmuxCommand, NewWindowTargetsSessionAndNamesWindow) {
    const auto s = tmux_new_window("sess", "w1", "/cwd", "python train.py");
    EXPECT_NE(s.find("tmux new-window"),         std::string::npos) << s;
    EXPECT_NE(s.find("-t 'sess'"),               std::string::npos);
    EXPECT_NE(s.find("-n 'w1'"),                 std::string::npos);
    EXPECT_NE(s.find("-c '/cwd'"),               std::string::npos);
    EXPECT_NE(s.find("'python train.py'"),       std::string::npos);
}

TEST(TmuxCommand, NewWindowEscapesSingleQuotesInCommand) {
    const auto s = tmux_new_window("sess", "w", "/cwd", R"(echo "can't")");
    // Command with a ' must round-trip via the canonical '\''.
    EXPECT_NE(s.find("'echo \"can'\\''t\"'"), std::string::npos) << s;
}

TEST(TmuxCommand, ListSessionsUsesFormat) {
    const auto s = tmux_list_sessions();
    EXPECT_NE(s.find("tmux list-sessions"),  std::string::npos) << s;
    EXPECT_NE(s.find("-F"),                  std::string::npos);
    EXPECT_NE(s.find("#{session_name}"),     std::string::npos);
    EXPECT_NE(s.find("#{session_windows}"),  std::string::npos);
    EXPECT_NE(s.find("#{?session_attached"), std::string::npos);
}

TEST(TmuxCommand, ListWindowsTargetsSessionWithFormat) {
    const auto s = tmux_list_windows("sess");
    EXPECT_NE(s.find("tmux list-windows"),   std::string::npos) << s;
    EXPECT_NE(s.find("-t 'sess'"),           std::string::npos);
    EXPECT_NE(s.find("#{window_name}"),      std::string::npos);
    EXPECT_NE(s.find("#{pane_pid}"),         std::string::npos);
}

TEST(TmuxCommand, KillWindowAddressesWindowWithSessionColon) {
    const auto s = tmux_kill_window("sess", "w1");
    EXPECT_NE(s.find("tmux kill-window"),  std::string::npos) << s;
    EXPECT_NE(s.find("-t 'sess:w1'"),      std::string::npos);
}

TEST(TmuxCommand, IsAliveQueriesSingleWindow) {
    const auto s = tmux_is_alive("sess", "w1");
    EXPECT_NE(s.find("tmux list-windows"),  std::string::npos) << s;
    EXPECT_NE(s.find("-t 'sess:w1'"),       std::string::npos);
    EXPECT_NE(s.find("#{pane_pid}"),        std::string::npos);
}

// ══════════════════════════════════════════════════════════════════════════════
// compose_remote_cmd — handles optional compute-node hop
// ══════════════════════════════════════════════════════════════════════════════

TEST(ComposeRemoteCmd, EmptyNodePassesThrough) {
    RemoteTarget t{"c1", /*node*/""};
    EXPECT_EQ(compose_remote_cmd(t, "tmux foo"), "tmux foo");
}

TEST(ComposeRemoteCmd, NodeWrapsWithSshAndShellQuotesEverything) {
    RemoteTarget t{"c1", "notch123", /*jobid*/""};
    const auto c = compose_remote_cmd(t, "tmux new-session -d");
    EXPECT_NE(c.find("ssh 'notch123' 'tmux new-session -d'"), std::string::npos) << c;
}

TEST(ComposeRemoteCmd, JobidPrefersSrunOverSshNodeHop) {
    // When both jobid and node are set, jobid wins — srun works on
    // any SLURM cluster, ssh-to-compute is site-policy-dependent.
    RemoteTarget t{"c1", "notch123", /*jobid*/"1153518"};
    const auto c = compose_remote_cmd(t, "tmux new-session -d");
    EXPECT_NE(c.find("srun --jobid='1153518' --overlap bash -c "),
              std::string::npos) << c;
    EXPECT_NE(c.find("'tmux new-session -d'"), std::string::npos) << c;
    // ssh-to-node must NOT appear when jobid provides the hop.
    EXPECT_EQ(c.find("ssh 'notch123'"), std::string::npos) << c;
}

// ══════════════════════════════════════════════════════════════════════════════
// attach argv
// ══════════════════════════════════════════════════════════════════════════════

TEST(AttachArgv, UsesDashTForTtyAndNestedTmux) {
    RemoteTarget t{"utah-notch", "n5", /*jobid*/""};
    const auto argv = build_attach_argv(t, "sess", "w1");
    EXPECT_EQ(argv[0], "ssh");
    bool saw_t = false, saw_login = false, saw_compute_cmd = false;
    for (const auto& a : argv) {
        if (a == "-t") saw_t = true;
        if (a == "utah-notch") saw_login = true;
        if (a.find("tmux") != std::string::npos
            && a.find("attach") != std::string::npos
            && a.find("sess:w1") != std::string::npos) saw_compute_cmd = true;
    }
    EXPECT_TRUE(saw_t);
    EXPECT_TRUE(saw_login);
    EXPECT_TRUE(saw_compute_cmd);
}

TEST(AttachArgv, HandlesEmptyNodeAsLoginLocal) {
    RemoteTarget t{"login-host", "", /*jobid*/""};
    const auto argv = build_attach_argv(t, "sess", "w1");
    EXPECT_EQ(argv[0], "ssh");
    const std::string joined = [&]{ std::string j; for (const auto& a : argv) { j += a; j += ' '; } return j; }();
    EXPECT_NE(joined.find("login-host"), std::string::npos);
    EXPECT_NE(joined.find("sess:w1"),    std::string::npos);
}

TEST(AttachArgv, JobidUsesSrunPtyOverSshNodeHop) {
    RemoteTarget t{"granite", "grn081", /*jobid*/"1153518"};
    const auto argv = build_attach_argv(t, "smoke", "1");
    // ssh -t <login> srun --jobid=... --overlap --pty tmux attach-session ...
    ASSERT_GE(argv.size(), 6u);
    EXPECT_EQ(argv[0], "ssh");
    EXPECT_EQ(argv[1], "-t");
    EXPECT_EQ(argv[2], "granite");
    EXPECT_EQ(argv[3], "srun");
    EXPECT_EQ(argv[4], "--jobid=1153518");
    EXPECT_EQ(argv[5], "--overlap");
    EXPECT_EQ(argv[6], "--pty");
    // No second ssh hop to compute node when jobid hops through srun.
    for (const auto& a : argv) EXPECT_NE(a, "grn081");
}

// ══════════════════════════════════════════════════════════════════════════════
// Parsers
// ══════════════════════════════════════════════════════════════════════════════

TEST(ParseListSessions, ManySessions) {
    const auto v = parse_list_sessions(read_fixture("tmux/list-sessions-many.txt"));
    ASSERT_EQ(v.size(), 3u);
    EXPECT_EQ(v[0].name,         "tash-utah-notchpeak-1234567-repoA");
    EXPECT_EQ(v[0].window_count, 2);
    EXPECT_EQ(v[0].attached,     true);
    EXPECT_EQ(v[1].attached,     false);
    EXPECT_EQ(v[2].name,         "tash-utah-kingspeak-9999-repoC");
    EXPECT_EQ(v[2].window_count, 1);
}

TEST(ParseListSessions, EmptyInputIsEmpty) {
    EXPECT_TRUE(parse_list_sessions("").empty());
}

TEST(ParseListSessions, MalformedLinesAreSkipped) {
    const auto v = parse_list_sessions("good|1|0\ngarbage\n\nalso good|2|1\n");
    ASSERT_EQ(v.size(), 2u);
    EXPECT_EQ(v[0].name, "good");
    EXPECT_EQ(v[1].name, "also good");
}

TEST(ParseListWindows, ParsesNameAndPid) {
    const auto v = parse_list_windows(read_fixture("tmux/list-windows-basic.txt"));
    ASSERT_EQ(v.size(), 3u);
    EXPECT_EQ(v[0].first,  "claude");
    EXPECT_EQ(v[0].second, 12345);
    EXPECT_EQ(v[2].first,  "1");
    EXPECT_EQ(v[2].second, 12347);
}

// ══════════════════════════════════════════════════════════════════════════════
// TmuxOpsReal — drives through a FakeSshClient + verifies it composes
// compose_remote_cmd + tmux_cmd_* correctly.
// ══════════════════════════════════════════════════════════════════════════════

TEST(TmuxOpsReal, NewSessionSendsComposedCmdThroughSsh) {
    auto t = make_tmux_ops();
    FakeSshClient ssh;
    RemoteTarget rt{"utah", "notch123"};
    t->new_session(rt, "repoA", "/scratch/repoA", ssh);

    ASSERT_EQ(ssh.run_calls.size(), 1u);
    EXPECT_EQ(ssh.run_calls[0].cluster, "utah");
    // The composed argv is a single shell-command string.
    const auto& cmd = ssh.run_calls[0].argv.back();
    EXPECT_NE(cmd.find("ssh 'notch123'"),  std::string::npos) << cmd;
    EXPECT_NE(cmd.find("tmux new-session"), std::string::npos);
    EXPECT_NE(cmd.find("repoA"),           std::string::npos);
    EXPECT_NE(cmd.find("/scratch/repoA"),  std::string::npos);
}

TEST(TmuxOpsReal, ListSessionsRoundtrips) {
    auto t = make_tmux_ops();
    FakeSshClient ssh;
    SshResult scripted;
    scripted.exit_code = 0;
    scripted.out       = "s1|4|1\ns2|1|0\n";
    ssh.queue_run(scripted);

    const auto v = t->list_sessions(RemoteTarget{"utah", "n1"}, ssh);
    ASSERT_EQ(v.size(), 2u);
    EXPECT_EQ(v[0].name, "s1");
    EXPECT_EQ(v[0].window_count, 4);
    EXPECT_EQ(v[0].attached, true);
}

TEST(TmuxOpsReal, IsWindowAliveExitCodeMapping) {
    auto t = make_tmux_ops();
    FakeSshClient ssh;
    // First call: exit 0 + a pid -> alive
    SshResult alive; alive.exit_code = 0; alive.out = "12345\n";
    ssh.queue_run(alive);
    EXPECT_TRUE(t->is_window_alive(RemoteTarget{"utah", "n1"}, "sess", "w", ssh));

    // Second call: exit 1 -> not alive
    SshResult dead; dead.exit_code = 1;
    ssh.queue_run(dead);
    EXPECT_FALSE(t->is_window_alive(RemoteTarget{"utah", "n1"}, "sess", "w", ssh));
}
