#include "test_helpers.h"

TEST(Safety, BlocksRmRfRoot) {
    auto r = run_shell("rm -rf /\nexit\n");
    // The BLOCKED warning is printed to stderr by the hook.
    EXPECT_NE(r.output.find("BLOCKED"), std::string::npos);
}

TEST(Safety, BackslashBypassRunsCommand) {
    auto r = run_shell("\\echo safety_bypass_ok\nexit\n");
    EXPECT_NE(r.output.find("safety_bypass_ok"), std::string::npos);
    EXPECT_EQ(r.output.find("BLOCKED"), std::string::npos);
}

TEST(Safety, SafeCommandNotTouched) {
    auto r = run_shell("echo ordinary_command_ok\nexit\n");
    EXPECT_NE(r.output.find("ordinary_command_ok"), std::string::npos);
    EXPECT_EQ(r.output.find("BLOCKED"), std::string::npos);
    EXPECT_EQ(r.output.find("WARNING"), std::string::npos);
}
