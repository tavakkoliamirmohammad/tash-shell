#include <gtest/gtest.h>


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

    LLMResponse generate_structured_stream(
        const string &, const string &,
        function<void(const string &)>) override {
        return LLMResponse{false, "", 0, "not implemented"};
    }
    LLMResponse generate_structured_stream_with_context(
        const string &, const vector<ConversationTurn> &,
        const string &,
        function<void(const string &)>) override {
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
        state.ai.ai_enabled = true;
        state.ai.last_command_text = "";
        state.ai.last_executed_cmd = "";
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
    state.ai.ai_enabled = false;
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

// ═══════════════════════════════════════════════════════════════
// Lazy factory path: the hook can be registered without a client and
// later activates when the factory first returns one.
// ═══════════════════════════════════════════════════════════════

TEST_F(AiErrorHookTest, FactoryStaysDormantUntilClientAvailable) {
    int factory_calls = 0;
    std::unique_ptr<LLMClient> no_client;
    AiErrorHookProvider hook(
        [&]() -> std::unique_ptr<LLMClient> {
            factory_calls++;
            return nullptr;   // AI not yet configured
        });
    // Trigger a failing command — factory is called but returns null, so
    // no LLM call is made and call_count stays 0.
    ShellState state;
    state.ai.ai_enabled = true;
    hook.on_after_command("missing_cmd", 2, "some error\n", state);
    EXPECT_EQ(factory_calls, 1);
    EXPECT_EQ(hook.call_count(), 0);
}

TEST_F(AiErrorHookTest, FactoryActivatesHookWhenClientAppears) {
    auto mock = std::make_shared<MockLLMClient>();
    mock->set_response(
        "{\"explanation\":\"cmd not found\",\"fix\":\"which mycmd\"}");

    int factory_calls = 0;
    AiErrorHookProvider hook(
        [&]() -> std::unique_ptr<LLMClient> {
            factory_calls++;
            // Return a client wrapping our shared mock so the test
            // can observe call_count.
            struct Proxy : public LLMClient {
                std::shared_ptr<MockLLMClient> inner;
                explicit Proxy(std::shared_ptr<MockLLMClient> m) : inner(m) {}
                LLMResponse generate(const std::string &s,
                                     const std::string &u) override {
                    return inner->generate(s, u);
                }
                LLMResponse generate_stream(
                    const std::string &, const std::string &,
                    std::function<void(const std::string &)>) override {
                    return LLMResponse{false, "", 0, ""};
                }
                LLMResponse generate_with_context(
                    const std::string &,
                    const std::vector<ConversationTurn> &,
                    const std::string &) override {
                    return LLMResponse{false, "", 0, ""};
                }
                LLMResponse generate_structured(
                    const std::string &, const std::string &) override {
                    return LLMResponse{false, "", 0, ""};
                }
                LLMResponse generate_structured_with_context(
                    const std::string &,
                    const std::vector<ConversationTurn> &,
                    const std::string &) override {
                    return LLMResponse{false, "", 0, ""};
                }
                LLMResponse generate_structured_stream(
                    const std::string &, const std::string &,
                    std::function<void(const std::string &)>) override {
                    return LLMResponse{false, "", 0, ""};
                }
                LLMResponse generate_structured_stream_with_context(
                    const std::string &,
                    const std::vector<ConversationTurn> &,
                    const std::string &,
                    std::function<void(const std::string &)>) override {
                    return LLMResponse{false, "", 0, ""};
                }
                void set_model(const std::string &) override {}
                std::string get_model() const override { return "m"; }
                std::string get_provider_name() const override { return "m"; }
            };
            return std::unique_ptr<LLMClient>(new Proxy(mock));
        });

    ShellState state;
    state.ai.ai_enabled = true;
    // exit_code 1 + non-empty stderr passes should_trigger (127 is
    // bypassed because "command not found" rarely needs AI explanation).
    hook.on_after_command("mycmd", 1,
                          "mycmd: some real error\n", state);
    EXPECT_EQ(factory_calls, 1);
    EXPECT_EQ(mock->call_count(), 1);  // LLM was actually called
    EXPECT_EQ(hook.call_count(), 1);

    // Second trigger (after cooldown reset) reuses cached client, no new
    // factory call.
    hook.reset_cooldown();
    hook.on_after_command("other", 2, "other stderr\n", state);
    EXPECT_EQ(factory_calls, 1);  // still 1 — factory not re-invoked
    EXPECT_EQ(mock->call_count(), 2);
}

