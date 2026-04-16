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

TEST_F(StarshipTest, SetsShellEnvVar) {
    // render() sets STARSHIP_SHELL; we can verify it stays set after a
    // call to render (which will fail to exec starship, but the env var
    // is set before the exec).
    StarshipPromptProvider provider;
    // Even if starship is not installed, the env var should be set
    provider.render(state);

    const char *val = getenv("STARSHIP_SHELL");
    ASSERT_NE(val, nullptr);
    EXPECT_STREQ(val, "bash");
}

TEST_F(StarshipTest, PriorityIs20) {
    StarshipPromptProvider provider;
    EXPECT_EQ(provider.priority(), 20);
    EXPECT_EQ(provider.name(), "starship");
}
