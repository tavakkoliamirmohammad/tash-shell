#include <gtest/gtest.h>
#include "tash/plugins/starship_prompt_provider.h"
#include "tash/shell.h"
#include <cstdlib>
#include <string>

// ── Test fixture ─────────────────────────────────────────────

class StarshipTest : public ::testing::Test {
protected:
    ShellState state;
};

// ── build_starship_command tests ─────────────────────────────

TEST_F(StarshipTest, RendersWithCorrectArgs) {
    state.last_exit_status = 0;
    state.last_cmd_duration = 1.5;

    std::string cmd = build_starship_command(state);
    EXPECT_NE(cmd.find("--status="), std::string::npos);
    EXPECT_NE(cmd.find("--cmd-duration="), std::string::npos);
    EXPECT_NE(cmd.find("--jobs="), std::string::npos);
    EXPECT_NE(cmd.find("--terminal-width="), std::string::npos);
    EXPECT_NE(cmd.find("starship prompt"), std::string::npos);
}

TEST_F(StarshipTest, PassesExitStatus) {
    state.last_exit_status = 127;
    state.last_cmd_duration = 0;

    std::string cmd = build_starship_command(state);
    EXPECT_NE(cmd.find("--status=127"), std::string::npos);
}

TEST_F(StarshipTest, PassesDuration) {
    state.last_exit_status = 0;
    state.last_cmd_duration = 2.5; // 2500 ms

    std::string cmd = build_starship_command(state);
    EXPECT_NE(cmd.find("--cmd-duration=2500"), std::string::npos);
}

TEST_F(StarshipTest, PassesNegativeDurationAsZero) {
    state.last_exit_status = 0;
    state.last_cmd_duration = -1; // default / not measured

    std::string cmd = build_starship_command(state);
    EXPECT_NE(cmd.find("--cmd-duration=0"), std::string::npos);
}

TEST_F(StarshipTest, PassesJobCount) {
    state.last_exit_status = 0;
    state.last_cmd_duration = 0;
    state.background_processes[100] = "sleep";
    state.background_processes[200] = "make";
    state.background_processes[300] = "cargo build";

    std::string cmd = build_starship_command(state);
    EXPECT_NE(cmd.find("--jobs=3"), std::string::npos);
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
