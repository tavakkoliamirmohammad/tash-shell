#include "tash/plugins/starship_prompt_provider.h"
#include "tash/util/config_resolver.h"
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

// ── Static members ───────────────────────────────────────────

bool StarshipPromptProvider::cached_ = false;
bool StarshipPromptProvider::available_ = false;

// ── Helper: run a command and capture stdout ─────────────────

std::string popen_read(const std::string &command) {
    FILE *pipe = popen(command.c_str(), "r");
    if (!pipe) return "";
    char buffer[256];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result += buffer;
    }
    pclose(pipe);
    return result;
}

// ── Build the starship command string from shell state ────────

std::string build_starship_command(const ShellState &state) {
    int exit_code = state.last_exit_status;
    long duration_ms = static_cast<long>(state.last_cmd_duration * 1000);
    if (duration_ms < 0) duration_ms = 0;
    int jobs = static_cast<int>(state.background_processes.size());

    int cols = 80;
    struct winsize ws;
    if (isatty(STDOUT_FILENO) && ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        cols = ws.ws_col;
    }

    std::ostringstream cmd;
    cmd << "starship prompt"
        << " --status=" << exit_code
        << " --cmd-duration=" << duration_ms
        << " --jobs=" << jobs
        << " --terminal-width=" << cols;
    return cmd.str();
}

// ── render() ─────────────────────────────────────────────────

std::string StarshipPromptProvider::render(const ShellState &state) {
    // Fall through to the builtin prompt when starship isn't installed /
    // configured; empty return signals "no override".
    if (!is_available()) return "";
    setenv("STARSHIP_SHELL", "bash", 1);
    std::string cmd = build_starship_command(state) + " 2>/dev/null";
    return popen_read(cmd);
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
    std::string which_out = popen_read("which starship 2>/dev/null");
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
