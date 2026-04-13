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
// Note: uses TASH_AI_KEY_PATH to avoid clobbering real key,
// since the piped "exit" would be consumed as the key value
TEST(AiIntegration, AiSetupDoesNotCrash) {
    std::string tmp_key = "/tmp/tash_integ_key_" + std::to_string(getpid());
    std::string cmd = "TASH_AI_KEY_PATH=" + tmp_key + " " + shell_binary + " < ";

    // Build input file
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
    auto r = run_shell("@ai status\nexit\n");
    EXPECT_NE(r.output.find("Status"), std::string::npos);
}
