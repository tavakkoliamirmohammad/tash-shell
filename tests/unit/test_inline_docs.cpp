#include <gtest/gtest.h>
#include "tash/ui/inline_docs.h"

#include <algorithm>

// ── Helper: find flag in result vector ───────────────────────

static const FlagExplanation *find_flag(
    const std::vector<FlagExplanation> &v,
    const std::string &flag)
{
    for (const auto &fe : v) {
        if (fe.flag == flag) return &fe;
    }
    return nullptr;
}

// ── ExplainTarFlags ──────────────────────────────────────────

TEST(InlineDocs, ExplainTarFlags) {
    auto result = explain_command("tar", {"-x", "-z", "-f", "a.tar.gz"});
    // Should explain the three flags, skip the positional "a.tar.gz"
    ASSERT_EQ(result.size(), 3u);

    auto *x = find_flag(result, "-x");
    ASSERT_NE(x, nullptr);
    EXPECT_EQ(x->description, "Extract files from archive");

    auto *z = find_flag(result, "-z");
    ASSERT_NE(z, nullptr);
    EXPECT_EQ(z->description, "Filter through gzip");

    auto *f = find_flag(result, "-f");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->description, "Use archive file (next argument)");
}

// ── ExplainGrepFlags ─────────────────────────────────────────

TEST(InlineDocs, ExplainGrepFlags) {
    auto result = explain_command("grep", {"-i", "-r", "pattern"});
    // -i and -r explained; "pattern" is positional, skipped
    ASSERT_EQ(result.size(), 2u);

    auto *i = find_flag(result, "-i");
    ASSERT_NE(i, nullptr);
    EXPECT_EQ(i->description, "Ignore case distinctions");

    auto *r = find_flag(result, "-r");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->description, "Search recursively");
}

// ── ExplainUnknownFlag ───────────────────────────────────────

TEST(InlineDocs, ExplainUnknownFlag) {
    auto result = explain_command("tar", {"--unknown"});
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].flag, "--unknown");
    EXPECT_EQ(result[0].description, "");
}

// ── ExplainUnknownCommand ────────────────────────────────────

TEST(InlineDocs, ExplainUnknownCommand) {
    auto result = explain_command("myapp", {"-v"});
    EXPECT_TRUE(result.empty());
}

// ── HintKnownCommand ────────────────────────────────────────

TEST(InlineDocs, HintKnownCommand) {
    EXPECT_EQ(get_command_hint("tar"),
              "Create or extract compressed archives");
}

// ── HintUnknownCommand ──────────────────────────────────────

TEST(InlineDocs, HintUnknownCommand) {
    EXPECT_EQ(get_command_hint("myapp123"), "");
}

// ── HintGit ─────────────────────────────────────────────────

TEST(InlineDocs, HintGit) {
    EXPECT_EQ(get_command_hint("git"),
              "Distributed version control system");
}

// ── ExplainGitSubcommands ───────────────────────────────────

TEST(InlineDocs, ExplainGitSubcommands) {
    auto result = explain_command("git", {"push"});
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].flag, "push");
    EXPECT_EQ(result[0].description, "Upload local commits to remote");
}

// ── ExplainMixedFlags ───────────────────────────────────────

TEST(InlineDocs, ExplainMixedFlags) {
    // -la should be split into -l and -a for ls
    auto result = explain_command("ls", {"-la"});
    ASSERT_EQ(result.size(), 2u);

    auto *l = find_flag(result, "-l");
    ASSERT_NE(l, nullptr);
    EXPECT_EQ(l->description, "Long listing format");

    auto *a = find_flag(result, "-a");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->description, "Include hidden files");
}

// ── EmptyArgs ───────────────────────────────────────────────

TEST(InlineDocs, EmptyArgs) {
    auto result = explain_command("tar", {});
    EXPECT_TRUE(result.empty());
}

// ── Extra: database accessors are non-empty ─────────────────

TEST(InlineDocs, DatabaseNonEmpty) {
    EXPECT_FALSE(get_flag_db().empty());
    EXPECT_FALSE(get_cmd_hints().empty());
}

// ── Extra: long option with = ───────────────────────────────

TEST(InlineDocs, LongOptionWithEquals) {
    auto result = explain_command("grep", {"--color=auto"});
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].flag, "--color=auto");
    EXPECT_EQ(result[0].description, "Colorize matches");
}
