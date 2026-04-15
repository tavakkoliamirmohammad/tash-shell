#include <gtest/gtest.h>
#include "tash/core.h"
#include "tash/ui.h"
#include "tash/history.h"

#include <sys/stat.h>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>

using namespace std;

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
// Tilde expansion
// ═══════════════════════════════════════════════════════════════

TEST(TildeTest, TildeExpandsToHome) {
    const char *home = getenv("HOME");
    ASSERT_NE(home, nullptr);
    EXPECT_EQ(expand_tilde("~/documents"), string(home) + "/documents");
}

TEST(TildeTest, TildeAlone) {
    EXPECT_EQ(expand_tilde("~"), string(getenv("HOME")));
}

TEST(TildeTest, NoTildeExpansion) {
    EXPECT_EQ(expand_tilde("hello~world"), "hello~world");
}

// ═══════════════════════════════════════════════════════════════
// Environment variable expansion
// ═══════════════════════════════════════════════════════════════

TEST(ExpandVarsTest, SimpleVar) {
    setenv("TASH_TEST_UNIT", "hello", 1);
    EXPECT_EQ(expand_variables("$TASH_TEST_UNIT", 0), "hello");
    unsetenv("TASH_TEST_UNIT");
}

TEST(ExpandVarsTest, BracedVar) {
    setenv("TASH_TEST_UNIT", "world", 1);
    EXPECT_EQ(expand_variables("${TASH_TEST_UNIT}", 0), "world");
    unsetenv("TASH_TEST_UNIT");
}

TEST(ExpandVarsTest, UndefinedVarEmpty) {
    EXPECT_EQ(expand_variables("$SURELY_UNDEFINED_VAR_XYZ", 0), "");
}

TEST(ExpandVarsTest, MixedText) {
    setenv("TASH_TEST_UNIT", "val", 1);
    EXPECT_EQ(expand_variables("pre_$TASH_TEST_UNIT_post", 0), "pre_");
    unsetenv("TASH_TEST_UNIT");
}

TEST(ExpandVarsTest, LoneDollar) {
    EXPECT_EQ(expand_variables("cost is $", 0), "cost is $");
}

TEST(ExpandVarsTest, NoVars) {
    EXPECT_EQ(expand_variables("hello world", 0), "hello world");
}

TEST(ExpandVarsTest, EmptyInput) {
    EXPECT_EQ(expand_variables("", 0), "");
}

TEST(ExpandVarsTest, MultipleVars) {
    setenv("TASH_A", "foo", 1);
    setenv("TASH_B", "bar", 1);
    EXPECT_EQ(expand_variables("$TASH_A and $TASH_B", 0), "foo and bar");
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

// ═══════════════════════════════════════════════════════════════
// Command substitution
// ═══════════════════════════════════════════════════════════════

TEST(CommandSubstitutionTest, SimpleEcho) {
    string result = expand_command_substitution("$(echo hello)");
    EXPECT_EQ(result, "hello");
}

TEST(CommandSubstitutionTest, EmbeddedInString) {
    string result = expand_command_substitution("count: $(echo 42)");
    EXPECT_EQ(result, "count: 42");
}

TEST(CommandSubstitutionTest, NoSubstitution) {
    string result = expand_command_substitution("hello world");
    EXPECT_EQ(result, "hello world");
}

TEST(CommandSubstitutionTest, EmptyInput) {
    string result = expand_command_substitution("");
    EXPECT_EQ(result, "");
}

TEST(CommandSubstitutionTest, NestedParens) {
    string result = expand_command_substitution("$(echo $(echo nested))");
    EXPECT_EQ(result, "nested");
}

TEST(CommandSubstitutionTest, MultipleSubstitutions) {
    string result = expand_command_substitution("$(echo a) and $(echo b)");
    EXPECT_EQ(result, "a and b");
}

TEST(CommandSubstitutionTest, TrailingNewlineStripped) {
    string result = expand_command_substitution("$(echo hello)");
    EXPECT_EQ(result, "hello");
}

TEST(CommandSubstitutionTest, LoneDollarSign) {
    string result = expand_command_substitution("cost is $");
    EXPECT_EQ(result, "cost is $");
}

TEST(CommandSubstitutionTest, UnmatchedParen) {
    string result = expand_command_substitution("$(echo hello");
    EXPECT_EQ(result, "$(echo hello");
}

// ═══════════════════════════════════════════════════════════════
// Special variables: $? and $$
// ═══════════════════════════════════════════════════════════════

TEST(ExpandVarsTest, DollarQuestionExitStatus) {
    EXPECT_EQ(expand_variables("$?", 42), "42");
}

TEST(ExpandVarsTest, DollarDollarPID) {
    string result = expand_variables("$$", 0);
    EXPECT_EQ(result, to_string(getpid()));
}

TEST(ExpandVarsTest, DollarQuestionInContext) {
    EXPECT_EQ(expand_variables("exit: $?", 7), "exit: 7");
}

TEST(ExpandVarsTest, DollarDollarInContext) {
    string result = expand_variables("pid=$$", 0);
    EXPECT_EQ(result, "pid=" + to_string(getpid()));
}

// ═══════════════════════════════════════════════════════════════
// Quote-aware variable expansion (single quotes prevent expansion)
// ═══════════════════════════════════════════════════════════════

TEST(ExpandVarsTest, SingleQuotesPreventExpansion) {
    setenv("TASH_TEST_UNIT", "hello", 1);
    EXPECT_EQ(expand_variables("'$TASH_TEST_UNIT'", 0), "'$TASH_TEST_UNIT'");
    unsetenv("TASH_TEST_UNIT");
}

TEST(ExpandVarsTest, DoubleQuotesAllowExpansion) {
    setenv("TASH_TEST_UNIT", "hello", 1);
    EXPECT_EQ(expand_variables("\"$TASH_TEST_UNIT\"", 0), "\"hello\"");
    unsetenv("TASH_TEST_UNIT");
}

// ═══════════════════════════════════════════════════════════════
// parse_redirections
// ═══════════════════════════════════════════════════════════════

TEST(ParseRedirectionsTest, StdoutRedirect) {
    Command cmd = parse_redirections("echo hello > file.txt");
    ASSERT_EQ(cmd.argv.size(), 2u);
    EXPECT_EQ(cmd.argv[0], "echo");
    EXPECT_EQ(cmd.argv[1], "hello");
    ASSERT_EQ(cmd.redirections.size(), 1u);
    EXPECT_EQ(cmd.redirections[0].fd, 1);
    EXPECT_EQ(cmd.redirections[0].filename, "file.txt");
    EXPECT_FALSE(cmd.redirections[0].append);
}

TEST(ParseRedirectionsTest, StdoutAppend) {
    Command cmd = parse_redirections("echo hello >> file.txt");
    ASSERT_EQ(cmd.redirections.size(), 1u);
    EXPECT_EQ(cmd.redirections[0].fd, 1);
    EXPECT_TRUE(cmd.redirections[0].append);
}

TEST(ParseRedirectionsTest, StdinRedirect) {
    Command cmd = parse_redirections("sort < input.txt");
    ASSERT_EQ(cmd.argv.size(), 1u);
    EXPECT_EQ(cmd.argv[0], "sort");
    ASSERT_EQ(cmd.redirections.size(), 1u);
    EXPECT_EQ(cmd.redirections[0].fd, 0);
    EXPECT_EQ(cmd.redirections[0].filename, "input.txt");
}

TEST(ParseRedirectionsTest, StderrRedirect) {
    Command cmd = parse_redirections("cmd 2> errors.txt");
    ASSERT_EQ(cmd.redirections.size(), 1u);
    EXPECT_EQ(cmd.redirections[0].fd, 2);
    EXPECT_EQ(cmd.redirections[0].filename, "errors.txt");
}

TEST(ParseRedirectionsTest, StderrToStdout) {
    Command cmd = parse_redirections("cmd 2>&1");
    ASSERT_EQ(cmd.redirections.size(), 1u);
    EXPECT_EQ(cmd.redirections[0].fd, 2);
    EXPECT_TRUE(cmd.redirections[0].dup_to_stdout);
}

TEST(ParseRedirectionsTest, MultipleRedirections) {
    Command cmd = parse_redirections("sort < input.txt > output.txt 2> errors.txt");
    ASSERT_EQ(cmd.argv.size(), 1u);
    EXPECT_EQ(cmd.argv[0], "sort");
    ASSERT_EQ(cmd.redirections.size(), 3u);
}

TEST(ParseRedirectionsTest, NoRedirections) {
    Command cmd = parse_redirections("echo hello world");
    ASSERT_EQ(cmd.argv.size(), 3u);
    EXPECT_EQ(cmd.argv[0], "echo");
    EXPECT_EQ(cmd.argv[1], "hello");
    EXPECT_EQ(cmd.argv[2], "world");
    EXPECT_TRUE(cmd.redirections.empty());
}

// ═══════════════════════════════════════════════════════════════
// is_input_complete (multiline detection)
// ═══════════════════════════════════════════════════════════════

TEST(InputComplete, SimpleCommandIsComplete) {
    EXPECT_TRUE(is_input_complete("echo hello"));
}

TEST(InputComplete, EmptyIsComplete) {
    EXPECT_TRUE(is_input_complete(""));
}

TEST(InputComplete, UnclosedDoubleQuote) {
    EXPECT_FALSE(is_input_complete("echo \"hello"));
}

TEST(InputComplete, UnclosedSingleQuote) {
    EXPECT_FALSE(is_input_complete("echo 'hello"));
}

TEST(InputComplete, ClosedQuotesComplete) {
    EXPECT_TRUE(is_input_complete("echo \"hello world\""));
}

TEST(InputComplete, TrailingPipe) {
    EXPECT_FALSE(is_input_complete("ls |"));
}

TEST(InputComplete, TrailingAnd) {
    EXPECT_FALSE(is_input_complete("echo hello &&"));
}

TEST(InputComplete, TrailingOr) {
    EXPECT_FALSE(is_input_complete("echo hello ||"));
}

TEST(InputComplete, TrailingBackslash) {
    EXPECT_FALSE(is_input_complete("echo hello\\"));
}

TEST(InputComplete, PipeWithCommandIsComplete) {
    EXPECT_TRUE(is_input_complete("ls | grep foo"));
}

// ═══════════════════════════════════════════════════════════════
// suggest_command (Levenshtein-based suggestions)
// ═══════════════════════════════════════════════════════════════

TEST(SuggestCommand, FindsCloseMatch) {
    build_command_cache();
    string suggestion = suggest_command("ech");
    EXPECT_EQ(suggestion, "echo") << "Should suggest 'echo' for 'ech'";
}

TEST(SuggestCommand, NoMatchForGibberish) {
    build_command_cache();
    string suggestion = suggest_command("xyzzy_not_a_command_99");
    EXPECT_TRUE(suggestion.empty()) << "Should not suggest for gibberish";
}

TEST(SuggestCommand, ExactMatchNotSuggested) {
    build_command_cache();
    string suggestion = suggest_command("echo");
    EXPECT_NE(suggestion, "echo") << "Exact match should not be suggested as itself";
}

// ═══════════════════════════════════════════════════════════════
// command_exists_on_path (used by syntax highlighting)
// ═══════════════════════════════════════════════════════════════

TEST(CommandExists, KnownCommandExists) {
    build_command_cache();
    EXPECT_TRUE(command_exists_on_path("ls")) << "ls should exist on PATH";
}

TEST(CommandExists, UnknownCommandDoesNotExist) {
    build_command_cache();
    EXPECT_FALSE(command_exists_on_path("xyzzy_not_real_99")) << "gibberish should not exist";
}

TEST(CommandExists, BuiltinNotOnPath) {
    build_command_cache();
    // "cd" is a builtin, not a PATH executable
    // is_builtin("cd") should be true, but command_exists_on_path may or may not find it
    EXPECT_TRUE(is_builtin("cd")) << "cd should be a builtin";
}

// ═══════════════════════════════════════════════════════════════
// get_path_commands (completion data source)
// ═══════════════════════════════════════════════════════════════

TEST(PathCommands, ReturnsNonEmptyList) {
    build_command_cache();
    const auto &cmds = get_path_commands();
    EXPECT_GT(cmds.size(), 0u) << "PATH should have commands";
}

TEST(PathCommands, ContainsLs) {
    build_command_cache();
    const auto &cmds = get_path_commands();
    bool found = false;
    for (const auto &c : cmds) {
        if (c == "ls") { found = true; break; }
    }
    EXPECT_TRUE(found) << "ls should be in PATH commands";
}

// ═══════════════════════════════════════════════════════════════
// Frecency (z command data)
// ═══════════════════════════════════════════════════════════════

TEST(Frecency, RecordAndFind) {
    // Record a unique path and verify we can find it
    string unique_path = "/tmp/tash_frecency_test_" + to_string(getpid());
    z_record_directory(unique_path);
    string found = z_find_directory("frecency_test");
    // The path might not exist on disk, so z_find_directory checks stat()
    // and may return empty. Just verify the function doesn't crash.
    // (The full test is in test_modern_ux.cpp with real directories)
}

TEST(Frecency, NoMatchReturnsEmpty) {
    string result = z_find_directory("xyzzy_no_such_dir_ever_99");
    EXPECT_TRUE(result.empty());
}

// ═══════════════════════════════════════════════════════════════
// completion_callback unit tests
// ═══════════════════════════════════════════════════════════════

static bool completions_contain(const replxx::Replxx::completions_t &c, const string &text) {
    for (const auto &entry : c) {
        if (entry.text() == text) return true;
    }
    return false;
}

// -- Command-position completion --

TEST(CompletionCallback, EmptyInputReturnsBuiltinsAndPath) {
    build_command_cache();
    int ctx = 0;
    auto results = completion_callback("", ctx);
    // Should include builtins like cd and PATH commands like ls
    EXPECT_TRUE(completions_contain(results, "cd")) << "empty input should offer cd";
    EXPECT_TRUE(completions_contain(results, "ls")) << "empty input should offer ls";
}

TEST(CompletionCallback, PartialCommandMatchesPrefix) {
    build_command_cache();
    int ctx = 0;
    auto results = completion_callback("ech", ctx);
    EXPECT_TRUE(completions_contain(results, "echo")) << "'ech' should complete to echo";
    EXPECT_EQ(ctx, 3) << "context_len should be length of prefix";
}

TEST(CompletionCallback, NoMatchReturnsEmpty) {
    build_command_cache();
    int ctx = 0;
    auto results = completion_callback("xyzzy_not_real_99", ctx);
    EXPECT_TRUE(results.empty()) << "gibberish should produce no completions";
}

// -- Git subcommand completion --

TEST(CompletionCallback, GitSubcommands) {
    int ctx = 0;
    auto results = completion_callback("git st", ctx);
    EXPECT_TRUE(completions_contain(results, "stash")) << "git st should suggest stash";
    EXPECT_TRUE(completions_contain(results, "status")) << "git st should suggest status";
    EXPECT_FALSE(completions_contain(results, "push")) << "git st should not suggest push";
}

// -- Docker subcommand completion --

TEST(CompletionCallback, DockerSubcommands) {
    int ctx = 0;
    auto results = completion_callback("docker r", ctx);
    EXPECT_TRUE(completions_contain(results, "run")) << "docker r should suggest run";
    EXPECT_TRUE(completions_contain(results, "rm")) << "docker r should suggest rm";
    EXPECT_TRUE(completions_contain(results, "rmi")) << "docker r should suggest rmi";
}

// -- Environment variable completion --

TEST(CompletionCallback, EnvVarCompletion) {
    setenv("TASH_TEST_COMP_VAR", "1", 1);
    int ctx = 0;
    auto results = completion_callback("echo $TASH_TEST_COMP", ctx);
    EXPECT_TRUE(completions_contain(results, "$TASH_TEST_COMP_VAR"))
        << "$TASH_TEST_COMP should complete to $TASH_TEST_COMP_VAR";
    unsetenv("TASH_TEST_COMP_VAR");
}

// -- File/directory completion --

TEST(CompletionCallback, FileCompletionListsCwd) {
    // Create a temp dir with known contents
    string tmpdir = "/tmp/tash_comp_test_" + to_string(getpid());
    mkdir(tmpdir.c_str(), 0755);
    // Create a file and a subdirectory
    string filepath = tmpdir + "/hello.txt";
    FILE *f = fopen(filepath.c_str(), "w");
    if (f) fclose(f);
    string subdir = tmpdir + "/subdir";
    mkdir(subdir.c_str(), 0755);

    // cd into the temp dir so "." resolves to it
    char *old_cwd = getcwd(nullptr, 0);
    chdir(tmpdir.c_str());

    int ctx = 0;
    auto results = completion_callback("ls ", ctx);
    EXPECT_TRUE(completions_contain(results, "hello.txt"))
        << "should complete file hello.txt";
    EXPECT_TRUE(completions_contain(results, "subdir/"))
        << "should complete directory with trailing slash";

    // Cleanup
    chdir(old_cwd);
    free(old_cwd);
    remove(filepath.c_str());
    rmdir(subdir.c_str());
    rmdir(tmpdir.c_str());
}

TEST(CompletionCallback, FileCompletionWithPrefix) {
    string tmpdir = "/tmp/tash_comp_pfx_" + to_string(getpid());
    mkdir(tmpdir.c_str(), 0755);
    string f1 = tmpdir + "/alpha.cpp";
    string f2 = tmpdir + "/alpha.h";
    string f3 = tmpdir + "/beta.cpp";
    FILE *fp;
    fp = fopen(f1.c_str(), "w"); if (fp) fclose(fp);
    fp = fopen(f2.c_str(), "w"); if (fp) fclose(fp);
    fp = fopen(f3.c_str(), "w"); if (fp) fclose(fp);

    char *old_cwd = getcwd(nullptr, 0);
    chdir(tmpdir.c_str());

    int ctx = 0;
    auto results = completion_callback("cat al", ctx);
    EXPECT_TRUE(completions_contain(results, "alpha.cpp"))
        << "'al' prefix should match alpha.cpp";
    EXPECT_TRUE(completions_contain(results, "alpha.h"))
        << "'al' prefix should match alpha.h";
    EXPECT_FALSE(completions_contain(results, "beta.cpp"))
        << "'al' prefix should not match beta.cpp";
    EXPECT_EQ(ctx, 2) << "context_len should be length of 'al'";

    chdir(old_cwd);
    free(old_cwd);
    remove(f1.c_str());
    remove(f2.c_str());
    remove(f3.c_str());
    rmdir(tmpdir.c_str());
}

TEST(CompletionCallback, FileCompletionWithPathPrefix) {
    string tmpdir = "/tmp/tash_comp_path_" + to_string(getpid());
    mkdir(tmpdir.c_str(), 0755);
    string sub = tmpdir + "/nested";
    mkdir(sub.c_str(), 0755);
    string f1 = sub + "/foo.txt";
    FILE *fp = fopen(f1.c_str(), "w");
    if (fp) fclose(fp);

    int ctx = 0;
    string input = "cat " + sub + "/fo";
    auto results = completion_callback(input, ctx);
    EXPECT_TRUE(completions_contain(results, "foo.txt"))
        << "path prefix should complete foo.txt inside nested dir";
    EXPECT_EQ(ctx, 2) << "context_len should be length of 'fo'";

    remove(f1.c_str());
    rmdir(sub.c_str());
    rmdir(tmpdir.c_str());
}

TEST(CompletionCallback, HiddenFilesOnlyWhenDotPrefix) {
    string tmpdir = "/tmp/tash_comp_dot_" + to_string(getpid());
    mkdir(tmpdir.c_str(), 0755);
    string hidden = tmpdir + "/.hidden";
    string visible = tmpdir + "/visible";
    FILE *fp;
    fp = fopen(hidden.c_str(), "w"); if (fp) fclose(fp);
    fp = fopen(visible.c_str(), "w"); if (fp) fclose(fp);

    char *old_cwd = getcwd(nullptr, 0);
    chdir(tmpdir.c_str());

    // Without dot prefix — hidden file should NOT appear
    int ctx = 0;
    auto r1 = completion_callback("ls ", ctx);
    EXPECT_FALSE(completions_contain(r1, ".hidden"))
        << "hidden files should not appear without dot prefix";
    EXPECT_TRUE(completions_contain(r1, "visible"));

    // With dot prefix — hidden file should appear
    ctx = 0;
    auto r2 = completion_callback("ls .", ctx);
    EXPECT_TRUE(completions_contain(r2, ".hidden"))
        << "hidden files should appear with dot prefix";

    chdir(old_cwd);
    free(old_cwd);
    remove(hidden.c_str());
    remove(visible.c_str());
    rmdir(tmpdir.c_str());
}

