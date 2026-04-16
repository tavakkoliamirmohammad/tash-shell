#include <gtest/gtest.h>

#ifdef TASH_AI_ENABLED

#include "tash/plugins/ai_error_hook_provider.h"
#include "tash/llm_client.h"
#include "tash/shell.h"
#include <nlohmann/json.hpp>
#include <string>

using namespace std;
using json = nlohmann::json;

// ═══════════════════════════════════════════════════════════════
// Mock LLM Client
// ═══════════════════════════════════════════════════════════════

class MockLLMClient : public LLMClient {
public:
    MockLLMClient() : call_count_(0) {}

    void set_response(const string &text) {
        response_text_ = text;
    }

    int call_count() const { return call_count_; }

    string last_system_prompt() const { return last_system_prompt_; }
    string last_user_prompt() const { return last_user_prompt_; }

    LLMResponse generate(const string &system_prompt,
                          const string &user_prompt) override {
        call_count_++;
        last_system_prompt_ = system_prompt;
        last_user_prompt_ = user_prompt;
        LLMResponse resp;
        resp.success = true;
        resp.text = response_text_;
        resp.http_status = 200;
        return resp;
    }

    LLMResponse generate_stream(const string &, const string &,
                                 function<void(const string &)>) override {
        return LLMResponse{false, "", 0, "not implemented"};
    }

    LLMResponse generate_with_context(const string &,
                                       const vector<ConversationTurn> &,
                                       const string &) override {
        return LLMResponse{false, "", 0, "not implemented"};
    }

    LLMResponse generate_structured(const string &,
                                     const string &) override {
        return LLMResponse{false, "", 0, "not implemented"};
    }

    LLMResponse generate_structured_with_context(
        const string &, const vector<ConversationTurn> &,
        const string &) override {
        return LLMResponse{false, "", 0, "not implemented"};
    }

    void set_model(const string &) override {}
    string get_model() const override { return "mock-model"; }
    string get_provider_name() const override { return "mock"; }

private:
    int call_count_;
    string response_text_;
    string last_system_prompt_;
    string last_user_prompt_;
};

// ═══════════════════════════════════════════════════════════════
// Test fixture
// ═══════════════════════════════════════════════════════════════

class AiErrorHookTest : public ::testing::Test {
protected:
    MockLLMClient mock_client;
    ShellState state;

    void SetUp() override {
        mock_client.set_response(
            R"({"explanation":"Permission denied on the file.","fix":"sudo chmod 644 test.txt"})");
        state.ai_enabled = true;
        state.last_command_text = "";
        state.last_executed_cmd = "";
    }
};

// ═══════════════════════════════════════════════════════════════
// Trigger condition tests
// ═══════════════════════════════════════════════════════════════

TEST_F(AiErrorHookTest, TriggersOnNonZeroExit) {
    AiErrorHookProvider hook(&mock_client);
    EXPECT_TRUE(hook.should_trigger(1, "error: something failed", state));
}

TEST_F(AiErrorHookTest, SkipsOnExitZero) {
    AiErrorHookProvider hook(&mock_client);
    EXPECT_FALSE(hook.should_trigger(0, "some output", state));
}

TEST_F(AiErrorHookTest, SkipsOnCtrlC) {
    AiErrorHookProvider hook(&mock_client);
    EXPECT_FALSE(hook.should_trigger(130, "interrupted", state));
}

TEST_F(AiErrorHookTest, SkipsOnCommandNotFound) {
    AiErrorHookProvider hook(&mock_client);
    EXPECT_FALSE(hook.should_trigger(127, "command not found", state));
}

TEST_F(AiErrorHookTest, SkipsOnEmptyStderr) {
    AiErrorHookProvider hook(&mock_client);
    EXPECT_FALSE(hook.should_trigger(1, "", state));
}

TEST_F(AiErrorHookTest, SkipsWhenAiDisabled) {
    state.ai_enabled = false;
    AiErrorHookProvider hook(&mock_client);
    EXPECT_FALSE(hook.should_trigger(1, "error message", state));
}

// ═══════════════════════════════════════════════════════════════
// Rate limiter tests
// ═══════════════════════════════════════════════════════════════

TEST_F(AiErrorHookTest, RateLimiterBlocks) {
    AiErrorHookProvider hook(&mock_client);

    // First call succeeds and increments call_count via on_after_command
    // We call on_after_command in non-tty mode (no interactive prompt)
    hook.on_after_command("make", 2, "error: fail", state);
    EXPECT_EQ(mock_client.call_count(), 1);

    // Second call within cooldown should be blocked
    hook.on_after_command("make", 2, "error: fail", state);
    EXPECT_EQ(mock_client.call_count(), 1); // still 1 -- blocked
}

TEST_F(AiErrorHookTest, RateLimiterAllowsAfterCooldown) {
    AiErrorHookProvider hook(&mock_client);

    // First call
    hook.on_after_command("make", 2, "error: fail", state);
    EXPECT_EQ(mock_client.call_count(), 1);

    // Reset cooldown to simulate time passing
    hook.reset_cooldown();

    // Now it should allow again
    hook.on_after_command("make", 2, "error: fail", state);
    EXPECT_EQ(mock_client.call_count(), 2);
}

// ═══════════════════════════════════════════════════════════════
// Response parsing tests
// ═══════════════════════════════════════════════════════════════

TEST_F(AiErrorHookTest, ParsesJsonResponse) {
    string raw = R"({"explanation":"File not found.","fix":"touch missing.txt"})";
    ErrorRecoveryResponse resp = AiErrorHookProvider::parse_response(raw);
    EXPECT_TRUE(resp.valid);
    EXPECT_EQ(resp.explanation, "File not found.");
    EXPECT_EQ(resp.fix, "touch missing.txt");
}

TEST_F(AiErrorHookTest, ParsesExplanationOnly) {
    string raw = R"({"explanation":"Network is unreachable.","fix":""})";
    ErrorRecoveryResponse resp = AiErrorHookProvider::parse_response(raw);
    EXPECT_TRUE(resp.valid);
    EXPECT_EQ(resp.explanation, "Network is unreachable.");
    EXPECT_TRUE(resp.fix.empty());
}

TEST_F(AiErrorHookTest, HandlesMalformedResponse) {
    string raw = "this is not json at all";
    ErrorRecoveryResponse resp = AiErrorHookProvider::parse_response(raw);
    EXPECT_FALSE(resp.valid);
}

// ═══════════════════════════════════════════════════════════════
// Context JSON tests
// ═══════════════════════════════════════════════════════════════

TEST_F(AiErrorHookTest, ContextIncludesCommand) {
    AiErrorHookProvider hook(&mock_client);
    string ctx = hook.build_context_json("gcc -o main main.c", 1,
                                          "error: undeclared", state);
    json j = json::parse(ctx);
    EXPECT_EQ(j["command"], "gcc -o main main.c");
}

TEST_F(AiErrorHookTest, ContextIncludesStderr) {
    AiErrorHookProvider hook(&mock_client);
    string ctx = hook.build_context_json("make", 2,
                                          "make: *** No rule to make target", state);
    json j = json::parse(ctx);
    EXPECT_EQ(j["stderr"], "make: *** No rule to make target");
}

TEST_F(AiErrorHookTest, ContextIncludesDirectory) {
    AiErrorHookProvider hook(&mock_client);
    string ctx = hook.build_context_json("ls", 1, "no such file", state);
    json j = json::parse(ctx);
    EXPECT_FALSE(j["directory"].get<string>().empty());
}

// ═══════════════════════════════════════════════════════════════
// Hook provider name test
// ═══════════════════════════════════════════════════════════════

TEST_F(AiErrorHookTest, ProviderName) {
    AiErrorHookProvider hook(&mock_client);
    EXPECT_EQ(hook.name(), "ai-error-recovery");
}

#else

TEST(AiErrorHookDisabled, AiErrorHookNotAvailable) {
    SUCCEED() << "AI features disabled at build time";
}

#endif // TASH_AI_ENABLED
