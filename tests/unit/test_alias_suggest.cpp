#include <gtest/gtest.h>
#include "tash/plugins/alias_suggest_provider.h"
#include "tash/shell.h"

// ── find_matching_alias tests ────────────────────────────────

TEST(AliasSuggest, FindMatchExact) {
    std::unordered_map<std::string, std::string> aliases;
    aliases["gco"] = "git checkout";

    EXPECT_EQ(find_matching_alias("git checkout", aliases), "gco");
}

TEST(AliasSuggest, FindMatchWithArgs) {
    std::unordered_map<std::string, std::string> aliases;
    aliases["gco"] = "git checkout";

    EXPECT_EQ(find_matching_alias("git checkout main", aliases), "gco");
}

TEST(AliasSuggest, NoMatchDifferentCommand) {
    std::unordered_map<std::string, std::string> aliases;
    aliases["gco"] = "git checkout";

    EXPECT_EQ(find_matching_alias("git push", aliases), "");
}

TEST(AliasSuggest, NoMatchPartialOverlap) {
    std::unordered_map<std::string, std::string> aliases;
    aliases["gco"] = "git checkout";

    // "git check" is NOT "git checkout" -- must not match
    EXPECT_EQ(find_matching_alias("git check", aliases), "");
}

TEST(AliasSuggest, PreferLongestMatch) {
    std::unordered_map<std::string, std::string> aliases;
    aliases["g"] = "git";
    aliases["gco"] = "git checkout";

    // "git checkout -b" should match "git checkout" (longer) over "git"
    EXPECT_EQ(find_matching_alias("git checkout -b", aliases), "gco");
}

TEST(AliasSuggest, EmptyAliases) {
    std::unordered_map<std::string, std::string> aliases;

    EXPECT_EQ(find_matching_alias("git checkout", aliases), "");
}

TEST(AliasSuggest, EmptyCommand) {
    std::unordered_map<std::string, std::string> aliases;
    aliases["gco"] = "git checkout";

    EXPECT_EQ(find_matching_alias("", aliases), "");
}

// ── get_remaining_args tests ─────────────────────────────────

TEST(AliasSuggest, GetRemainingArgs) {
    EXPECT_EQ(get_remaining_args("git checkout main", "git checkout"), " main");
}

TEST(AliasSuggest, GetRemainingArgsExact) {
    EXPECT_EQ(get_remaining_args("git checkout", "git checkout"), "");
}

// ── Reminder-once-per-session test ───────────────────────────

TEST(AliasSuggest, ReminderOnlyOnce) {
    AliasSuggestProvider provider;
    ShellState state;
    state.core.aliases["gco"] = "git checkout";

    // First call -- should add "gco" to the reminded set.
    provider.on_before_command("git checkout main", state);
    EXPECT_EQ(provider.reminded_aliases().count("gco"), 1u);

    // Second call -- should NOT add again (set size stays 1).
    // We verify by checking the set size hasn't grown and that
    // the provider still has exactly one entry.
    provider.on_before_command("git checkout develop", state);
    EXPECT_EQ(provider.reminded_aliases().size(), 1u);
}
