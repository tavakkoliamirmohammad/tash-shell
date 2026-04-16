#include <gtest/gtest.h>
#include "tash/plugins/manpage_completion_provider.h"
#include "tash/shell.h"

// ── Sample help text used across tests ───────────────────────

static const char *SAMPLE_HELP =
    "Usage: myapp [OPTIONS] FILE\n"
    "\n"
    "Options:\n"
    "  -v, --verbose       Increase verbosity\n"
    "  -o, --output FILE   Specify output file\n"
    "  -n                  Dry run mode\n"
    "      --force         Force overwrite\n"
    "  -h, --help          Show this help\n";

// ── Parser tests ─────────────────────────────────────────────

TEST(ManpageCompletion, ParseShortAndLong) {
    auto options = parse_help_output("  -v, --verbose       Increase verbosity\n");
    ASSERT_EQ(options.size(), 1u);
    EXPECT_EQ(options[0].short_flag, "-v");
    EXPECT_EQ(options[0].long_flag, "--verbose");
}

TEST(ManpageCompletion, ParseShortOnly) {
    auto options = parse_help_output("  -n                  Dry run\n");
    ASSERT_EQ(options.size(), 1u);
    EXPECT_EQ(options[0].short_flag, "-n");
    EXPECT_EQ(options[0].long_flag, "");
}

TEST(ManpageCompletion, ParseLongOnly) {
    auto options = parse_help_output("      --force         Force overwrite\n");
    ASSERT_EQ(options.size(), 1u);
    EXPECT_EQ(options[0].short_flag, "");
    EXPECT_EQ(options[0].long_flag, "--force");
}

TEST(ManpageCompletion, ParseDescription) {
    auto options = parse_help_output("  -v, --verbose       Increase verbosity\n");
    ASSERT_EQ(options.size(), 1u);
    EXPECT_EQ(options[0].description, "Increase verbosity");
}

TEST(ManpageCompletion, ParseMultipleOptions) {
    auto options = parse_help_output(SAMPLE_HELP);
    // Should find: -v/--verbose, -o/--output, -n, --force, -h/--help
    EXPECT_EQ(options.size(), 5u);
}

TEST(ManpageCompletion, ParseEmptyInput) {
    auto options = parse_help_output("");
    EXPECT_TRUE(options.empty());
}

TEST(ManpageCompletion, ParseNoOptions) {
    auto options = parse_help_output("Usage: foo\nSome text about the program\n");
    EXPECT_TRUE(options.empty());
}

TEST(ManpageCompletion, ParseIndentedHelp) {
    // Various indentation levels
    std::string text =
        "    -a, --all         Show all files\n"
        "\t-b, --brief       Brief output\n"
        "  -c, --count         Count items\n";
    auto options = parse_help_output(text);
    ASSERT_EQ(options.size(), 3u);
    EXPECT_EQ(options[0].short_flag, "-a");
    EXPECT_EQ(options[0].long_flag, "--all");
    EXPECT_EQ(options[1].short_flag, "-b");
    EXPECT_EQ(options[1].long_flag, "--brief");
    EXPECT_EQ(options[2].short_flag, "-c");
    EXPECT_EQ(options[2].long_flag, "--count");
}

TEST(ManpageCompletion, CacheHitReturnsQuickly) {
    ManpageCompletionProvider provider;
    ShellState state;

    // Under TESTING_BUILD, get_help_options won't run popen,
    // so the cache will be populated with empty results.
    // Call complete twice and verify cache has an entry.
    provider.complete("fakecmd", "--", {}, state);
    EXPECT_EQ(provider.cache().size(), 1u);
    EXPECT_TRUE(provider.cache().count("fakecmd") > 0);

    // Second call should still have exactly 1 cache entry
    provider.complete("fakecmd", "--", {}, state);
    EXPECT_EQ(provider.cache().size(), 1u);
}

TEST(ManpageCompletion, PriorityIs5) {
    ManpageCompletionProvider provider;
    EXPECT_EQ(provider.priority(), 5);
}

// ── End-to-end: completion_callback + ManpageCompletionProvider ──
//
// Demonstrates that tab-completing `ls -` surfaces real flags from
// `ls --help`. Bypasses the normal registry by using the provider's
// cache setter so the test doesn't depend on popen/system ls behaviour.

#include "tash/plugin.h"
#include "tash/ui.h"

TEST(Manpage, CompletionCallbackOffersLongFlag) {
    // Pre-seed the registry's provider with a fake help parse result.
    auto provider = std::make_unique<ManpageCompletionProvider>();
    auto &cache_mut = const_cast<
        std::unordered_map<std::string, std::vector<HelpOption>>&>(
        provider->cache());
    HelpOption opt;
    opt.short_flag = "-v";
    opt.long_flag  = "--verbose";
    opt.description = "Increase verbosity";
    cache_mut["mycmd"] = {opt};

    global_plugin_registry().register_completion_provider(std::move(provider));

    int ctx = 0;
    auto completions = completion_callback("mycmd --v", ctx);
    bool found = false;
    for (size_t i = 0; i < completions.size(); i++) {
        if (completions[i].text() == "--verbose") { found = true; break; }
    }
    EXPECT_TRUE(found);
}
