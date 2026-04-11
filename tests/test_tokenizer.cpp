#include <gtest/gtest.h>
#include "shell.h"

// ═══════════════════════════════════════════════════════════════
// Trim tests
// ═══════════════════════════════════════════════════════════════

TEST(TrimTest, TrimWhitespace) {
    string s = "  hello  ";
    EXPECT_EQ(trim(s), "hello");
}

TEST(TrimTest, TrimTabs) {
    string s = "\thello\t";
    EXPECT_EQ(trim(s), "hello");
}

TEST(TrimTest, TrimNewlines) {
    string s = "\nhello\n";
    EXPECT_EQ(trim(s), "hello");
}

TEST(TrimTest, NoTrimNeeded) {
    string s = "hello";
    EXPECT_EQ(trim(s), "hello");
}

TEST(TrimTest, EmptyString) {
    string s = "";
    EXPECT_EQ(trim(s), "");
}

TEST(TrimTest, AllWhitespace) {
    string s = "   \t\n  ";
    EXPECT_EQ(trim(s), "");
}

TEST(TrimTest, LtrimOnly) {
    string s = "  hello";
    EXPECT_EQ(ltrim(s), "hello");
}

TEST(TrimTest, RtrimOnly) {
    string s = "hello  ";
    EXPECT_EQ(rtrim(s), "hello");
}

// ═══════════════════════════════════════════════════════════════
// Tokenizer: space delimiter
// ═══════════════════════════════════════════════════════════════

TEST(TokenizerSpaceTest, BasicSplit) {
    auto tokens = tokenize_string("echo hello world", " ");
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0], "echo");
    EXPECT_EQ(tokens[1], "hello");
    EXPECT_EQ(tokens[2], "world");
}

TEST(TokenizerSpaceTest, ExtraWhitespace) {
    auto tokens = tokenize_string("  echo   hello   ", " ");
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0], "echo");
    EXPECT_EQ(tokens[1], "hello");
}

TEST(TokenizerSpaceTest, SingleToken) {
    auto tokens = tokenize_string("ls", " ");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0], "ls");
}

TEST(TokenizerSpaceTest, EmptyInput) {
    auto tokens = tokenize_string("", " ");
    EXPECT_TRUE(tokens.empty());
}

TEST(TokenizerSpaceTest, OnlySpaces) {
    auto tokens = tokenize_string("   ", " ");
    EXPECT_TRUE(tokens.empty());
}

TEST(TokenizerSpaceTest, DoubleQuotedString) {
    auto tokens = tokenize_string("echo \"hello world\"", " ");
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0], "echo");
    EXPECT_EQ(tokens[1], "\"hello world\"");
}

TEST(TokenizerSpaceTest, SingleQuotedString) {
    auto tokens = tokenize_string("echo 'hello world'", " ");
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0], "echo");
    EXPECT_EQ(tokens[1], "'hello world'");
}

TEST(TokenizerSpaceTest, EscapedQuote) {
    auto tokens = tokenize_string("echo \\\"hello", " ");
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0], "echo");
    EXPECT_EQ(tokens[1], "\"hello");
}

TEST(TokenizerSpaceTest, EscapedBackslash) {
    auto tokens = tokenize_string("echo \\\\hello", " ");
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0], "echo");
    EXPECT_EQ(tokens[1], "\\hello");
}

// ═══════════════════════════════════════════════════════════════
// Tokenizer: && delimiter
// ═══════════════════════════════════════════════════════════════

TEST(TokenizerAndTest, TwoCommands) {
    auto tokens = tokenize_string("ls && pwd", "&&");
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0], "ls");
    EXPECT_EQ(tokens[1], "pwd");
}

TEST(TokenizerAndTest, ThreeCommands) {
    auto tokens = tokenize_string("echo a && echo b && echo c", "&&");
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0], "echo a");
    EXPECT_EQ(tokens[1], "echo b");
    EXPECT_EQ(tokens[2], "echo c");
}

TEST(TokenizerAndTest, NoDelimiter) {
    auto tokens = tokenize_string("echo hello", "&&");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0], "echo hello");
}

TEST(TokenizerAndTest, QuotedAndInside) {
    auto tokens = tokenize_string("echo \"a && b\"", "&&");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0], "echo \"a && b\"");
}

// ═══════════════════════════════════════════════════════════════
// Tokenizer: > delimiter (redirection)
// ═══════════════════════════════════════════════════════════════

TEST(TokenizerRedirectTest, BasicRedirect) {
    auto tokens = tokenize_string("echo hello > file.txt", ">");
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0], "echo hello");
    EXPECT_EQ(tokens[1], "file.txt");
}

TEST(TokenizerRedirectTest, NoRedirect) {
    auto tokens = tokenize_string("echo hello", ">");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0], "echo hello");
}

TEST(TokenizerRedirectTest, QuotedRedirect) {
    auto tokens = tokenize_string("echo \"a > b\"", ">");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0], "echo \"a > b\"");
}

// ═══════════════════════════════════════════════════════════════
// Tokenizer: | delimiter (pipe)
// ═══════════════════════════════════════════════════════════════

TEST(TokenizerPipeTest, BasicPipe) {
    auto tokens = tokenize_string("ls | grep cpp", "|");
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0], "ls");
    EXPECT_EQ(tokens[1], "grep cpp");
}

TEST(TokenizerPipeTest, TriplePipe) {
    auto tokens = tokenize_string("ls | grep cpp | wc -l", "|");
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0], "ls");
    EXPECT_EQ(tokens[1], "grep cpp");
    EXPECT_EQ(tokens[2], "wc -l");
}

TEST(TokenizerPipeTest, QuotedPipe) {
    auto tokens = tokenize_string("echo \"a | b\" | cat", "|");
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0], "echo \"a | b\"");
    EXPECT_EQ(tokens[1], "cat");
}

// ═══════════════════════════════════════════════════════════════
// Tokenizer: tilde expansion
// ═══════════════════════════════════════════════════════════════

TEST(TokenizerTildeTest, TildeExpandsToHome) {
    auto tokens = tokenize_string("~/documents", " ");
    ASSERT_EQ(tokens.size(), 1u);
    const char *home = getenv("HOME");
    ASSERT_NE(home, nullptr);
    EXPECT_EQ(tokens[0], string(home) + "/documents");
}

TEST(TokenizerTildeTest, TildeAlone) {
    auto tokens = tokenize_string("~", " ");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0], string(getenv("HOME")));
}

TEST(TokenizerTildeTest, NoTildeExpansion) {
    auto tokens = tokenize_string("echo hello~world", " ");
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[1], "hello~world");
}
