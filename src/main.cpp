// Slim entry point: parse argv flags, dispatch to the right mode.
//
// Everything else was extracted as part of the main-split refactor:
//   src/core/executor.cpp  — command execution pipeline
//   src/core/signals.cpp   — SIGINT/SIGCHLD handlers + install helper
//   src/startup.cpp        — plugin registration, tashrc load, --benchmark
//   src/repl.cpp           — interactive loop, banner, replxx setup
//
// main.cpp is now ~60 LOC, which is the point.

#include "tash/ai/bootstrap.h"
#include "tash/core.h"
#include "tash/history.h"
#include "tash/repl.h"
#include "tash/startup.h"
#include "tash/ui.h"
#include "theme.h"

#include <string>
#include <unistd.h>

using std::string;

// I/O helpers are inline in tash/core.h so standalone plugin tests
// (TEST_STANDALONE targets not linked to shell_lib) can resolve them.

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
    tash::ai::build_history_context();
    tash::load_tashrc(state);
    return tash::run_interactive(state);
}
#endif // TESTING_BUILD
