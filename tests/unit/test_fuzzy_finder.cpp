#include <gtest/gtest.h>
#include "tash/ui/fuzzy_finder.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

using namespace tash;

// ── Individual scoring tests ─────────────────────────────────────

TEST(FuzzyFinder, ExactMatchHighestScore) {
    int exact  = fuzzy_score("git", "git");
    int prefix = fuzzy_score("gi", "git");
    int partial = fuzzy_score("gt", "git");
    EXPECT_GT(exact, 0);
    EXPECT_GT(exact, prefix);
    EXPECT_GT(exact, partial);
}

TEST(FuzzyFinder, PrefixMatchScoresHigh) {
    int prefix   = fuzzy_score("gi", "git");
    int scattered = fuzzy_score("gt", "git");
    EXPECT_GT(prefix, 0);
    EXPECT_GE(prefix, scattered);
}

TEST(FuzzyFinder, BoundaryMatchScoresHigh) {
    // 'g' matches start of "git checkout" (start + boundary bonus)
    // 'c' matches start of "checkout" (boundary bonus)
    int score = fuzzy_score("gc", "git checkout");
    EXPECT_GT(score, 0);

    // Compare against a non-boundary match: "gc" in "abgcd"
    // where neither g nor c is at a word boundary
    int non_boundary = fuzzy_score("gc", "abgcd");
    EXPECT_GT(score, non_boundary);
}

TEST(FuzzyFinder, SubsequenceMatches) {
    int score = fuzzy_score("gchk", "git checkout");
    EXPECT_GT(score, 0);
}

TEST(FuzzyFinder, NoMatchReturnsZero) {
    EXPECT_EQ(fuzzy_score("xyz", "git"), 0);
}

TEST(FuzzyFinder, CaseInsensitiveOption) {
    // Upper-case query still matches lower-case candidate
    int score_upper = fuzzy_score("Git", "git");
    int score_lower = fuzzy_score("git", "git");
    EXPECT_GT(score_upper, 0);
    EXPECT_GT(score_lower, 0);
    // Both should yield the same score (case-insensitive matching)
    EXPECT_EQ(score_upper, score_lower);
}

TEST(FuzzyFinder, ShorterCandidatePreferred) {
    // When match quality is equal, shorter candidate should rank higher
    // in fuzzy_filter output.
    std::vector<std::string> candidates = {"git", "git-extras"};
    auto results = fuzzy_filter("git", candidates);
    ASSERT_GE(results.size(), 2u);
    // "git" is shorter and has at least the same score, so it comes first.
    EXPECT_EQ(results[0].text, "git");
}

TEST(FuzzyFinder, RankingCorrect) {
    std::vector<std::string> candidates = {
        "build_tools",    // weaker match for "bt"
        "bluetooth",      // 'b' start, 't' later
        "bt",             // exact
        "big_table"       // boundary match
    };
    auto results = fuzzy_filter("bt", candidates);
    ASSERT_FALSE(results.empty());
    // The exact match "bt" should be at the top.
    EXPECT_EQ(results[0].text, "bt");
}

TEST(FuzzyFinder, EmptyQueryMatchesAll) {
    std::vector<std::string> candidates = {"a", "b", "c"};
    auto results = fuzzy_filter("", candidates);
    EXPECT_EQ(results.size(), 3u);
    // All should have a positive score.
    for (const auto &r : results) {
        EXPECT_GT(r.score, 0);
    }
}

TEST(FuzzyFinder, EmptyCandidate) {
    EXPECT_EQ(fuzzy_score("a", ""), 0);
}

TEST(FuzzyFinder, SpecialCharacters) {
    // Spaces, dots in query should be handled
    int score_space = fuzzy_score("g c", "git checkout");
    EXPECT_GT(score_space, 0);

    int score_dot = fuzzy_score("m.j", "main.js");
    EXPECT_GT(score_dot, 0);
}

TEST(FuzzyFinder, FilterLimitsResults) {
    std::vector<std::string> candidates;
    for (int i = 0; i < 50; i++) {
        candidates.push_back("item" + std::to_string(i));
    }
    auto results = fuzzy_filter("item", candidates, 3);
    EXPECT_EQ(static_cast<int>(results.size()), 3);
}

TEST(FuzzyFinder, FilterEmptyInput) {
    std::vector<std::string> empty;
    auto results = fuzzy_filter("test", empty);
    EXPECT_TRUE(results.empty());
}

TEST(FuzzyFinder, LargeCandidateSet) {
    std::vector<std::string> candidates;
    candidates.reserve(10000);
    for (int i = 0; i < 10000; i++) {
        candidates.push_back("candidate_" + std::to_string(i));
    }

    auto start = std::chrono::steady_clock::now();
    auto results = fuzzy_filter("can", candidates, 20);
    auto end = std::chrono::steady_clock::now();

    EXPECT_FALSE(results.empty());
    EXPECT_LE(static_cast<int>(results.size()), 20);

    // Sanity: should complete in under 5 seconds (very generous).
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    EXPECT_LT(ms, 5000);
}

// ── Completion callback wiring ───────────────────────────────────

#include "tash/ui.h"
// Fuzzy fallback triggers only when the user typed a prefix that yields no
// direct prefix match and is at least 2 chars. Built-ins like `cd`, `pwd`,
// `history` must still show up fuzzily for made-up prefixes.
TEST(FuzzyFinder, CompletionCallbackFuzzyFallback) {
    int ctx = 0;
    // "hsty" has no direct prefix match but should fuzzy-rank "history".
    auto completions = completion_callback("hsty", ctx);
    bool found_history = false;
    for (size_t i = 0; i < completions.size(); i++) {
        if (completions[i].text() == "history") { found_history = true; break; }
    }
    EXPECT_TRUE(found_history);
}

TEST(FuzzyFinder, CompletionCallbackPrefixWins) {
    int ctx = 0;
    // "hist" is a real prefix match — we should NOT activate fuzzy fallback.
    auto completions = completion_callback("hist", ctx);
    ASSERT_FALSE(completions.empty());
    EXPECT_EQ(completions[0].text(), "history");
}
