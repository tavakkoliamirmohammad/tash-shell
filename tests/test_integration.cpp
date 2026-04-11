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
// Pipe support (PR #9)
// ═══════════════════════════════════════════════════════════════

TEST(IntegrationPipe, SinglePipe) {
    auto r = run_shell("echo hello | cat\nexit\n");
    EXPECT_NE(r.output.find("hello"), std::string::npos);
}

TEST(IntegrationPipe, PipeWithGrep) {
    auto r = run_shell("echo -e 'aaa\\nbbb\\nccc' | grep bbb\nexit\n");
    EXPECT_NE(r.output.find("bbb"), std::string::npos);
}

TEST(IntegrationPipe, TriplePipe) {
    auto r = run_shell("echo hello | cat | cat\nexit\n");
    EXPECT_NE(r.output.find("hello"), std::string::npos);
}

TEST(IntegrationPipe, PipeWithAndOperator) {
    auto r = run_shell("echo hello | cat && echo world\nexit\n");
    EXPECT_NE(r.output.find("hello"), std::string::npos);
    EXPECT_NE(r.output.find("world"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════
// Input & append redirection (PR #10)
// ═══════════════════════════════════════════════════════════════

TEST(IntegrationInputRedirect, AppendToFile) {
    std::string testfile = "/tmp/amish_gtest_append_" + std::to_string(getpid()) + ".txt";
    run_shell("echo line1 > " + testfile + "\nexit\n");
    run_shell("echo line2 >> " + testfile + "\nexit\n");

    std::string content = read_file(testfile);
    EXPECT_NE(content.find("line1"), std::string::npos);
    EXPECT_NE(content.find("line2"), std::string::npos);

    unlink(testfile.c_str());
}

TEST(IntegrationInputRedirect, InputFromFile) {
    std::string infile = "/tmp/amish_gtest_input_" + std::to_string(getpid()) + ".txt";
    // Write test data directly
    {
        std::ofstream f(infile);
        f << "cherry\napple\nbanana\n";
    }

    auto r = run_shell("sort < " + infile + "\nexit\n");
    EXPECT_NE(r.output.find("apple"), std::string::npos);
    // Verify sorted order: apple should come before banana
    auto apple_pos = r.output.find("apple");
    auto banana_pos = r.output.find("banana");
    if (apple_pos != std::string::npos && banana_pos != std::string::npos) {
        EXPECT_LT(apple_pos, banana_pos);
    }

    unlink(infile.c_str());
}

TEST(IntegrationInputRedirect, AppendDoesNotOverwrite) {
    std::string testfile = "/tmp/amish_gtest_append2_" + std::to_string(getpid()) + ".txt";
    run_shell("echo original > " + testfile + "\nexit\n");
    run_shell("echo appended >> " + testfile + "\nexit\n");

    std::string content = read_file(testfile);
    EXPECT_NE(content.find("original"), std::string::npos) << "Original content should be preserved";
    EXPECT_NE(content.find("appended"), std::string::npos) << "Appended content should be present";

    unlink(testfile.c_str());
}

// ═══════════════════════════════════════════════════════════════
// History command (PR #11)
// ═══════════════════════════════════════════════════════════════

TEST(IntegrationHistory, ShowsCommandHistory) {
    auto r = run_shell("echo aaa\necho bbb\nhistory\nexit\n");
    EXPECT_NE(r.output.find("echo aaa"), std::string::npos);
    EXPECT_NE(r.output.find("echo bbb"), std::string::npos);
    EXPECT_NE(r.output.find("history"), std::string::npos);
}

TEST(IntegrationHistory, NumberedEntries) {
    auto r = run_shell("echo first\nhistory\nexit\n");
    // Should have numbered entries like "  1  echo first"
    EXPECT_NE(r.output.find("1"), std::string::npos);
    EXPECT_NE(r.output.find("echo first"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════
// Environment variable expansion (PR #12)
// ═══════════════════════════════════════════════════════════════

TEST(IntegrationEnvVars, ExpandHOME) {
    auto r = run_shell("echo $HOME\nexit\n");
    const char *home = getenv("HOME");
    ASSERT_NE(home, nullptr);
    EXPECT_NE(r.output.find(home), std::string::npos);
}

TEST(IntegrationEnvVars, ExpandUSER) {
    auto r = run_shell("echo $USER\nexit\n");
    const char *user = getenv("USER");
    if (user) {
        EXPECT_NE(r.output.find(user), std::string::npos);
    }
}

TEST(IntegrationEnvVars, ExportAndRead) {
    auto r = run_shell("export AMISH_TEST_VAR=gtest_value_42\necho $AMISH_TEST_VAR\nexit\n");
    EXPECT_NE(r.output.find("gtest_value_42"), std::string::npos);
}

TEST(IntegrationEnvVars, UnsetVariable) {
    auto r = run_shell("export AMISH_UNSET_TEST=before\nunset AMISH_UNSET_TEST\necho $AMISH_UNSET_TEST\nexit\n");
    // After unset, echo $AMISH_UNSET_TEST should NOT contain "before"
    // The output will have "before" from... actually no, echo only runs after unset
    // Count occurrences of "before" - should appear 0 times in output after unset
    // The echo after unset should produce an empty line
    EXPECT_EQ(r.output.find("before"), std::string::npos) << "Variable should be unset";
}

TEST(IntegrationEnvVars, UndefinedVarExpandsEmpty) {
    auto r = run_shell("echo $DEFINITELY_NOT_SET_12345\nexit\n");
    // Should not crash, and should not print the literal "$DEFINITELY_NOT_SET_12345"
    EXPECT_EQ(r.output.find("DEFINITELY_NOT_SET_12345"), std::string::npos);
}

TEST(IntegrationEnvVars, ExportNoArgsListsVars) {
    auto r = run_shell("export\nexit\n");
    // Should output at least some env vars like HOME=, PATH=
    EXPECT_NE(r.output.find("HOME="), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════
// Glob expansion (PR #13)
// ═══════════════════════════════════════════════════════════════

TEST(IntegrationGlob, ExpandStar) {
    // Create temp files to glob against
    std::string dir = "/tmp/amish_glob_test_" + std::to_string(getpid());
    std::string cmd = "mkdir -p " + dir;
    system(cmd.c_str());
    system(("touch " + dir + "/aaa.txt " + dir + "/bbb.txt " + dir + "/ccc.log").c_str());

    auto r = run_shell("echo " + dir + "/*.txt\nexit\n");
    EXPECT_NE(r.output.find("aaa.txt"), std::string::npos);
    EXPECT_NE(r.output.find("bbb.txt"), std::string::npos);
    EXPECT_EQ(r.output.find("ccc.log"), std::string::npos);

    system(("rm -rf " + dir).c_str());
}

TEST(IntegrationGlob, UnmatchedPatternPreserved) {
    auto r = run_shell("echo *.nonexistent_ext_xyz\nexit\n");
    EXPECT_NE(r.output.find("*.nonexistent_ext_xyz"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════
// Semicolon and || operators (PR #21)
// ═══════════════════════════════════════════════════════════════

TEST(IntegrationOperators, SemicolonRunsBoth) {
    auto r = run_shell("echo aaa ; echo bbb\nexit\n");
    EXPECT_NE(r.output.find("aaa"), std::string::npos);
    EXPECT_NE(r.output.find("bbb"), std::string::npos);
}

TEST(IntegrationOperators, OrRunsOnFailure) {
    auto r = run_shell("false || echo fallback_ran\nexit\n");
    EXPECT_NE(r.output.find("fallback_ran"), std::string::npos);
}

TEST(IntegrationOperators, OrSkipsOnSuccess) {
    auto r = run_shell("true || echo should_not_appear_xyz\nexit\n");
    EXPECT_EQ(r.output.find("should_not_appear_xyz"), std::string::npos);
}

TEST(IntegrationOperators, AndSkipsOnFailure) {
    auto r = run_shell("false && echo should_not_appear_abc\nexit\n");
    EXPECT_EQ(r.output.find("should_not_appear_abc"), std::string::npos);
}

TEST(IntegrationOperators, AndRunsOnSuccess) {
    auto r = run_shell("true && echo success_ran\nexit\n");
    EXPECT_NE(r.output.find("success_ran"), std::string::npos);
}

TEST(IntegrationOperators, SemicolonAfterFailure) {
    auto r = run_shell("false ; echo still_runs\nexit\n");
    EXPECT_NE(r.output.find("still_runs"), std::string::npos);
}

TEST(IntegrationOperators, MixedOperators) {
    auto r = run_shell("echo start && echo mid ; echo end\nexit\n");
    EXPECT_NE(r.output.find("start"), std::string::npos);
    EXPECT_NE(r.output.find("mid"), std::string::npos);
    EXPECT_NE(r.output.find("end"), std::string::npos);
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
