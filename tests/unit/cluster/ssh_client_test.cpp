// Unit tests for SshClient. Argv construction is pure-string-testable;
// process-spawning behavior (ControlMaster reuse, timeout, exit-code
// capture) is exercised via a stub `ssh` shell script on $PATH.

#include <gtest/gtest.h>

#include "tash/cluster/ssh_client.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace tash::cluster;

namespace {

std::string join(const std::vector<std::string>& v) {
    std::string o;
    for (const auto& x : v) { o += x; o += ' '; }
    return o;
}

SshFlags sample_flags(const std::filesystem::path& sock = "/tmp/ss") {
    return SshFlags{sock, "utah-notch", /*batch*/ true};
}

}  // namespace

// ══════════════════════════════════════════════════════════════════════════════
// Argv construction
// ══════════════════════════════════════════════════════════════════════════════

TEST(SshClientArgv, RunIncludesControlMasterFlagsAndHost) {
    const auto argv = build_run_argv(sample_flags(), {"squeue", "-h"});
    const std::string j = join(argv);
    EXPECT_EQ(argv[0], "ssh");
    EXPECT_NE(j.find("ControlMaster=auto"),   std::string::npos) << j;
    EXPECT_NE(j.find("ControlPersist=yes"),   std::string::npos);
    // ControlPath is deliberately NOT forced on the command line —
    // we defer to ~/.ssh/config so existing multiplex sockets from
    // the user's outer shell get reused.
    EXPECT_EQ(j.find("ControlPath="),         std::string::npos) << j;
    EXPECT_NE(j.find("BatchMode=yes"),        std::string::npos);   // when batch=true
    EXPECT_NE(j.find("utah-notch"),           std::string::npos);
    // Remote args are passed through unchanged; callers shell-quote
    // any values that contain spaces (see slurm_parse::build_sbatch_argv
    // for the --wrap= quoting convention).
    EXPECT_NE(j.find("squeue"),               std::string::npos) << j;
    EXPECT_NE(j.find(" -h "),                 std::string::npos);
}

TEST(SshClientArgv, BatchModeOffWhenFlagFalse) {
    SshFlags f = sample_flags();
    f.batch_mode = false;
    const auto argv = build_run_argv(f, {"true"});
    const std::string j = join(argv);
    EXPECT_EQ(j.find("BatchMode=yes"), std::string::npos) << j;
}

TEST(SshClientArgv, RunPreservesRemoteArgvOrder) {
    const auto argv = build_run_argv(sample_flags(), {"echo", "hello", "world"});
    ASSERT_GE(argv.size(), 3u);
    // Remote args are passed through unchanged — callers own their
    // own shell-quoting when values have spaces.
    EXPECT_EQ(argv[argv.size() - 3], "echo");
    EXPECT_EQ(argv[argv.size() - 2], "hello");
    EXPECT_EQ(argv[argv.size() - 1], "world");
}

TEST(SshClientArgv, MasterCheckUsesDashOCheck) {
    const auto argv = build_master_check_argv(sample_flags());
    const std::string j = join(argv);
    EXPECT_EQ(argv[0], "ssh");
    EXPECT_NE(j.find(" -O check "),         std::string::npos) << j;
    EXPECT_EQ(j.find("ControlPath="),       std::string::npos) << j;
    EXPECT_NE(j.find("utah-notch"),         std::string::npos);
}

TEST(SshClientArgv, ConnectEstablishesMasterViaForegroundTrue) {
    const auto argv = build_connect_argv(sample_flags());
    const std::string j = join(argv);
    EXPECT_EQ(argv[0], "ssh");
    // ControlMaster=auto + ControlPersist=yes (from base flags) plus
    // a trivial foreground command establish the persistent master
    // as a side effect — no -M/-N/-f needed.
    EXPECT_NE(j.find("ControlMaster=auto"),   std::string::npos) << j;
    EXPECT_NE(j.find("ControlPersist=yes"),   std::string::npos);
    EXPECT_EQ(argv.back(), "true");
    EXPECT_NE(j.find("utah-notch"),           std::string::npos);
}

TEST(SshClientArgv, ConnectUsesBatchModeOffForPasswordPrompts) {
    const auto argv = build_connect_argv(sample_flags());
    const std::string j = join(argv);
    // connect must not force BatchMode — that would block password+Duo prompts.
    EXPECT_EQ(j.find("BatchMode=yes"), std::string::npos) << j;
}

// ══════════════════════════════════════════════════════════════════════════════
// install_remote_file argv — base64 encode + sh -c pipeline
// ══════════════════════════════════════════════════════════════════════════════
TEST(SshInstallFile, ArgvIsSingleShDashCPayload) {
    const auto argv = build_install_file_argv("hi\n", "/tmp/hook.sh");
    // One argv element: a pre-composed `/bin/sh -c '…'` shell invocation.
    // This survives ssh's concatenate-and-re-parse protocol intact,
    // whereas an (sh, -c, cmd) triple would be split by the remote
    // shell's word-splitter.
    ASSERT_EQ(argv.size(), 1u);
    EXPECT_EQ(argv[0].substr(0, 10), "/bin/sh -c");
}

TEST(SshInstallFile, CommandIncludesMkdirAndChmodAndBase64) {
    const auto argv = build_install_file_argv("#!/bin/sh\necho hi\n",
                                                "/home/u/.tash-cluster/stop-hooks/x.sh");
    const std::string& cmd = argv[0];
    EXPECT_NE(cmd.find("mkdir -p"),  std::string::npos) << cmd;
    EXPECT_NE(cmd.find("chmod 0755"), std::string::npos);
    EXPECT_NE(cmd.find("base64 -d"),  std::string::npos);
    EXPECT_NE(cmd.find("/home/u/.tash-cluster/stop-hooks"), std::string::npos);
    EXPECT_NE(cmd.find("/home/u/.tash-cluster/stop-hooks/x.sh"), std::string::npos);
}

TEST(SshInstallFile, HomeVarInPathIsNotSingleQuoted) {
    // Paths that include $HOME (or other shell vars) must be
    // double-quoted so the remote shell expands them, not
    // single-quoted (which would create a dir literally named
    // "$HOME"). Regression: CHPC granite install was silently
    // creating ~/$HOME/.tash-cluster/... because of the wrong
    // quoting.
    const auto argv = build_install_file_argv(
        "x", "$HOME/.tash-cluster/stop-hooks/hook.sh");
    const std::string& cmd = argv[0];
    EXPECT_NE(cmd.find("\"$HOME/.tash-cluster/stop-hooks\""),
              std::string::npos) << cmd;
    EXPECT_NE(cmd.find("\"$HOME/.tash-cluster/stop-hooks/hook.sh\""),
              std::string::npos) << cmd;
    // And the literal single-quoted form of $HOME must NOT appear —
    // that's the bug shape we're defending against.
    EXPECT_EQ(cmd.find("'$HOME"), std::string::npos) << cmd;
}

TEST(SshInstallFile, BinaryContentSurvivesBase64Roundtrip) {
    // Build a local file containing null bytes and non-printable chars;
    // assert the command contains base64 that decodes back to the
    // original. We decode the base64 ourselves here to avoid a real ssh.
    std::string input;
    for (int i = 0; i < 256; ++i) input.push_back(static_cast<char>(i));

    const auto argv = build_install_file_argv(input, "/tmp/b.bin");
    const std::string& cmd = argv[0];

    // The argv is a single pre-composed `/bin/sh -c '…'` where the
    // inner command has its own single-quoted pieces escaped as '\''.
    // Find "printf %s" and then scan forward to the next base64-
    // alphabet run, reading until a non-alphabet char. That skips
    // the nested quote prefix regardless of how the outer is wrapped.
    const auto p0 = cmd.find("printf %s");
    ASSERT_NE(p0, std::string::npos) << cmd;
    auto is_b64 = [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
               (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=';
    };
    std::size_t start = p0 + std::string("printf %s").size();
    while (start < cmd.size() && !is_b64(cmd[start])) ++start;
    std::size_t end = start;
    while (end < cmd.size() && is_b64(cmd[end])) ++end;
    ASSERT_GT(end, start);
    const std::string b64 = cmd.substr(start, end - start);

    // Decode and compare. Tiny inline base64 decoder for the test.
    auto dec = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::string out;
    for (std::size_t i = 0; i + 3 < b64.size(); i += 4) {
        const int a = dec(b64[i]), b = dec(b64[i+1]);
        const int c = b64[i+2] == '=' ? -1 : dec(b64[i+2]);
        const int d = b64[i+3] == '=' ? -1 : dec(b64[i+3]);
        out += static_cast<char>((a << 2) | (b >> 4));
        if (c >= 0) out += static_cast<char>(((b & 0xf) << 4) | (c >> 2));
        if (d >= 0) out += static_cast<char>(((c & 0x3) << 6) | d);
    }
    EXPECT_EQ(out, input);
}

TEST(SshClientArgv, DisconnectUsesDashOExit) {
    const auto argv = build_disconnect_argv(sample_flags());
    const std::string j = join(argv);
    EXPECT_EQ(argv[0], "ssh");
    EXPECT_NE(j.find(" -O exit "),           std::string::npos) << j;
    EXPECT_EQ(j.find("ControlPath="),        std::string::npos) << j;
}

// ══════════════════════════════════════════════════════════════════════════════
// Process-level behaviour via a stub `ssh` script on $PATH
// ══════════════════════════════════════════════════════════════════════════════
//
// Strategy: put a tiny `ssh` bash shim ahead of $PATH that writes every
// invocation to $TASH_FAKE_SSH_LOG and emits $TASH_FAKE_SSH_STDOUT /
// $TASH_FAKE_SSH_EXIT. We construct SshClientReal and observe:
//   - run() actually forks/execs *something* named ssh
//   - multiple run() calls all find the stub (single socket dir, single hash)
//   - stdout / exit_code round-trip through the pipe correctly

namespace {

std::filesystem::path make_ssh_stub_dir() {
    const auto tag = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    auto d = std::filesystem::temp_directory_path() /
             ("tash_cluster_ssh_stub_" + tag);
    std::filesystem::create_directories(d);

    const auto script = d / "ssh";
    std::ofstream f(script);
    // POSIX sh so this runs on Alpine (busybox ash) in addition to
    // bash on other lanes.
    f << R"SH(#!/bin/sh
log="${TASH_FAKE_SSH_LOG:-/dev/null}"
{
    printf 'argv='
    for a in "$@"; do printf '%s|' "$a"; done
    printf '\n'
} >> "$log"
printf '%s' "${TASH_FAKE_SSH_STDOUT:-}"
exit "${TASH_FAKE_SSH_EXIT:-0}"
)SH";
    f.close();
    std::filesystem::permissions(script,
        std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
        std::filesystem::perms::group_exec,
        std::filesystem::perm_options::replace);

    return d;
}

class SshStubFixture : public ::testing::Test {
protected:
    std::filesystem::path stub_dir;
    std::filesystem::path log_path;
    std::filesystem::path sockets;
    std::string saved_path;

    void SetUp() override {
        stub_dir  = make_ssh_stub_dir();
        log_path  = stub_dir / "ssh.log";
        sockets   = stub_dir / "sockets";
        std::filesystem::create_directories(sockets);
        saved_path = std::getenv("PATH") ? std::getenv("PATH") : "";
        const std::string new_path = stub_dir.string() + ":" + saved_path;
        ::setenv("PATH",                  new_path.c_str(),       1);
        ::setenv("TASH_FAKE_SSH_LOG",     log_path.c_str(),       1);
        ::setenv("TASH_FAKE_SSH_STDOUT",  "fake-stdout",          1);
        ::setenv("TASH_FAKE_SSH_EXIT",    "0",                    1);
    }
    void TearDown() override {
        ::setenv("PATH", saved_path.c_str(), 1);
        ::unsetenv("TASH_FAKE_SSH_LOG");
        ::unsetenv("TASH_FAKE_SSH_STDOUT");
        ::unsetenv("TASH_FAKE_SSH_EXIT");
        std::error_code ec;
        std::filesystem::remove_all(stub_dir, ec);
    }

    std::string read_log() const {
        std::ifstream f(log_path);
        std::ostringstream b; b << f.rdbuf(); return b.str();
    }
};

}  // namespace

TEST_F(SshStubFixture, RunExecsSshAndCapturesStdoutAndExit) {
    auto client = make_ssh_client(
        [](const std::string& c) { return c + "-host"; }, sockets);

    auto r = client->run("c1", {"echo", "hi"}, std::chrono::seconds{5});
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.out,        "fake-stdout");

    const auto log = read_log();
    EXPECT_NE(log.find("c1-host"),         std::string::npos) << log;
    EXPECT_NE(log.find("ControlMaster"),   std::string::npos);
    EXPECT_NE(log.find("echo"),            std::string::npos);
}

TEST_F(SshStubFixture, TenRunsShareTheSameControlMasterIntent) {
    auto client = make_ssh_client(
        [](const std::string& c) { return c + "-host"; }, sockets);
    for (int i = 0; i < 10; ++i) {
        auto r = client->run("c1", {"true"}, std::chrono::seconds{5});
        ASSERT_EQ(r.exit_code, 0);
    }
    const auto log = read_log();
    // We emit ControlMaster=auto + ControlPersist=yes on every run so
    // OpenSSH multiplexes through whatever ControlPath ~/.ssh/config
    // specifies (or its default). We intentionally don't force a
    // ControlPath on the command line — verify the flags we *do* send
    // show up on every invocation.
    std::size_t cm_count = 0, cp_count = 0;
    for (std::size_t pos = 0;
         (pos = log.find("ControlMaster=auto", pos)) != std::string::npos; ++pos) ++cm_count;
    for (std::size_t pos = 0;
         (pos = log.find("ControlPersist=yes", pos)) != std::string::npos; ++pos) ++cp_count;
    EXPECT_EQ(cm_count, 10u) << log;
    EXPECT_EQ(cp_count, 10u) << log;
}

TEST_F(SshStubFixture, NonZeroExitIsSurfaced) {
    ::setenv("TASH_FAKE_SSH_EXIT", "255", 1);
    auto client = make_ssh_client(
        [](const std::string& c) { return c + "-host"; }, sockets);
    auto r = client->run("c1", {"true"}, std::chrono::seconds{5});
    EXPECT_EQ(r.exit_code, 255);
}

TEST_F(SshStubFixture, MasterAliveMapsExit0ToTrue) {
    auto client = make_ssh_client(
        [](const std::string& c) { return c + "-host"; }, sockets);
    EXPECT_TRUE(client->master_alive("c1"));
    ::setenv("TASH_FAKE_SSH_EXIT", "1", 1);
    EXPECT_FALSE(client->master_alive("c1"));
}

TEST_F(SshStubFixture, ConnectAndDisconnectEmitCorrectArgv) {
    auto client = make_ssh_client(
        [](const std::string& c) { return c + "-host"; }, sockets);
    client->connect("c2");
    client->disconnect("c2");
    const auto log = read_log();
    EXPECT_NE(log.find("ControlMaster=auto"), std::string::npos) << log;
    EXPECT_NE(log.find("ControlPersist=yes"), std::string::npos);
    EXPECT_NE(log.find("|true"),              std::string::npos);
    EXPECT_NE(log.find("-O|exit"),            std::string::npos);
    EXPECT_NE(log.find("c2-host"),            std::string::npos);
}

// Regression: if the stub sleeps well past the SshClient timeout,
// spawn_capture must kill it and reap it, not hang the caller. This
// covers the kill+waitpid path and exercises the "child still alive
// at break" invariant that the fix for C2 also depends on.
TEST_F(SshStubFixture, RunTimesOutAndKillsHungChild) {
    // Rewrite the stub in place to sleep forever.
    std::ofstream f(stub_dir / "ssh");
    f << R"SH(#!/bin/sh
sleep 30
)SH";
    f.close();
    std::filesystem::permissions(stub_dir / "ssh",
        std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
        std::filesystem::perms::group_exec,
        std::filesystem::perm_options::replace);

    auto client = make_ssh_client(
        [](const std::string& c) { return c + "-host"; }, sockets);

    const auto t0 = std::chrono::steady_clock::now();
    auto r = client->run("c1", {"true"}, std::chrono::milliseconds{200});
    const auto dt = std::chrono::steady_clock::now() - t0;

    // Must return within a small multiple of the timeout — proves the
    // child was SIGKILLed and reaped, not blocking waitpid.
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(dt).count(),
              1500);
    // exit_code reflects the SIGKILL (128 + 9 = 137) or a non-zero
    // failure — we don't assert exact, just that it's not a clean 0.
    EXPECT_NE(r.exit_code, 0);
}

TEST_F(SshStubFixture, MakeSshClientCreatesSocketDir) {
    const auto deep = stub_dir / "nonexistent" / "nested";
    ASSERT_FALSE(std::filesystem::exists(deep));
    auto client = make_ssh_client(
        [](const std::string& c) { return c + "-host"; }, deep);
    EXPECT_TRUE(std::filesystem::exists(deep));
}
