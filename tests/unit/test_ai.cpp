#include <gtest/gtest.h>

#ifdef TASH_AI_ENABLED

#include "tash/ai.h"
#include "tash/llm_client.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdlib>
#include <sys/stat.h>

using namespace std;

// ═══════════════════════════════════════════════════════════════
// Test fixture that redirects key/usage paths to /tmp
// so tests never touch the real ~/.tash_ai_key
// ═══════════════════════════════════════════════════════════════

class AiTestFixture : public ::testing::Test {
protected:
    string test_key_path;
    string test_usage_path;

    void SetUp() override {
        test_key_path = "/tmp/tash_test_ai_key_" + to_string(getpid());
        test_usage_path = "/tmp/tash_test_ai_usage_" + to_string(getpid());
        setenv("TASH_AI_KEY_PATH", test_key_path.c_str(), 1);
        setenv("TASH_AI_USAGE_PATH", test_usage_path.c_str(), 1);
    }

    void TearDown() override {
        unlink(test_key_path.c_str());
        unlink(test_usage_path.c_str());
        unsetenv("TASH_AI_KEY_PATH");
        unsetenv("TASH_AI_USAGE_PATH");
    }
};

// ═══════════════════════════════════════════════════════════════
// is_ai_command detection
// ═══════════════════════════════════════════════════════════════

TEST(AiParser, DetectsAiCommand) {
    EXPECT_TRUE(is_ai_command("@ai \"hello\""));
    EXPECT_TRUE(is_ai_command("@ai explain"));
    EXPECT_TRUE(is_ai_command("@ai what does ls"));
    EXPECT_TRUE(is_ai_command("@ai script \"task\""));
    EXPECT_TRUE(is_ai_command("@ai help \"topic\""));
    EXPECT_TRUE(is_ai_command("@ai setup"));
    EXPECT_TRUE(is_ai_command("@ai on"));
    EXPECT_TRUE(is_ai_command("@ai off"));
    EXPECT_TRUE(is_ai_command("@ai status"));
}

TEST(AiParser, DetectsWithLeadingSpaces) {
    EXPECT_TRUE(is_ai_command("  @ai \"hello\""));
    EXPECT_TRUE(is_ai_command("   @ai explain"));
}

TEST(AiParser, DetectsBarAiCommand) {
    EXPECT_TRUE(is_ai_command("@ai"));
}

TEST(AiParser, RejectsNonAiCommands) {
    EXPECT_FALSE(is_ai_command("echo hello"));
    EXPECT_FALSE(is_ai_command("ls -la"));
    EXPECT_FALSE(is_ai_command("git status"));
    EXPECT_FALSE(is_ai_command(""));
}

TEST(AiParser, RejectsPartialMatch) {
    EXPECT_FALSE(is_ai_command("@airplane"));
    EXPECT_FALSE(is_ai_command("@aide"));
    EXPECT_FALSE(is_ai_command("@airbag"));
}

// ═══════════════════════════════════════════════════════════════
// AI key management (uses temp paths via fixture)
// ═══════════════════════════════════════════════════════════════

TEST_F(AiTestFixture, KeyPathUsesOverride) {
    EXPECT_EQ(ai_get_key_path(), test_key_path);
}

TEST_F(AiTestFixture, SaveAndLoadKey) {
    string test_key = "test_key_" + to_string(getpid());

    EXPECT_TRUE(ai_save_key(test_key));

    string loaded = ai_load_key();
    EXPECT_EQ(loaded, test_key);

    // Check permissions (600)
    struct stat st;
    ASSERT_EQ(stat(test_key_path.c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0600);
}

TEST_F(AiTestFixture, LoadMissingKeyReturnsEmpty) {
    unlink(test_key_path.c_str());
    EXPECT_TRUE(ai_load_key().empty());
}

TEST_F(AiTestFixture, ValidateKey) {
    EXPECT_TRUE(ai_validate_key("AIzaSyAbcdefghijklmnopqrstuvwxyz0123456789")); // 42 chars >= 39
    EXPECT_FALSE(ai_validate_key(""));
    EXPECT_FALSE(ai_validate_key("short"));
    EXPECT_FALSE(ai_validate_key("AIzaSyAbcde")); // too short
}

// ═══════════════════════════════════════════════════════════════
// AI usage tracking (uses temp paths via fixture)
// ═══════════════════════════════════════════════════════════════

TEST_F(AiTestFixture, UsagePathUsesOverride) {
    EXPECT_EQ(ai_get_usage_path(), test_usage_path);
}

TEST_F(AiTestFixture, IncrementAndGetUsage) {
    EXPECT_EQ(ai_get_today_usage(), 0);

    ai_increment_usage();
    EXPECT_EQ(ai_get_today_usage(), 1);

    ai_increment_usage();
    EXPECT_EQ(ai_get_today_usage(), 2);
}

// ═══════════════════════════════════════════════════════════════
// Context-aware suggestions
// ═══════════════════════════════════════════════════════════════

TEST(ContextSuggest, BuildsTransitionsFromHistory) {
    string hist_path = "/tmp/tash_test_hist_" + to_string(getpid());
    {
        ofstream f(hist_path);
        for (int i = 0; i < 5; i++) {
            f << "git add .\n";
            f << "git commit -m \"change\"\n";
        }
    }

    TransitionMap tmap;
    build_transition_map(hist_path, tmap);

    EXPECT_GT(tmap.transitions.count("git add"), 0u);

    unlink(hist_path.c_str());
}

TEST(ContextSuggest, SuggestsFrequentSuccessor) {
    TransitionMap tmap;
    for (int i = 0; i < 5; i++) {
        tmap.transitions["git add"]["git commit -m \"update\""]++;
    }

    string suggestion = context_suggest("git add .", tmap);
    EXPECT_EQ(suggestion, "git commit -m \"update\"");
}

TEST(ContextSuggest, NoSuggestionBelowThreshold) {
    TransitionMap tmap;
    tmap.transitions["make"]["./a.out"] = 2;

    string suggestion = context_suggest("make", tmap);
    EXPECT_TRUE(suggestion.empty());
}

TEST(ContextSuggest, NoSuggestionForUnknownCommand) {
    TransitionMap tmap;
    string suggestion = context_suggest("unknown_cmd", tmap);
    EXPECT_TRUE(suggestion.empty());
}

TEST(ContextSuggest, EmptyHistoryNoTransitions) {
    string hist_path = "/tmp/tash_test_empty_hist_" + to_string(getpid());
    {
        ofstream f(hist_path);
    }

    TransitionMap tmap;
    build_transition_map(hist_path, tmap);
    EXPECT_TRUE(tmap.transitions.empty());

    unlink(hist_path.c_str());
}

// ═══════════════════════════════════════════════════════════════
// Gemini JSON tests
// ═══════════════════════════════════════════════════════════════

TEST(GeminiClient, BuildsValidRequestJson) {
    std::string json_str = build_gemini_request_json("sys", "usr");
    auto parsed = nlohmann::json::parse(json_str);
    EXPECT_EQ(parsed["system_instruction"]["parts"][0]["text"], "sys");
    EXPECT_EQ(parsed["contents"][0]["parts"][0]["text"], "usr");
}

TEST(GeminiClient, ParsesSuccessResponse) {
    std::string resp = R"({"candidates":[{"content":{"parts":[{"text":"hello"}]}}]})";
    EXPECT_EQ(extract_gemini_text(resp), "hello");
}

TEST(GeminiClient, ParsesUnicodeInResponse) {
    std::string resp = R"({"candidates":[{"content":{"parts":[{"text":"caf\u00e9"}]}}]})";
    EXPECT_NE(extract_gemini_text(resp).find("caf"), std::string::npos);
}

TEST(GeminiClient, ParsesErrorResponse) {
    std::string resp = R"({"error":{"message":"API key not valid"}})";
    EXPECT_EQ(extract_gemini_error(resp), "API key not valid");
}

TEST(GeminiClient, HandlesEmptyResponse) {
    EXPECT_TRUE(extract_gemini_text("{}").empty());
}

TEST(GeminiClient, HandlesNestedTextFields) {
    std::string resp = R"({"candidates":[{"content":{"parts":[{"text":"correct"}]},"safetyRatings":[{"text":"SAFE"}]}]})";
    EXPECT_EQ(extract_gemini_text(resp), "correct");
}

// ═══════════════════════════════════════════════════════════════
// OpenAI JSON tests
// ═══════════════════════════════════════════════════════════════

TEST(OpenAIClient, BuildsValidRequestJson) {
    std::string json_str = build_openai_request_json("gpt-4o-mini", "sys", "usr", false);
    auto parsed = nlohmann::json::parse(json_str);
    EXPECT_EQ(parsed["model"], "gpt-4o-mini");
    EXPECT_EQ(parsed["messages"][0]["role"], "system");
    EXPECT_EQ(parsed["messages"][0]["content"], "sys");
    EXPECT_EQ(parsed["messages"][1]["role"], "user");
    EXPECT_EQ(parsed["messages"][1]["content"], "usr");
    EXPECT_EQ(parsed["stream"], false);
}

TEST(OpenAIClient, ParsesSuccessResponse) {
    std::string resp = R"({"choices":[{"message":{"content":"hello"}}]})";
    EXPECT_EQ(extract_openai_text(resp), "hello");
}

TEST(OpenAIClient, ParsesErrorResponse) {
    std::string resp = R"({"error":{"message":"invalid key"}})";
    EXPECT_EQ(extract_openai_error(resp), "invalid key");
}

TEST(OpenAIClient, HandlesEmptyResponse) {
    EXPECT_TRUE(extract_openai_text("{}").empty());
}

// ═══════════════════════════════════════════════════════════════
// Ollama JSON tests
// ═══════════════════════════════════════════════════════════════

TEST(OllamaClient, BuildsValidRequestJson) {
    std::string json_str = build_ollama_request_json("llama3.2", "sys", "usr", false);
    auto parsed = nlohmann::json::parse(json_str);
    EXPECT_EQ(parsed["model"], "llama3.2");
    EXPECT_EQ(parsed["messages"][0]["role"], "system");
    EXPECT_EQ(parsed["messages"][0]["content"], "sys");
    EXPECT_EQ(parsed["messages"][1]["role"], "user");
    EXPECT_EQ(parsed["stream"], false);
}

TEST(OllamaClient, ParsesSuccessResponse) {
    std::string resp = R"({"message":{"role":"assistant","content":"hello"}})";
    EXPECT_EQ(extract_ollama_text(resp), "hello");
}

TEST(OllamaClient, HandlesEmptyResponse) {
    EXPECT_TRUE(extract_ollama_text("{}").empty());
}

// ═══════════════════════════════════════════════════════════════
// Factory tests
// ═══════════════════════════════════════════════════════════════

TEST(LLMFactory, CreateGemini) {
    auto c = create_llm_client("gemini", "key", "", "");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->get_provider_name(), "gemini");
}

TEST(LLMFactory, CreateOpenAI) {
    auto c = create_llm_client("openai", "", "key", "");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->get_provider_name(), "openai");
}

TEST(LLMFactory, CreateOllama) {
    auto c = create_llm_client("ollama", "", "", "http://localhost:11434");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->get_provider_name(), "ollama");
}

TEST(LLMFactory, UnknownReturnsNull) {
    auto c = create_llm_client("unknown", "", "", "");
    EXPECT_EQ(c, nullptr);
}

// ═══════════════════════════════════════════════════════════════
// Context JSON tests
// ═══════════════════════════════════════════════════════════════

TEST(GeminiClient, BuildsContextJson) {
    std::vector<ConversationTurn> hist = {{"user", "find files"}, {"assistant", "find . -type f"}};
    std::string json_str = build_gemini_context_json("sys", hist, "now delete them");
    auto parsed = nlohmann::json::parse(json_str);
    EXPECT_EQ(parsed["contents"].size(), 3u); // 2 history + 1 new
}

TEST(OpenAIClient, BuildsContextJson) {
    std::vector<ConversationTurn> hist = {{"user", "find files"}, {"assistant", "find . -type f"}};
    std::string json_str = build_openai_context_json("gpt-4o-mini", "sys", hist, "now delete them", false);
    auto parsed = nlohmann::json::parse(json_str);
    EXPECT_EQ(parsed["messages"].size(), 4u); // system + 2 history + 1 new
}

TEST(OllamaClient, BuildsContextJson) {
    std::vector<ConversationTurn> hist = {{"user", "find files"}, {"assistant", "find . -type f"}};
    std::string json_str = build_ollama_context_json("llama3.2", "sys", hist, "now delete them", false);
    auto parsed = nlohmann::json::parse(json_str);
    EXPECT_EQ(parsed["messages"].size(), 4u); // system + 2 history + 1 new
}

// ═══════════════════════════════════════════════════════════════
// Rate limiter tests
// ═══════════════════════════════════════════════════════════════

TEST(RateLimiter, AllowsUnderLimit) {
    AiRateLimiter limiter(5, 60);
    EXPECT_TRUE(limiter.allow());
    EXPECT_TRUE(limiter.allow());
    EXPECT_TRUE(limiter.allow());
}

TEST(RateLimiter, BlocksOverLimit) {
    AiRateLimiter limiter(2, 60);
    EXPECT_TRUE(limiter.allow());
    EXPECT_TRUE(limiter.allow());
    EXPECT_FALSE(limiter.allow());
}

// ═══════════════════════════════════════════════════════════════
// XDG config tests
// ═══════════════════════════════════════════════════════════════

TEST_F(AiTestFixture, ConfigPathRespectsXdgOverride) {
    unsetenv("TASH_AI_KEY_PATH");
    setenv("XDG_CONFIG_HOME", "/tmp/tash_test_xdg", 1);
    std::string path = ai_get_key_path();
    EXPECT_NE(path.find("/tmp/tash_test_xdg/tash"), std::string::npos);
    unsetenv("XDG_CONFIG_HOME");
    setenv("TASH_AI_KEY_PATH", test_key_path.c_str(), 1);
}

TEST_F(AiTestFixture, ConfigPathFallsBackToHome) {
    unsetenv("TASH_AI_KEY_PATH");
    unsetenv("XDG_CONFIG_HOME");
    std::string path = ai_get_key_path();
    EXPECT_NE(path.find(".config/tash"), std::string::npos);
    setenv("TASH_AI_KEY_PATH", test_key_path.c_str(), 1);
}

#else

TEST(AiDisabled, AiFeaturesNotAvailable) {
    SUCCEED() << "AI features disabled at build time";
}

#endif // TASH_AI_ENABLED
