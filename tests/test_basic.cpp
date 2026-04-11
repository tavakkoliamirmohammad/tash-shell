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

TEST(Basic, BadCommand) {
    auto r = run_shell("nonexistent_command_xyz_12345\nexit\n");
    EXPECT_NE(r.output.find("No such file or directory"), std::string::npos);
}
