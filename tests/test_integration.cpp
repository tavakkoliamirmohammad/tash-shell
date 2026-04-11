#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

// ═══════════════════════════════════════════════════════════════
// Integration test helpers
// ═══════════════════════════════════════════════════════════════

static std::string shell_binary;

// Run the shell with given input, return stdout+stderr
struct ShellResult {
    std::string output;
    int exit_code;
};

ShellResult run_shell(const std::string &input) {
    // Write input to a temp file
    char tmpfile[] = "/tmp/amish_test_XXXXXX";
    int fd = mkstemp(tmpfile);
    write(fd, input.c_str(), input.size());
    close(fd);

    std::string cmd = shell_binary + " < " + tmpfile + " 2>&1";
    FILE *pipe = popen(cmd.c_str(), "r");

    std::string output;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        output += buffer;
    }
    int status = pclose(pipe);
    int exit_code = WEXITSTATUS(status);

    unlink(tmpfile);
    return {output, exit_code};
}

std::string read_file(const std::string &path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int get_file_perms(const std::string &path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return -1;
    return st.st_mode & 0777;
}

// ═══════════════════════════════════════════════════════════════
// Basic commands
// ═══════════════════════════════════════════════════════════════

TEST(IntegrationBasic, EchoCommand) {
    auto r = run_shell("echo integration_test_output\nexit\n");
    EXPECT_NE(r.output.find("integration_test_output"), std::string::npos);
}

TEST(IntegrationBasic, PwdCommand) {
    char cwd[1024];
    getcwd(cwd, sizeof(cwd));
    auto r = run_shell("pwd\nexit\n");
    EXPECT_NE(r.output.find(cwd), std::string::npos);
}

TEST(IntegrationBasic, ExitMessage) {
    auto r = run_shell("exit\n");
    EXPECT_NE(r.output.find("GoodBye! See you soon!"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════
// EOF handling (PR #1)
// ═══════════════════════════════════════════════════════════════

TEST(IntegrationEOF, CtrlDDoesNotCrash) {
    auto r = run_shell("");  // empty input = immediate EOF
    EXPECT_NE(r.exit_code, 139);  // 139 = SIGSEGV
}

// ═══════════════════════════════════════════════════════════════
// File permissions (PR #3)
// ═══════════════════════════════════════════════════════════════

TEST(IntegrationRedirect, FileCreatedWithCorrectPermissions) {
    std::string testfile = "/tmp/amish_gtest_perms_" + std::to_string(getpid()) + ".txt";
    run_shell("echo perm_test > " + testfile + "\nexit\n");

    EXPECT_EQ(read_file(testfile), "perm_test\n");
    EXPECT_EQ(get_file_perms(testfile), 0644);

    unlink(testfile.c_str());
}

TEST(IntegrationRedirect, OverwriteExistingFile) {
    std::string testfile = "/tmp/amish_gtest_overwrite_" + std::to_string(getpid()) + ".txt";
    run_shell("echo first > " + testfile + "\nexit\n");
    run_shell("echo second > " + testfile + "\nexit\n");

    std::string content = read_file(testfile);
    EXPECT_NE(content.find("second"), std::string::npos);
    EXPECT_EQ(content.find("first"), std::string::npos);

    unlink(testfile.c_str());
}

// ═══════════════════════════════════════════════════════════════
// Background command args (PR #4)
// ═══════════════════════════════════════════════════════════════

TEST(IntegrationBgArgs, BgkillNoArgs) {
    auto r = run_shell("bgkill\nexit\n");
    EXPECT_NE(r.output.find("missing process number"), std::string::npos);
}

TEST(IntegrationBgArgs, BgstopNoArgs) {
    auto r = run_shell("bgstop\nexit\n");
    EXPECT_NE(r.output.find("missing process number"), std::string::npos);
}

TEST(IntegrationBgArgs, BgstartNoArgs) {
    auto r = run_shell("bgstart\nexit\n");
    EXPECT_NE(r.output.find("missing process number"), std::string::npos);
}

TEST(IntegrationBgArgs, BgkillNonNumeric) {
    auto r = run_shell("bgkill abc\nexit\n");
    EXPECT_NE(r.output.find("invalid process number"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════
// Background processes (PR #7)
// ═══════════════════════════════════════════════════════════════

TEST(IntegrationBackground, BgStartsProcess) {
    auto r = run_shell("bg sleep 1\nbglist\nexit\n");
    EXPECT_NE(r.output.find("Executing"), std::string::npos);
    EXPECT_NE(r.output.find("sleep"), std::string::npos);
    EXPECT_NE(r.output.find("Total Background Jobs: 1"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════
// cd & tilde expansion
// ═══════════════════════════════════════════════════════════════

TEST(IntegrationCd, CdHome) {
    auto r = run_shell("cd ~\npwd\nexit\n");
    const char *home = getenv("HOME");
    ASSERT_NE(home, nullptr);
    EXPECT_NE(r.output.find(home), std::string::npos);
}

TEST(IntegrationCd, CdNoArgs) {
    auto r = run_shell("cd\npwd\nexit\n");
    const char *home = getenv("HOME");
    ASSERT_NE(home, nullptr);
    EXPECT_NE(r.output.find(home), std::string::npos);
}

TEST(IntegrationCd, CdTmp) {
    auto r = run_shell("cd /tmp\npwd\nexit\n");
    // macOS uses /private/tmp
    bool found = r.output.find("/tmp") != std::string::npos ||
                 r.output.find("/private/tmp") != std::string::npos;
    EXPECT_TRUE(found);
}

TEST(IntegrationCd, CdBadPath) {
    auto r = run_shell("cd /nonexistent_path_xyz\nexit\n");
    EXPECT_NE(r.output.find("No such file or directory"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════
// Multiple commands with &&
// ═══════════════════════════════════════════════════════════════

TEST(IntegrationAnd, MultipleCommands) {
    auto r = run_shell("echo aaa && echo bbb && echo ccc\nexit\n");
    EXPECT_NE(r.output.find("aaa"), std::string::npos);
    EXPECT_NE(r.output.find("bbb"), std::string::npos);
    EXPECT_NE(r.output.find("ccc"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════
// Error handling
// ═══════════════════════════════════════════════════════════════

TEST(IntegrationError, BadCommand) {
    auto r = run_shell("nonexistent_command_xyz_12345\nexit\n");
    EXPECT_NE(r.output.find("No such file or directory"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════
// Main — pass shell binary path via env var
// ═══════════════════════════════════════════════════════════════

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);

    const char *bin = getenv("AMISH_SHELL_BIN");
    if (bin) {
        shell_binary = bin;
    } else {
        // Default: assume it's in the build directory
        shell_binary = "./shell.out";
    }

    return RUN_ALL_TESTS();
}
