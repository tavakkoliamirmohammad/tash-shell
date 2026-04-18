// Property-based tests for parser invariants.
//
// Instead of writing one test per specific input, declare a property
// (e.g. "tokenizing then rejoining a single-quoted string preserves
// it") and run it against many random inputs. The parser bugs that
// PR #94 fixed — globs expanding inside quotes, `\*` not surviving —
// are exactly the kind of thing a property sweep surfaces quickly.
//
// Hand-rolled, no rapidcheck dep. Each property uses a deterministic
// seeded RNG so failures reproduce.

#include "tash/core/parser.h"
#include <gtest/gtest.h>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr int ITERATIONS = 200;
constexpr uint32_t SEED = 0xBEEF5EED;

std::string random_safe_word(std::mt19937 &rng, int max_len = 8) {
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz_0123456789";
    std::uniform_int_distribution<int> len_d(1, max_len);
    std::uniform_int_distribution<int> char_d(0, (int)sizeof(alphabet) - 2);
    std::string w;
    int n = len_d(rng);
    for (int i = 0; i < n; ++i) w += alphabet[char_d(rng)];
    return w;
}

} // namespace

// ── Property: parsing and reassembling an echo command round-trips
//    the word list (no word gets dropped, split, or merged). ─────────
TEST(ParserProperties, EchoRoundTripsWordList) {
    std::mt19937 rng(SEED);
    for (int i = 0; i < ITERATIONS; ++i) {
        std::uniform_int_distribution<int> word_count_d(1, 6);
        int n = word_count_d(rng);
        std::vector<std::string> words;
        std::string cmd = "echo";
        for (int j = 0; j < n; ++j) {
            std::string w = random_safe_word(rng);
            words.push_back(w);
            cmd += " " + w;
        }
        Command parsed = parse_redirections(cmd);
        ASSERT_EQ(parsed.argv.size(), words.size() + 1)
            << "word count mismatch for: " << cmd;
        EXPECT_EQ(parsed.argv[0], "echo");
        for (size_t k = 0; k < words.size(); ++k) {
            EXPECT_EQ(parsed.argv[k + 1], words[k])
                << "word " << k << " differs for: " << cmd;
        }
    }
}

// ── Property: tokens inside single quotes are never split by
//    tokenize_string, regardless of embedded spaces or operators. ─────
TEST(ParserProperties, SingleQuotedTokenStaysOneToken) {
    std::mt19937 rng(SEED ^ 0x1);
    for (int i = 0; i < ITERATIONS; ++i) {
        // Build an inner string with spaces and benign operators.
        std::string inner;
        std::uniform_int_distribution<int> parts_d(2, 4);
        int n = parts_d(rng);
        for (int j = 0; j < n; ++j) {
            if (j) inner += " ";
            inner += random_safe_word(rng);
        }
        std::string quoted = "'" + inner + "'";
        std::string cmd = "echo " + quoted;
        Command parsed = parse_redirections(cmd);
        ASSERT_EQ(parsed.argv.size(), 2u) << "failed on: " << cmd;
        EXPECT_EQ(parsed.argv[1], inner) << "inner changed for: " << cmd;
    }
}

// ── Property: a glob metachar inside single quotes must NOT trigger
//    expansion. Regression contract for PR #94. ─────────────────────
TEST(ParserProperties, QuotedGlobStaysLiteral) {
    std::mt19937 rng(SEED ^ 0x2);
    for (int i = 0; i < ITERATIONS; ++i) {
        std::string inner = random_safe_word(rng) + "*" + random_safe_word(rng);
        std::string cmd = "echo '" + inner + "'";
        Command parsed = parse_redirections(cmd);
        ASSERT_EQ(parsed.argv.size(), 2u);
        // After parse the inner should still have its literal `*`.
        EXPECT_NE(parsed.argv[1].find('*'), std::string::npos)
            << "quoted * got lost in: " << cmd;
        std::vector<std::string> expanded =
            expand_globs(parsed.argv, parsed.argv_quoted);
        EXPECT_EQ(expanded[1], inner)
            << "quoted glob expanded despite quotes: " << cmd;
    }
}

// ── Property: a trailing `|` (incomplete command) is consistently
//    classified as not input-complete — the REPL relies on this to
//    keep prompting. ─────────────────────────────────────────────────
TEST(ParserProperties, TrailingPipeIsNotComplete) {
    std::mt19937 rng(SEED ^ 0x3);
    for (int i = 0; i < ITERATIONS; ++i) {
        std::string cmd = random_safe_word(rng) + " |";
        EXPECT_FALSE(is_input_complete(cmd)) << "should be incomplete: " << cmd;
    }
}

// ── Property: a balanced quote pair always produces complete input;
//    an unbalanced one always doesn't. ─────────────────────────────
TEST(ParserProperties, QuoteBalanceDeterminesCompleteness) {
    std::mt19937 rng(SEED ^ 0x4);
    for (int i = 0; i < ITERATIONS; ++i) {
        std::string word = random_safe_word(rng);
        std::string balanced = "echo '" + word + "'";
        std::string unbalanced = "echo '" + word;
        EXPECT_TRUE(is_input_complete(balanced))
            << "balanced single-quote marked incomplete: " << balanced;
        EXPECT_FALSE(is_input_complete(unbalanced))
            << "unbalanced single-quote marked complete: " << unbalanced;
    }
}

// ── Property: parse_command_line never drops a semicolon-separated
//    segment. n non-empty commands separated by `;` → n segments. ─────
TEST(ParserProperties, SemicolonSplitSegmentCount) {
    std::mt19937 rng(SEED ^ 0x5);
    for (int i = 0; i < ITERATIONS; ++i) {
        std::uniform_int_distribution<int> n_d(1, 5);
        int n = n_d(rng);
        std::string line;
        for (int j = 0; j < n; ++j) {
            if (j) line += "; ";
            line += "echo " + random_safe_word(rng);
        }
        std::vector<CommandSegment> segs = parse_command_line(line);
        EXPECT_EQ((int)segs.size(), n) << "segment count off for: " << line;
    }
}

// ── Property: expand_globs is a no-op (identity) on inputs that
//    contain no glob metacharacters, regardless of quote state. ──────
TEST(ParserProperties, NoGlobMetaMeansIdentity) {
    std::mt19937 rng(SEED ^ 0x6);
    for (int i = 0; i < ITERATIONS; ++i) {
        std::vector<std::string> args;
        std::uniform_int_distribution<int> n_d(1, 5);
        int n = n_d(rng);
        for (int j = 0; j < n; ++j) args.push_back(random_safe_word(rng));
        std::vector<bool> quoted(args.size(), false);
        auto out = expand_globs(args, quoted);
        EXPECT_EQ(out, args);
    }
}
