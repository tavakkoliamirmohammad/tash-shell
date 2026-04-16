#include <gtest/gtest.h>

#ifdef TASH_AI_ENABLED

#include "tash/plugins/fig_completion_provider.h"
#include "tash/shell.h"

#include <fstream>
#include <cstdlib>
#include <sys/stat.h>

// ── Helper: create a temporary directory ─────────────────────

static std::string make_temp_dir(const std::string &suffix) {
    std::string base = "/tmp/tash_test_fig_" + suffix + "_XXXXXX";
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

// ── Test fixture ─────────────────────────────────────────────

class FigProviderTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = make_temp_dir("provider");
        ASSERT_FALSE(test_dir_.empty());

        // Create a git.json spec file
        write_file(test_dir_ + "/git.json", R"({
            "name": "git",
            "description": "Version control",
            "subcommands": [
                {
                    "name": ["checkout", "co"],
                    "description": "Switch branches",
                    "options": [
                        {
                            "name": ["-b", "--branch"],
                            "description": "Create and checkout branch"
                        },
                        {
                            "name": ["-f", "--force"],
                            "description": "Force checkout"
                        }
                    ]
                },
                {
                    "name": "commit",
                    "description": "Record changes",
                    "options": [
                        {
                            "name": ["-m", "--message"],
                            "description": "Commit message"
                        },
                        {
                            "name": "--amend",
                            "description": "Amend previous commit"
                        }
                    ]
                },
                {
                    "name": "remote",
                    "description": "Manage remotes",
                    "subcommands": [
                        {
                            "name": "add",
                            "description": "Add a remote"
                        },
                        {
                            "name": "remove",
                            "description": "Remove a remote"
                        }
                    ]
                }
            ],
            "options": [
                {
                    "name": "--version",
                    "description": "Show version"
                },
                {
                    "name": ["-C", "--git-dir"],
                    "description": "Set git directory"
                }
            ]
        })");

        // Create a spec with args/suggestions
        write_file(test_dir_ + "/npm.json", R"({
            "name": "npm",
            "subcommands": [
                {
                    "name": "run",
                    "description": "Run a script",
                    "args": {
                        "name": "script",
                        "suggestions": ["build", "test", "lint", "start"]
                    }
                }
            ]
        })");
    }

    void TearDown() override {
        if (!test_dir_.empty()) {
            std::string cmd = "rm -rf " + test_dir_;
            system(cmd.c_str());
        }
    }

    std::string test_dir_;
};

// ══════════════════════════════════════════════════════════════
// JSON loading tests
// ══════════════════════════════════════════════════════════════

TEST_F(FigProviderTest, LoadSpecFromFile) {
    FigCompletionProvider provider(test_dir_);
    EXPECT_TRUE(provider.has_spec("git"));
    EXPECT_TRUE(provider.has_spec("npm"));
    EXPECT_FALSE(provider.has_spec("nonexistent"));
}

TEST_F(FigProviderTest, LoadSpecFromString) {
    FigCompletionProvider provider(test_dir_);
    bool ok = provider.load_spec_from_string("test",
        R"({"name":"test","subcommands":[{"name":"sub","description":"A sub"}]})");
    ASSERT_TRUE(ok);
    EXPECT_TRUE(provider.has_spec("test"));
}

TEST_F(FigProviderTest, InvalidJsonReturnsError) {
    FigCompletionProvider provider(test_dir_);
    bool ok = provider.load_spec_from_string("bad", "not json at all {{{");
    EXPECT_FALSE(ok);
}

// ══════════════════════════════════════════════════════════════
// Subcommand extraction tests
// ══════════════════════════════════════════════════════════════

TEST_F(FigProviderTest, TopLevelSubcommands) {
    FigCompletionProvider provider(test_dir_);
    ShellState state;
    auto results = provider.complete("git", "ch", {}, state);

    // Should find "checkout" and "co" matching "ch" -> only "checkout"
    bool found_checkout = false;
    for (const auto &c : results) {
        if (c.text == "checkout") found_checkout = true;
    }
    EXPECT_TRUE(found_checkout);
}

TEST_F(FigProviderTest, SubcommandAliases) {
    FigCompletionProvider provider(test_dir_);
    ShellState state;
    auto results = provider.complete("git", "co", {}, state);

    // Both "checkout" and "co" aliases, plus "commit" matches "co"
    bool found_co = false;
    bool found_commit = false;
    for (const auto &c : results) {
        if (c.text == "co") found_co = true;
        if (c.text == "commit") found_commit = true;
    }
    EXPECT_TRUE(found_co);
    EXPECT_TRUE(found_commit);
}

// ══════════════════════════════════════════════════════════════
// Option extraction tests
// ══════════════════════════════════════════════════════════════

TEST_F(FigProviderTest, SubcommandOptions) {
    FigCompletionProvider provider(test_dir_);
    ShellState state;

    // After "git checkout", complete options starting with "-"
    auto results = provider.complete("git", "-", {"checkout"}, state);

    bool found_b = false;
    bool found_branch = false;
    bool found_f = false;
    for (const auto &c : results) {
        if (c.text == "-b") found_b = true;
        if (c.text == "--branch") found_branch = true;
        if (c.text == "-f") found_f = true;
    }
    EXPECT_TRUE(found_b);
    EXPECT_TRUE(found_branch);
    EXPECT_TRUE(found_f);
}

TEST_F(FigProviderTest, OptionPrefixFilter) {
    FigCompletionProvider provider(test_dir_);
    ShellState state;

    auto results = provider.complete("git", "--b", {"checkout"}, state);

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].text, "--branch");
}

// ══════════════════════════════════════════════════════════════
// Tree traversal tests
// ══════════════════════════════════════════════════════════════

TEST_F(FigProviderTest, NestedSubcommandTraversal) {
    FigCompletionProvider provider(test_dir_);
    ShellState state;

    // "git remote <TAB>" should show "add" and "remove"
    auto results = provider.complete("git", "", {"remote"}, state);

    bool found_add = false;
    bool found_remove = false;
    for (const auto &c : results) {
        if (c.text == "add") found_add = true;
        if (c.text == "remove") found_remove = true;
    }
    EXPECT_TRUE(found_add);
    EXPECT_TRUE(found_remove);
}

TEST_F(FigProviderTest, ArgumentSuggestions) {
    FigCompletionProvider provider(test_dir_);
    ShellState state;

    // "npm run <TAB>" should suggest scripts
    auto results = provider.complete("npm", "", {"run"}, state);

    bool found_build = false;
    bool found_test = false;
    for (const auto &c : results) {
        if (c.text == "build") found_build = true;
        if (c.text == "test") found_test = true;
    }
    EXPECT_TRUE(found_build);
    EXPECT_TRUE(found_test);
}

// ══════════════════════════════════════════════════════════════
// Provider metadata tests
// ══════════════════════════════════════════════════════════════

TEST_F(FigProviderTest, ProviderMetadata) {
    FigCompletionProvider provider(test_dir_);
    EXPECT_EQ(provider.name(), "fig");
    EXPECT_EQ(provider.priority(), 20);
}

TEST_F(FigProviderTest, CanCompleteKnownCommand) {
    FigCompletionProvider provider(test_dir_);
    EXPECT_TRUE(provider.can_complete("git"));
    EXPECT_TRUE(provider.can_complete("npm"));
    EXPECT_FALSE(provider.can_complete("unknown"));
}

// ══════════════════════════════════════════════════════════════
// Edge case tests
// ══════════════════════════════════════════════════════════════

TEST_F(FigProviderTest, MissingFieldsGraceful) {
    FigCompletionProvider provider(test_dir_);
    bool ok = provider.load_spec_from_string("minimal",
        R"({"name":"minimal"})");
    ASSERT_TRUE(ok);

    ShellState state;
    auto results = provider.complete("minimal", "", {}, state);
    EXPECT_TRUE(results.empty());
}

TEST_F(FigProviderTest, EmptySpecDir) {
    std::string empty_dir = make_temp_dir("empty");
    FigCompletionProvider provider(empty_dir);
    EXPECT_EQ(provider.loaded_spec_count(), 0u);
    EXPECT_FALSE(provider.can_complete("anything"));

    // Clean up
    std::string cmd = "rm -rf " + empty_dir;
    system(cmd.c_str());
}

#else // !TASH_AI_ENABLED

// When AI is disabled, Fig tests are skipped but the binary still compiles
TEST(FigCompletionDisabled, SkippedWithoutAI) {
    GTEST_SKIP() << "Fig completion requires TASH_AI_ENABLED";
}

#endif // TASH_AI_ENABLED
