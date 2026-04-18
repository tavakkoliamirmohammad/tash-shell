#include <gtest/gtest.h>
#include "tash/plugins/starship_prompt_provider.h"
#include "tash/shell.h"
#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

// ── Test fixture ─────────────────────────────────────────────

class StarshipTest : public ::testing::Test {
protected:
    ShellState state;
};

// ── argv helpers ─────────────────────────────────────────────

static bool argv_has_prefix(const std::vector<std::string> &argv,
                            const std::string &prefix) {
    return std::any_of(argv.begin(), argv.end(),
        [&](const std::string &a) { return a.rfind(prefix, 0) == 0; });
}

static bool argv_contains(const std::vector<std::string> &argv,
                          const std::string &value) {
    return std::find(argv.begin(), argv.end(), value) != argv.end();
}

// ── build_starship_argv tests ────────────────────────────────

TEST_F(StarshipTest, RendersWithCorrectArgs) {
    state.core.last_exit_status = 0;
    state.core.last_cmd_duration = 1.5;

    std::vector<std::string> argv = build_starship_argv(state);
    EXPECT_TRUE(argv_has_prefix(argv, "--status="));
    EXPECT_TRUE(argv_has_prefix(argv, "--cmd-duration="));
    EXPECT_TRUE(argv_has_prefix(argv, "--jobs="));
    EXPECT_TRUE(argv_has_prefix(argv, "--terminal-width="));
    EXPECT_TRUE(argv_contains(argv, "starship"));
    EXPECT_TRUE(argv_contains(argv, "prompt"));
}

TEST_F(StarshipTest, PassesExitStatus) {
    state.core.last_exit_status = 127;
    state.core.last_cmd_duration = 0;

    std::vector<std::string> argv = build_starship_argv(state);
    EXPECT_TRUE(argv_contains(argv, "--status=127"));
}

TEST_F(StarshipTest, PassesDuration) {
    state.core.last_exit_status = 0;
    state.core.last_cmd_duration = 2.5; // 2500 ms

    std::vector<std::string> argv = build_starship_argv(state);
    EXPECT_TRUE(argv_contains(argv, "--cmd-duration=2500"));
}

TEST_F(StarshipTest, PassesNegativeDurationAsZero) {
    state.core.last_exit_status = 0;
    state.core.last_cmd_duration = -1; // default / not measured

    std::vector<std::string> argv = build_starship_argv(state);
    EXPECT_TRUE(argv_contains(argv, "--cmd-duration=0"));
}

TEST_F(StarshipTest, PassesJobCount) {
    state.core.last_exit_status = 0;
    state.core.last_cmd_duration = 0;
    state.core.background_processes[100] = "sleep";
    state.core.background_processes[200] = "make";
    state.core.background_processes[300] = "cargo build";

    std::vector<std::string> argv = build_starship_argv(state);
    EXPECT_TRUE(argv_contains(argv, "--jobs=3"));
}

TEST_F(StarshipTest, RenderIsEmptyWhenUnavailable) {
    // render() now short-circuits when starship isn't installed /
    // configured so the builtin prompt stays clean (no leaked
    // "starship: command not found" on stderr). Empty return means
    // "fall through to the builtin prompt".
    StarshipPromptProvider provider;
    // In CI the starship binary is absent, so render should return "".
    EXPECT_EQ(provider.render(state), "");
}

TEST_F(StarshipTest, PriorityIs20) {
    StarshipPromptProvider provider;
    EXPECT_EQ(provider.priority(), 20);
    EXPECT_EQ(provider.name(), "starship");
}
