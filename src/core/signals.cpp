// Signal-handler setup and the globals they toggle. Extracted from
// main.cpp so the REPL doesn't have to know how signals are wired, only
// that `install_signal_handlers()` makes the shell Ctrl-C friendly and
// SIGCHLD-aware.

#include "tash/core.h"

#include <csignal>

// Globals the handlers touch. Defined here (not main.cpp) so anything
// linking the shell library sees them.
volatile sig_atomic_t sigchld_received = 0;
volatile sig_atomic_t fg_child_pid = 0;

// ── Handlers ──────────────────────────────────────────────────

static void sigint_handler(int) {
    if (fg_child_pid > 0) {
        kill(fg_child_pid, SIGINT);
    } else {
        if (write(STDOUT_FILENO, "\n", 1)) {}
    }
}

static void sigchld_handler(int) {
    sigchld_received = 1;
}

// ── Install ───────────────────────────────────────────────────

void install_signal_handlers() {
    struct sigaction sa_int;
    sa_int.sa_handler = sigint_handler;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;
    sigaction(SIGINT, &sa_int, nullptr);

    struct sigaction sa_chld;
    sa_chld.sa_handler = sigchld_handler;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa_chld, nullptr);
}
