#ifndef TASH_CORE_SIGNALS_H
#define TASH_CORE_SIGNALS_H

// Signal-handler globals, trap plumbing, and the process-wide fatal
// exit helper. Split out of the old mega-header tash/core.h.
// `tash/core.h` still includes this so existing #include paths keep
// working.

#include "tash/shell.h"

#include <atomic>
#include <signal.h>
#include <string>
#include <sys/types.h>
#include <unistd.h>

// ── Signal-related globals (visible to signal handlers in every TU) ─
extern volatile sig_atomic_t sigchld_received;
extern std::atomic<pid_t>    fg_child_pid;

// Enforced in the header so every TU reading this atomic from a signal
// handler inherits the lock-free check.
static_assert(std::atomic<pid_t>::is_always_lock_free,
              "fg_child_pid must be lock-free for async-signal-safety");

// ── I/O primitives ─────────────────────────────────────────────
//
// Defined inline so plugin tests that don't link shell_lib (e.g.
// TEST_STANDALONE targets) still resolve them. Every core TU needs at
// least one of these, so they live next to the signal plumbing.

inline void write_stderr(const std::string &message) {
    if (write(STDERR_FILENO, message.c_str(), message.length())) {}
}

inline void write_stdout(const std::string &message) {
    if (write(STDOUT_FILENO, message.c_str(), message.length())) {}
}

inline void exit_with_message(const std::string &message, int exit_status) {
    write_stderr(message);
    std::exit(exit_status);
}

// Install SIGINT + SIGCHLD handlers. Idempotent-safe to call once per
// process lifetime (main/startup does this).
void install_signal_handlers();

// ── trap plumbing (signals.cpp) ────────────────────────────────

void install_trap_handler(int signum);
void uninstall_trap_handler(int signum);
void ignore_signal(int signum);
void check_and_fire_traps(ShellState &state);
void fire_exit_trap(ShellState &state);

#endif // TASH_CORE_SIGNALS_H
