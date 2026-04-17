// Slim entry point: parse argv flags, dispatch to the right mode.
//
// Everything else was extracted as part of the main-split refactor:
//   src/core/executor.cpp  — command execution pipeline
//   src/core/signals.cpp   — SIGINT/SIGCHLD handlers + install helper
//   src/startup.cpp        — plugin registration, tashrc load, --benchmark
//   src/repl.cpp           — interactive loop, banner, replxx setup
//
// main.cpp is now ~60 LOC, which is the point.

#include "tash/core.h"
#include "tash/history.h"
#include "tash/repl.h"
#include "tash/startup.h"
#include "tash/ui.h"
#include "theme.h"

#include <string>
#include <unistd.h>

#ifdef TASH_AI_ENABLED
#include "tash/ai.h"
#endif

using std::string;

// ── I/O helpers used across the whole shell ───────────────────
//
// Defined here (not in an io.cpp module) because literally every other
// translation unit calls them and we want exactly one definition in the
// final binary.

void exit_with_message(const string &message, int exit_status) {
    if (write(STDERR_FILENO, message.c_str(), message.length())) {}
    exit(exit_status);
}

void write_stderr(const string &message) {
    if (write(STDERR_FILENO, message.c_str(), message.length())) {}
}

void write_stdout(const string &message) {
    if (write(STDOUT_FILENO, message.c_str(), message.length())) {}
}

// ── main ───────────────────────────────────────────────────────

#ifndef TESTING_BUILD
int main(int argc, char *argv[]) {
    // `tash --benchmark` — print a startup-stage breakdown and exit.
    if (argc == 2 && string(argv[1]) == "--benchmark") {
        return tash::run_benchmark_mode();
    }

    if (argc > 2) {
        exit_with_message("An error has occurred\n", 1);
    }

    // Interactive or script mode — both share one-time startup.
    load_user_theme();
    tash::register_default_plugins();

    ShellState state;
    install_signal_handlers();

    // `tash <script.tash>` — run the file and exit.
    if (argc == 2) {
        return execute_script_file(argv[1], state);
    }

    build_command_cache();

#ifdef TASH_AI_ENABLED
    // Build context-aware suggestion map from history.
    {
        string hist = history_file_path();
        if (!hist.empty()) {
            build_transition_map(hist, get_transition_map());
        }
    }
#endif

    tash::load_tashrc(state);
    return tash::run_interactive(state);
}
#endif // TESTING_BUILD
