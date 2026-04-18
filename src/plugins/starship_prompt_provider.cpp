#include "tash/plugins/starship_prompt_provider.h"
#include "tash/util/config_resolver.h"
#include "tash/util/safe_exec.h"
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

// ── Static members ───────────────────────────────────────────

bool StarshipPromptProvider::cached_ = false;
bool StarshipPromptProvider::available_ = false;

// ── Helper: run a command and capture stdout ─────────────────
//
// Previously this shelled out via popen(command_str). The command was
// a static literal so no injection was possible, but using /bin/sh as
// a wrapper for something as simple as `starship prompt --status=0`
// was pure overhead. Route through safe_exec instead.

std::string argv_read(const std::vector<std::string> &argv) {
    auto r = tash::util::safe_exec(argv, 500);
    return r.stdout_text;
}

// ── Build the starship argv from shell state ──────────────────

std::vector<std::string> build_starship_argv(const ShellState &state) {
    int exit_code = state.core.last_exit_status;
    long duration_ms = static_cast<long>(state.core.last_cmd_duration * 1000);
    if (duration_ms < 0) duration_ms = 0;
    int jobs = static_cast<int>(state.core.background_processes.size());

    int cols = 80;
    struct winsize ws;
    if (isatty(STDOUT_FILENO) && ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        cols = ws.ws_col;
    }

    return {
        "starship", "prompt",
        "--status=" + std::to_string(exit_code),
        "--cmd-duration=" + std::to_string(duration_ms),
        "--jobs=" + std::to_string(jobs),
        "--terminal-width=" + std::to_string(cols),
    };
}

// ── render() ─────────────────────────────────────────────────

std::string StarshipPromptProvider::render(const ShellState &state) {
    // Fall through to the builtin prompt when starship isn't installed /
    // configured; empty return signals "no override".
    if (!is_available()) return "";
    setenv("STARSHIP_SHELL", "bash", 1);
    return argv_read(build_starship_argv(state));
}

// ── is_available() ───────────────────────────────────────────

static bool file_exists(const std::string &path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

bool StarshipPromptProvider::is_available() {
    if (cached_) return available_;
    cached_ = true;
    available_ = false;

    // Check if starship binary is in PATH
    std::string which_out = argv_read({"which", "starship"});
    if (which_out.empty()) return false;

    // Check for config: STARSHIP_CONFIG env or default config path
    const char *config_env = getenv("STARSHIP_CONFIG");
    if (config_env && config_env[0] != '\0') {
        available_ = true;
        return true;
    }

    if (file_exists(tash::config::get_starship_config_path())) {
        available_ = true;
        return true;
    }

    // Starship works without a config file (uses defaults), so binary
    // presence is sufficient
    available_ = true;
    return true;
}
