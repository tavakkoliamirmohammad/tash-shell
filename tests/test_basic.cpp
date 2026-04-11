#include "test_helpers.h"

TEST(Basic, EchoCommand) {
    auto r = run_shell("echo integration_test_output\nexit\n");
    EXPECT_NE(r.output.find("integration_test_output"), std::string::npos);
}

TEST(Basic, PwdCommand) {
    char cwd[1024];
    getcwd(cwd, sizeof(cwd));
    auto r = run_shell("pwd\nexit\n");
    EXPECT_NE(r.output.find(cwd), std::string::npos);
}

TEST(Basic, ExitMessage) {
    auto r = run_shell("exit\n");
    EXPECT_NE(r.output.find("GoodBye! See you soon!"), std::string::npos);
}

TEST(Basic, CtrlDDoesNotCrash) {
    auto r = run_shell("");
    EXPECT_NE(r.exit_code, 139);
}

TEST(Basic, EchoQuotedStripsQuotes) {
    // Use file side-effect to avoid readline echo issues on Linux
    std::string testfile = "/tmp/tash_quote_test_" + std::to_string(getpid()) + ".txt";
    run_shell("echo \"hello world\" > " + testfile + "\nexit\n");
    std::string content = read_file(testfile);
    // File should contain unquoted string
    EXPECT_NE(content.find("hello world"), std::string::npos);
    // File should NOT contain quotes
    EXPECT_EQ(content.find("\"hello world\""), std::string::npos);
    unlink(testfile.c_str());
}

TEST(Basic, BadCommand) {
    auto r = run_shell("nonexistent_command_xyz_12345\nexit\n");
    EXPECT_NE(r.output.find("No such file or directory"), std::string::npos);
}

TEST(Basic, CommandSubstitutionSimple) {
    auto r = run_shell("echo $(echo hello)\nexit\n");
    EXPECT_NE(r.output.find("hello"), std::string::npos);
}

TEST(Basic, CommandSubstitutionInQuotes) {
    auto r = run_shell("echo \"count: $(echo 42)\"\nexit\n");
    EXPECT_NE(r.output.find("count: 42"), std::string::npos);
}

TEST(Basic, NoBannerWhenNotTTY) {
    auto r = run_shell("exit\n");
    EXPECT_EQ(r.output.find("Welcome"), std::string::npos);
}
TEST(Basic, TabCompletionRegistrationDoesNotCrash) {
    auto r = run_shell("exit\n");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.output.find("GoodBye! See you soon!"), std::string::npos);
}
