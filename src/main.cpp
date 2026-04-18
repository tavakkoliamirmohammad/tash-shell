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
#include "tash/plugin.h"
#include "tash/repl.h"
#include "tash/startup.h"
#include "tash/ui.h"
#include "theme.h"

#include <cstdio>
#include <string>
#include <unistd.h>

using std::string;

// I/O helpers are inline in tash/core.h so standalone plugin tests
// (TEST_STANDALONE targets not linked to shell_lib) can resolve them.

// ── --version / --features ─────────────────────────────────────
//
// Prints the build's version and which optional features were
// compiled in. `install.sh` invokes this after install so users see
// exactly what they got — especially useful because AI and SQLite
// history compile out silently when their headers are missing.
// CI lanes assert on the output to catch regressions.
//
// Output format is stable (space-separated `+feat` / `-feat` tokens)
// so scripts can grep it.
static int print_version_and_features() {
#ifndef TASH_VERSION_STRING
#define TASH_VERSION_STRING "unknown"
#endif
    std::printf("tash %s\n", TASH_VERSION_STRING);
    std::printf("features:");
#ifdef TASH_AI_ENABLED
    std::printf(" +ai");
#else
    std::printf(" -ai");
#endif
#ifdef TASH_SQLITE_ENABLED
    std::printf(" +sqlite-history");
#else
    std::printf(" -sqlite-history");
#endif
    // Always-on features worth surfacing so users can sanity-check a
    // build (and so scripts can assert they're present).
    std::printf(" +fish-completion +fig-completion +manpage-completion");
    std::printf(" +clipboard +themes +trap +heredocs +subshells");
    std::printf("\n");
    return 0;
}

// ── main ───────────────────────────────────────────────────────

#ifndef TESTING_BUILD
int main(int argc, char *argv[]) {
    // `tash --version` / `tash --features` — report build info and exit.
    if (argc == 2 &&
        (string(argv[1]) == "--version" ||
         string(argv[1]) == "-V" ||
         string(argv[1]) == "--features")) {
        return print_version_and_features();
    }

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

    // Fire lifecycle on_startup hooks now that state + tashrc are
    // both ready. Plugins can observe the initialized state (aliases,
    // env, dir stack) before the first REPL iteration.
    global_plugin_registry().fire_startup(state);

    return tash::run_interactive(state);
}
#endif // TESTING_BUILD
