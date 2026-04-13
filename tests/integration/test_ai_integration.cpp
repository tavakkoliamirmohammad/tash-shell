#include "test_helpers.h"

// Test that @ai with no args shows usage help
TEST(AiIntegration, AiNoArgsShowsUsage) {
    auto r = run_shell("@ai\nexit\n");
    EXPECT_NE(r.output.find("@ai"), std::string::npos);
}

// Test that @ai on/off toggle works
TEST(AiIntegration, AiToggleOnOff) {
    auto r = run_shell("@ai off\n@ai on\nexit\n");
    EXPECT_NE(r.output.find("disabled"), std::string::npos);
    EXPECT_NE(r.output.find("enabled"), std::string::npos);
}

// Test that @ai setup prompts for key
TEST(AiIntegration, AiSetupDoesNotCrash) {
    auto r = run_shell("@ai setup\nexit\n");
    EXPECT_NE(r.output.find("API key"), std::string::npos);
}

// Test that @ai explain with no previous error handles gracefully
TEST(AiIntegration, AiExplainNoPreviousError) {
    auto r = run_shell("@ai explain\nexit\n");
    EXPECT_NE(r.exit_code, 139); // no segfault
}

// Test that @ai with unknown subcommand does not crash
TEST(AiIntegration, AiUnknownSubcommandDoesNotCrash) {
    auto r = run_shell("@ai \"list files\"\nexit\n");
    EXPECT_NE(r.exit_code, 139); // no segfault
}

// Test that @ai status shows status info
TEST(AiIntegration, AiStatusShowsInfo) {
    auto r = run_shell("@ai status\nexit\n");
    EXPECT_NE(r.output.find("Status"), std::string::npos);
}
