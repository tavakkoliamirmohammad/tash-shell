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

// ═══════════════════════════════════════════════════════════════
// Environment variable expansion
// ═══════════════════════════════════════════════════════════════

TEST(ExpandVarsTest, SimpleVar) {
    setenv("TASH_TEST_UNIT", "hello", 1);
    EXPECT_EQ(expand_variables("$TASH_TEST_UNIT"), "hello");
    unsetenv("TASH_TEST_UNIT");
}

TEST(ExpandVarsTest, BracedVar) {
    setenv("TASH_TEST_UNIT", "world", 1);
    EXPECT_EQ(expand_variables("${TASH_TEST_UNIT}"), "world");
    unsetenv("TASH_TEST_UNIT");
}

TEST(ExpandVarsTest, UndefinedVarEmpty) {
    EXPECT_EQ(expand_variables("$SURELY_UNDEFINED_VAR_XYZ"), "");
}

TEST(ExpandVarsTest, MixedText) {
    setenv("TASH_TEST_UNIT", "val", 1);
    EXPECT_EQ(expand_variables("pre_$TASH_TEST_UNIT_post"), "pre_");
    // Note: TASH_TEST_UNIT_post is treated as one var name (includes _post)
    unsetenv("TASH_TEST_UNIT");
}

TEST(ExpandVarsTest, LoneDollar) {
    EXPECT_EQ(expand_variables("cost is $"), "cost is $");
}

TEST(ExpandVarsTest, NoVars) {
    EXPECT_EQ(expand_variables("hello world"), "hello world");
}

TEST(ExpandVarsTest, EmptyInput) {
    EXPECT_EQ(expand_variables(""), "");
}

TEST(ExpandVarsTest, MultipleVars) {
    setenv("TASH_A", "foo", 1);
    setenv("TASH_B", "bar", 1);
    EXPECT_EQ(expand_variables("$TASH_A and $TASH_B"), "foo and bar");
    unsetenv("TASH_A");
    unsetenv("TASH_B");
}

// ═══════════════════════════════════════════════════════════════
// parse_command_line
// ═══════════════════════════════════════════════════════════════

TEST(ParseCommandLineTest, SingleCommand) {
    auto segs = parse_command_line("echo hello");
    ASSERT_EQ(segs.size(), 1u);
    EXPECT_EQ(segs[0].command, "echo hello");
    EXPECT_EQ(segs[0].op, OP_NONE);
}

TEST(ParseCommandLineTest, AndOperator) {
    auto segs = parse_command_line("echo a && echo b");
    ASSERT_EQ(segs.size(), 2u);
    EXPECT_EQ(segs[0].command, "echo a");
    EXPECT_EQ(segs[0].op, OP_NONE);
    EXPECT_EQ(segs[1].command, "echo b");
    EXPECT_EQ(segs[1].op, OP_AND);
}

TEST(ParseCommandLineTest, OrOperator) {
    auto segs = parse_command_line("false || echo fallback");
    ASSERT_EQ(segs.size(), 2u);
    EXPECT_EQ(segs[0].command, "false");
    EXPECT_EQ(segs[1].command, "echo fallback");
    EXPECT_EQ(segs[1].op, OP_OR);
}

TEST(ParseCommandLineTest, SemicolonOperator) {
    auto segs = parse_command_line("echo a ; echo b");
    ASSERT_EQ(segs.size(), 2u);
    EXPECT_EQ(segs[0].command, "echo a");
    EXPECT_EQ(segs[1].command, "echo b");
    EXPECT_EQ(segs[1].op, OP_SEMICOLON);
}

TEST(ParseCommandLineTest, MixedOperators) {
    auto segs = parse_command_line("echo a && echo b || echo c ; echo d");
    ASSERT_EQ(segs.size(), 4u);
    EXPECT_EQ(segs[0].op, OP_NONE);
    EXPECT_EQ(segs[1].op, OP_AND);
    EXPECT_EQ(segs[2].op, OP_OR);
    EXPECT_EQ(segs[3].op, OP_SEMICOLON);
}

TEST(ParseCommandLineTest, QuotedOperatorsNotSplit) {
    auto segs = parse_command_line("echo \"a && b\"");
    ASSERT_EQ(segs.size(), 1u);
    EXPECT_EQ(segs[0].command, "echo \"a && b\"");
}

TEST(ParseCommandLineTest, EmptyInput) {
    auto segs = parse_command_line("");
    EXPECT_TRUE(segs.empty());
}

// ═══════════════════════════════════════════════════════════════
// strip_quotes
// ═══════════════════════════════════════════════════════════════

TEST(StripQuotesTest, DoubleQuotes) {
    EXPECT_EQ(strip_quotes("\"hello world\""), "hello world");
}

TEST(StripQuotesTest, SingleQuotes) {
    EXPECT_EQ(strip_quotes("'hello world'"), "hello world");
}

TEST(StripQuotesTest, NoQuotes) {
    EXPECT_EQ(strip_quotes("hello"), "hello");
}

TEST(StripQuotesTest, EmptyString) {
    EXPECT_EQ(strip_quotes(""), "");
}

TEST(StripQuotesTest, MismatchedQuotes) {
    EXPECT_EQ(strip_quotes("\"hello'"), "\"hello'");
}

TEST(StripQuotesTest, SingleChar) {
    EXPECT_EQ(strip_quotes("\""), "\"");
}

TEST(StripQuotesTest, EmptyQuoted) {
    EXPECT_EQ(strip_quotes("\"\""), "");
}

TEST(StripQuotesTest, EmptySingleQuoted) {
    EXPECT_EQ(strip_quotes("''"), "");
}

TEST(StripQuotesTest, NestedQuotes) {
    EXPECT_EQ(strip_quotes("\"it's\""), "it's");
}
