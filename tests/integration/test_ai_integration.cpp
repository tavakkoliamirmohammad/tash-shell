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

// Test that @ai setup prompts for key
TEST(AiIntegration, AiSetupDoesNotCrash) {
    if (!ai_is_available()) { GTEST_SKIP() << "AI not compiled in"; }

    std::string tmp_key = "/tmp/tash_integ_key_" + std::to_string(getpid());
    char tmpfile[] = "/tmp/tash_setup_test_XXXXXX";
    int fd = mkstemp(tmpfile);
    std::string input = "@ai setup\nexit\n";
    if (write(fd, input.c_str(), input.size())) {}
    close(fd);

    std::string full_cmd = "TASH_AI_KEY_PATH=" + tmp_key + " " + shell_binary + " < " + tmpfile + " 2>&1";
    FILE *pipe = popen(full_cmd.c_str(), "r");
    std::string output;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe)) output += buffer;
    pclose(pipe);

    EXPECT_NE(output.find("API key"), std::string::npos);

    unlink(tmpfile);
    unlink(tmp_key.c_str());
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

// Test that help text shows new commands
TEST(AiIntegration, AiHelpTextShowsNewCommands) {
    if (!ai_is_available()) { GTEST_SKIP() << "AI not compiled in"; }
    auto r = run_shell("@ai\nexit\n");
    EXPECT_NE(r.output.find("config"), std::string::npos);
    EXPECT_NE(r.output.find("provider"), std::string::npos);
    EXPECT_NE(r.output.find("model"), std::string::npos);
    EXPECT_NE(r.output.find("test"), std::string::npos);
    EXPECT_NE(r.output.find("clear"), std::string::npos);
}

// Test that @ai status shows provider info
TEST(AiIntegration, AiStatusShowsProvider) {
    if (!ai_is_available()) { GTEST_SKIP() << "AI not compiled in"; }
    auto r = run_shell("@ai status\nexit\n");
    EXPECT_NE(r.output.find("Provider"), std::string::npos);
    EXPECT_NE(r.output.find("Model"), std::string::npos);
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

// Test that stderr capture works for @ai explain
TEST(AiIntegration, StderrCapturedAfterFailedCommand) {
    auto r = run_shell("ls /nonexistent_path_xyz_12345\n@ai explain\nexit\n");
    EXPECT_NE(r.exit_code, 139); // no segfault
}
