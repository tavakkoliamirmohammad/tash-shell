#include <gtest/gtest.h>
#include "tash/plugins/fish_completion_provider.h"
#include "tash/shell.h"

#include <fstream>
#include <cstdlib>
#include <sys/stat.h>

// ── Helper: create a temporary directory ─────────────────────

static std::string make_temp_dir(const std::string &suffix) {
    std::string base = "/tmp/tash_test_fish_" + suffix + "_XXXXXX";
    std::vector<char> buf(base.begin(), base.end());
    buf.push_back('\0');
    char *result = mkdtemp(buf.data());
    return result ? std::string(result) : "";
}

// ── Helper: write a file ─────────────────────────────────────

static void write_file(const std::string &path,
                       const std::string &content) {
    std::ofstream out(path);
    out << content;
}

// ══════════════════════════════════════════════════════════════
// Parsing tests
// ══════════════════════════════════════════════════════════════

TEST(FishParsingTest, SimpleCompleteLine) {
    FishCompletionEntry entry;
    bool ok = parse_fish_complete_line(
        "complete -c git -s b -l branch -d 'List branches'", entry);
    ASSERT_TRUE(ok);
    EXPECT_EQ(entry.command, "git");
    EXPECT_EQ(entry.short_opt, "b");
    EXPECT_EQ(entry.long_opt, "branch");
    EXPECT_EQ(entry.description, "List branches");
}

TEST(FishParsingTest, ShortOptionOnly) {
    FishCompletionEntry entry;
    bool ok = parse_fish_complete_line(
        "complete -c grep -s i -d 'Ignore case'", entry);
    ASSERT_TRUE(ok);
    EXPECT_EQ(entry.command, "grep");
    EXPECT_EQ(entry.short_opt, "i");
    EXPECT_TRUE(entry.long_opt.empty());
    EXPECT_EQ(entry.description, "Ignore case");
}

TEST(FishParsingTest, LongOptionOnly) {
    FishCompletionEntry entry;
    bool ok = parse_fish_complete_line(
        "complete -c grep -l color -d 'Colorize output'", entry);
    ASSERT_TRUE(ok);
    EXPECT_EQ(entry.command, "grep");
    EXPECT_TRUE(entry.short_opt.empty());
    EXPECT_EQ(entry.long_opt, "color");
    EXPECT_EQ(entry.description, "Colorize output");
}

TEST(FishParsingTest, DescriptionWithDoubleQuotes) {
    FishCompletionEntry entry;
    bool ok = parse_fish_complete_line(
        "complete -c git -s v -d \"Show version\"", entry);
    ASSERT_TRUE(ok);
    EXPECT_EQ(entry.description, "Show version");
}

TEST(FishParsingTest, ArgumentsExtraction) {
    FishCompletionEntry entry;
    bool ok = parse_fish_complete_line(
        "complete -c git -n '__fish_git_needs_command' -a clone "
        "-d 'Clone a repository'", entry);
    ASSERT_TRUE(ok);
    EXPECT_EQ(entry.command, "git");
    EXPECT_EQ(entry.arguments, "clone");
    EXPECT_EQ(entry.description, "Clone a repository");
}

TEST(FishParsingTest, MultipleArguments) {
    FishCompletionEntry entry;
    bool ok = parse_fish_complete_line(
        "complete -c grep -s d -x -a \"read skip recurse\"", entry);
    ASSERT_TRUE(ok);
    EXPECT_EQ(entry.command, "grep");
    EXPECT_EQ(entry.short_opt, "d");
    EXPECT_EQ(entry.arguments, "read skip recurse");
    EXPECT_TRUE(entry.no_files);
    EXPECT_TRUE(entry.requires_arg);
}

TEST(FishParsingTest, NoFilesFlag) {
    FishCompletionEntry entry;
    bool ok = parse_fish_complete_line(
        "complete -c git -f -a status", entry);
    ASSERT_TRUE(ok);
    EXPECT_TRUE(entry.no_files);
    EXPECT_FALSE(entry.requires_arg);
}

TEST(FishParsingTest, RequiresArgFlag) {
    FishCompletionEntry entry;
    bool ok = parse_fish_complete_line(
        "complete -c git -r -s m -d 'Message'", entry);
    ASSERT_TRUE(ok);
    EXPECT_TRUE(entry.requires_arg);
    EXPECT_FALSE(entry.no_files);
}

TEST(FishParsingTest, ExclusiveFlag) {
    FishCompletionEntry entry;
    bool ok = parse_fish_complete_line(
        "complete -c test -x -s t -d 'Type'", entry);
    ASSERT_TRUE(ok);
    EXPECT_TRUE(entry.no_files);
    EXPECT_TRUE(entry.requires_arg);
}

TEST(FishParsingTest, ConditionIgnored) {
    FishCompletionEntry entry;
    bool ok = parse_fish_complete_line(
        "complete -c git -n '__fish_git_needs_command' -a push "
        "-d 'Push'", entry);
    ASSERT_TRUE(ok);
    EXPECT_EQ(entry.command, "git");
    EXPECT_EQ(entry.arguments, "push");
}

TEST(FishParsingTest, EmptyLine) {
    FishCompletionEntry entry;
    EXPECT_FALSE(parse_fish_complete_line("", entry));
}

TEST(FishParsingTest, CommentLine) {
    FishCompletionEntry entry;
    EXPECT_FALSE(parse_fish_complete_line(
        "# This is a comment", entry));
}

TEST(FishParsingTest, NonCompleteLine) {
    FishCompletionEntry entry;
    EXPECT_FALSE(parse_fish_complete_line(
        "function __fish_git_needs_command", entry));
}

TEST(FishParsingTest, MalformedNoCommand) {
    FishCompletionEntry entry;
    EXPECT_FALSE(parse_fish_complete_line(
        "complete -s b -l branch", entry));
}

TEST(FishParsingTest, WhitespaceOnlyLine) {
    FishCompletionEntry entry;
    EXPECT_FALSE(parse_fish_complete_line("   \t  ", entry));
}

TEST(FishParsingTest, LeadingWhitespace) {
    FishCompletionEntry entry;
    bool ok = parse_fish_complete_line(
        "  complete -c ls -s a -d 'Show all'", entry);
    ASSERT_TRUE(ok);
    EXPECT_EQ(entry.command, "ls");
    EXPECT_EQ(entry.short_opt, "a");
}

TEST(FishParsingTest, LongFormFlags) {
    FishCompletionEntry entry;
    bool ok = parse_fish_complete_line(
        "complete --command git --short-option h --long-option help "
        "--description 'Show help'", entry);
    ASSERT_TRUE(ok);
    EXPECT_EQ(entry.command, "git");
    EXPECT_EQ(entry.short_opt, "h");
    EXPECT_EQ(entry.long_opt, "help");
    EXPECT_EQ(entry.description, "Show help");
}

// ══════════════════════════════════════════════════════════════
// Completion conversion tests
// ══════════════════════════════════════════════════════════════

TEST(FishConversionTest, ShortOptCompletion) {
    std::vector<FishCompletionEntry> entries;
    FishCompletionEntry e;
    e.command = "ls";
    e.short_opt = "a";
    e.description = "All files";
    entries.push_back(e);

    auto comps = fish_entries_to_completions(entries, "-");
    ASSERT_EQ(comps.size(), 1u);
    EXPECT_EQ(comps[0].text, "-a");
    EXPECT_EQ(comps[0].type, Completion::OPTION_SHORT);
}

TEST(FishConversionTest, LongOptCompletion) {
    std::vector<FishCompletionEntry> entries;
    FishCompletionEntry e;
    e.command = "ls";
    e.long_opt = "all";
    e.description = "All files";
    entries.push_back(e);

    auto comps = fish_entries_to_completions(entries, "--");
    ASSERT_EQ(comps.size(), 1u);
    EXPECT_EQ(comps[0].text, "--all");
    EXPECT_EQ(comps[0].type, Completion::OPTION_LONG);
}

TEST(FishConversionTest, PrefixFiltering) {
    std::vector<FishCompletionEntry> entries;
    FishCompletionEntry e1;
    e1.command = "git";
    e1.long_opt = "branch";
    entries.push_back(e1);

    FishCompletionEntry e2;
    e2.command = "git";
    e2.long_opt = "bare";
    entries.push_back(e2);

    FishCompletionEntry e3;
    e3.command = "git";
    e3.long_opt = "verbose";
    entries.push_back(e3);

    auto comps = fish_entries_to_completions(entries, "--b");
    ASSERT_EQ(comps.size(), 2u);
    EXPECT_EQ(comps[0].text, "--branch");
    EXPECT_EQ(comps[1].text, "--bare");
}

TEST(FishConversionTest, ArgumentAsSubcommand) {
    std::vector<FishCompletionEntry> entries;
    FishCompletionEntry e;
    e.command = "git";
    e.arguments = "clone";
    e.description = "Clone repo";
    entries.push_back(e);

    auto comps = fish_entries_to_completions(entries, "cl");
    ASSERT_EQ(comps.size(), 1u);
    EXPECT_EQ(comps[0].text, "clone");
    EXPECT_EQ(comps[0].type, Completion::SUBCOMMAND);
}

// ══════════════════════════════════════════════════════════════
// Provider lazy loading tests
// ══════════════════════════════════════════════════════════════

class FishProviderTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = make_temp_dir("provider");
        ASSERT_FALSE(test_dir_.empty());

        // Create a fake fish completion file
        write_file(test_dir_ + "/mycommand.fish",
            "complete -c mycommand -s v -l verbose -d 'Verbose output'\n"
            "complete -c mycommand -s h -l help -d 'Show help'\n"
            "complete -c mycommand -f -a 'start stop restart'\n"
            "# This is a comment\n"
            "\n"
            "complete -c mycommand -l config -r -d 'Config file'\n");
    }

    void TearDown() override {
        // Clean up temp files
        if (!test_dir_.empty()) {
            std::string cmd = "rm -rf " + test_dir_;
            if (system(cmd.c_str())) {}
        }
    }

    std::string test_dir_;
};

TEST_F(FishProviderTest, IndexDiscovery) {
    std::vector<std::string> dirs = {test_dir_ + "/"};
    FishCompletionProvider provider(dirs);

    EXPECT_EQ(provider.indexed_command_count(), 1u);
    EXPECT_TRUE(provider.can_complete("mycommand"));
    EXPECT_FALSE(provider.can_complete("nonexistent"));
}

TEST_F(FishProviderTest, LazyLoadFirstCall) {
    std::vector<std::string> dirs = {test_dir_ + "/"};
    FishCompletionProvider provider(dirs);

    // Not yet loaded
    EXPECT_FALSE(provider.is_command_loaded("mycommand"));

    // Complete triggers lazy load
    ShellState state;
    auto results = provider.complete("mycommand", "", {}, state);

    // Now loaded
    EXPECT_TRUE(provider.is_command_loaded("mycommand"));
    EXPECT_FALSE(results.empty());
}

TEST_F(FishProviderTest, CachedSecondCall) {
    std::vector<std::string> dirs = {test_dir_ + "/"};
    FishCompletionProvider provider(dirs);

    ShellState state;
    auto results1 = provider.complete("mycommand", "-", {}, state);
    EXPECT_TRUE(provider.is_command_loaded("mycommand"));

    // Second call uses cache
    auto results2 = provider.complete("mycommand", "-", {}, state);
    EXPECT_EQ(results1.size(), results2.size());
}

TEST_F(FishProviderTest, SkipsMissingDirs) {
    std::vector<std::string> dirs = {
        "/nonexistent/path/",
        test_dir_ + "/"
    };
    FishCompletionProvider provider(dirs);

    EXPECT_EQ(provider.indexed_command_count(), 1u);
    EXPECT_TRUE(provider.can_complete("mycommand"));
}

TEST_F(FishProviderTest, ProviderMetadata) {
    std::vector<std::string> dirs = {test_dir_ + "/"};
    FishCompletionProvider provider(dirs);

    EXPECT_EQ(provider.name(), "fish");
    EXPECT_EQ(provider.priority(), 10);
}

TEST_F(FishProviderTest, CompleteWithPrefix) {
    std::vector<std::string> dirs = {test_dir_ + "/"};
    FishCompletionProvider provider(dirs);

    ShellState state;
    auto results = provider.complete("mycommand", "--v", {}, state);

    // Should only match --verbose
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].text, "--verbose");
}

TEST_F(FishProviderTest, CompleteSubcommands) {
    std::vector<std::string> dirs = {test_dir_ + "/"};
    FishCompletionProvider provider(dirs);

    ShellState state;
    auto results = provider.complete("mycommand", "st", {}, state);

    // Should match "start" and "stop"
    ASSERT_EQ(results.size(), 2u);
}
