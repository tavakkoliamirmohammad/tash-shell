#include <gtest/gtest.h>

#ifdef TASH_AI_ENABLED

#include "tash/ai.h"
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
    EXPECT_TRUE(ai_validate_key("AIzaSyAbcdefghij1234567890"));
    EXPECT_FALSE(ai_validate_key(""));
    EXPECT_FALSE(ai_validate_key("short"));
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

#else

TEST(AiDisabled, AiFeaturesNotAvailable) {
    SUCCEED() << "AI features disabled at build time";
}

#endif // TASH_AI_ENABLED
