#include "test_helpers.h"

// Natural-language question with '?' routes through the AI layer, not through
// normal command execution. Without an API key the AI layer prints its own
// error; what matters is that we don't see a "command not found" for "how".
TEST(ContextualAi, QuestionRoutesToAi) {
    auto r = run_shell("how to list files?\nexit\n");
    EXPECT_EQ(r.output.find("how: command not found"), std::string::npos);
    EXPECT_EQ(r.output.find("No such file or directory"), std::string::npos);
    // An AI-layer marker is enough; every unconfigured path produces the
    // "tash ai" label in stderr output.
    EXPECT_NE(r.output.find("tash ai"), std::string::npos);
}

TEST(ContextualAi, CommandWithQuestionMarkInQuotesRunsNormally) {
    auto r = run_shell("echo 'what?'\nexit\n");
    EXPECT_NE(r.output.find("what?"), std::string::npos);
    EXPECT_EQ(r.output.find("tash ai"), std::string::npos);
}

TEST(ContextualAi, TestCommandWithQuestionMarkNotRoutedToAi) {
    // "test ..." starting with a valid builtin/command should NOT be
    // routed to AI even with a trailing '?'.
    auto r = run_shell("test -f /nonexistent_file_xyz?\nexit\n");
    EXPECT_EQ(r.output.find("tash ai"), std::string::npos);
}
