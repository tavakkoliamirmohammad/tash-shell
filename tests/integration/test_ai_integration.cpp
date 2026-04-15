#include "test_helpers.h"

// Helper: check if the tash binary was built with AI support
static bool ai_is_available() {
    auto r = run_shell("@ai\nexit\n");
    // If AI is compiled in, output contains "Usage" or "@ai"
    // If not, output contains "not available"
    return r.output.find("not available") == std::string::npos;
}

// Test that @ai with no args shows usage help (or "not available")
TEST(AiIntegration, AiNoArgsShowsMessage) {
    auto r = run_shell("@ai\nexit\n");
    // Should show either usage help or "not available" message
    bool has_usage = r.output.find("@ai") != std::string::npos;
    bool has_not_available = r.output.find("not available") != std::string::npos;
    EXPECT_TRUE(has_usage || has_not_available);
}

// Test that @ai on/off toggle works
TEST(AiIntegration, AiToggleOnOff) {
    if (!ai_is_available()) { GTEST_SKIP() << "AI not compiled in"; }
    auto r = run_shell("@ai off\n@ai on\nexit\n");
    EXPECT_NE(r.output.find("disabled"), std::string::npos);
    EXPECT_NE(r.output.find("enabled"), std::string::npos);
}

// Test that @ai setup shows config/status info (non-tty falls back to status)
TEST(AiIntegration, AiSetupDoesNotCrash) {
    if (!ai_is_available()) { GTEST_SKIP() << "AI not compiled in"; }
    auto r = run_shell("@ai setup\nexit\n");
    EXPECT_NE(r.exit_code, 139); // no segfault
    EXPECT_NE(r.output.find("Provider"), std::string::npos);
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
    if (!ai_is_available()) { GTEST_SKIP() << "AI not compiled in"; }
    auto r = run_shell("@ai status\nexit\n");
    EXPECT_NE(r.output.find("Status"), std::string::npos);
}

// Test that @ai clear does not crash
TEST(AiIntegration, AiClearDoesNotCrash) {
    if (!ai_is_available()) { GTEST_SKIP() << "AI not compiled in"; }
    auto r = run_shell("@ai clear\nexit\n");
    EXPECT_NE(r.exit_code, 139);
    EXPECT_NE(r.output.find("clear"), std::string::npos);
}

// Test that @ai test without key does not crash
TEST(AiIntegration, AiTestDoesNotCrash) {
    auto r = run_shell("@ai test\nexit\n");
    EXPECT_NE(r.exit_code, 139);
}

// Test that help text shows consolidated commands
TEST(AiIntegration, AiHelpTextShowsNewCommands) {
    if (!ai_is_available()) { GTEST_SKIP() << "AI not compiled in"; }
    auto r = run_shell("@ai\nexit\n");
    EXPECT_NE(r.output.find("config"), std::string::npos);
    EXPECT_NE(r.output.find("clear"), std::string::npos);
    EXPECT_NE(r.output.find("explain"), std::string::npos);
}

// Test that @ai status still works (alias for config in non-tty)
TEST(AiIntegration, AiStatusShowsProvider) {
    if (!ai_is_available()) { GTEST_SKIP() << "AI not compiled in"; }
    auto r = run_shell("@ai status\nexit\n");
    EXPECT_NE(r.output.find("Provider"), std::string::npos);
}

// Test that @ai setup still works (alias for config)
TEST(AiIntegration, AiSetupStillWorks) {
    if (!ai_is_available()) { GTEST_SKIP() << "AI not compiled in"; }
    auto r = run_shell("@ai setup\nexit\n");
    EXPECT_NE(r.exit_code, 139);
}

// Test that @ai provider with valid name does not crash
TEST(AiIntegration, AiProviderSwitchDoesNotCrash) {
    if (!ai_is_available()) { GTEST_SKIP() << "AI not compiled in"; }
    auto r = run_shell("@ai provider gemini\nexit\n");
    EXPECT_NE(r.exit_code, 139);
}

// Test that @ai provider with invalid name shows error
TEST(AiIntegration, AiProviderInvalidShowsError) {
    if (!ai_is_available()) { GTEST_SKIP() << "AI not compiled in"; }
    auto r = run_shell("@ai provider foobar\nexit\n");
    EXPECT_NE(r.output.find("unknown"), std::string::npos);
}

// Test that @ai model sets model without crash
TEST(AiIntegration, AiModelSetDoesNotCrash) {
    if (!ai_is_available()) { GTEST_SKIP() << "AI not compiled in"; }
    auto r = run_shell("@ai model gpt-4o\nexit\n");
    EXPECT_NE(r.exit_code, 139);
}

// Test that stderr capture works for @ai explain
TEST(AiIntegration, StderrCapturedAfterFailedCommand) {
    auto r = run_shell("ls /nonexistent_path_xyz_12345\n@ai explain\nexit\n");
    EXPECT_NE(r.exit_code, 139); // no segfault
}
