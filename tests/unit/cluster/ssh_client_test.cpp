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
    EXPECT_NE(j.find("ControlPath=/tmp/ss"),  std::string::npos);
    EXPECT_NE(j.find("BatchMode=yes"),        std::string::npos);   // when batch=true
    EXPECT_NE(j.find("utah-notch"),           std::string::npos);
    EXPECT_NE(j.find("squeue"),               std::string::npos);
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
    EXPECT_EQ(argv[argv.size() - 3], "echo");
    EXPECT_EQ(argv[argv.size() - 2], "hello");
    EXPECT_EQ(argv[argv.size() - 1], "world");
}

TEST(SshClientArgv, MasterCheckUsesDashOCheck) {
    const auto argv = build_master_check_argv(sample_flags());
    const std::string j = join(argv);
    EXPECT_EQ(argv[0], "ssh");
    EXPECT_NE(j.find(" -O check "),         std::string::npos) << j;
    EXPECT_NE(j.find("ControlPath=/tmp/ss"), std::string::npos);
    EXPECT_NE(j.find("utah-notch"),         std::string::npos);
}

TEST(SshClientArgv, ConnectStartsDetachedMaster) {
    const auto argv = build_connect_argv(sample_flags());
    const std::string j = join(argv);
    EXPECT_EQ(argv[0], "ssh");
    // -M = master, -N = no command, -f = background after auth. Together:
    EXPECT_NE(j.find("-M"), std::string::npos) << j;
    EXPECT_NE(j.find("-N"), std::string::npos);
    EXPECT_NE(j.find("-f"), std::string::npos);
    EXPECT_NE(j.find("utah-notch"), std::string::npos);
}

TEST(SshClientArgv, ConnectUsesBatchModeOffForPasswordPrompts) {
    const auto argv = build_connect_argv(sample_flags());
    const std::string j = join(argv);
    // connect must not force BatchMode — that would block password+Duo prompts.
    EXPECT_EQ(j.find("BatchMode=yes"), std::string::npos) << j;
}

TEST(SshClientArgv, DisconnectUsesDashOExit) {
    const auto argv = build_disconnect_argv(sample_flags());
    const std::string j = join(argv);
    EXPECT_EQ(argv[0], "ssh");
    EXPECT_NE(j.find(" -O exit "),           std::string::npos) << j;
    EXPECT_NE(j.find("ControlPath=/tmp/ss"), std::string::npos);
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
    f << R"SH(#!/usr/bin/env bash
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

TEST_F(SshStubFixture, TenRunsAllShareTheSameControlPath) {
    auto client = make_ssh_client(
        [](const std::string& c) { return c + "-host"; }, sockets);
    for (int i = 0; i < 10; ++i) {
        auto r = client->run("c1", {"true"}, std::chrono::seconds{5});
        ASSERT_EQ(r.exit_code, 0);
    }
    const auto log = read_log();
    // Count occurrences of the socket dir path in the log — exactly one
    // per invocation, all identical (not 10 different sockets).
    std::size_t count = 0;
    const auto marker = "ControlPath=" + sockets.string();
    for (std::size_t pos = 0; (pos = log.find(marker, pos)) != std::string::npos; ++pos) ++count;
    EXPECT_EQ(count, 10u) << log;
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
    EXPECT_NE(log.find("-M"),          std::string::npos) << log;
    EXPECT_NE(log.find("-N"),          std::string::npos);
    EXPECT_NE(log.find("-f"),          std::string::npos);
    EXPECT_NE(log.find("-O|exit"),     std::string::npos);
    EXPECT_NE(log.find("c2-host"),     std::string::npos);
}

TEST_F(SshStubFixture, MakeSshClientCreatesSocketDir) {
    const auto deep = stub_dir / "nonexistent" / "nested";
    ASSERT_FALSE(std::filesystem::exists(deep));
    auto client = make_ssh_client(
        [](const std::string& c) { return c + "-host"; }, deep);
    EXPECT_TRUE(std::filesystem::exists(deep));
}
