// Signal-handler setup and the globals they toggle. Extracted from
// main.cpp so the REPL doesn't have to know how signals are wired, only
// that `install_signal_handlers()` makes the shell Ctrl-C friendly and
// SIGCHLD-aware.

#include "tash/core/executor.h"
#include "tash/core/parser.h"
#include "tash/core/signals.h"
#include "tash/util/io.h"
#include <atomic>
#include <csignal>
#include <string>
#include <sys/types.h>
#include <unistd.h>

// Globals the handlers touch. Defined here (not main.cpp) so anything
// linking the shell library sees them. The lock-free static_asserts for
// std::atomic<pid_t> live in include/tash/shell.h so every TU that reads
// this atomic from a signal-adjacent path inherits the check.
volatile sig_atomic_t sigchld_received = 0;
std::atomic<pid_t> fg_child_pid{0};

// Pending-trap flags: one slot per signum up to TASH_MAX_SIGNAL. The
// signal handler writes 1 here (async-signal-safe); the main loop drains
// it via check_and_fire_traps() so the actual trap command runs in a
// normal execution context.
#ifndef TASH_MAX_SIGNAL
#define TASH_MAX_SIGNAL 64
#endif
volatile sig_atomic_t pending_traps[TASH_MAX_SIGNAL] = {0};

// ── Handlers ──────────────────────────────────────────────────

static void sigint_handler(int) {
    // Snapshot to a local so the guard-and-kill pair is safe against the
    // parent clearing fg_child_pid (post-waitpid) between our check and
    // the kill() call — otherwise we could signal a recycled pid.
    pid_t pid = fg_child_pid.load(std::memory_order_acquire);
    if (pid > 0) {
        kill(pid, SIGINT);
    } else {
        if (write(STDOUT_FILENO, "\n", 1)) {}
    }
    // Queue the trap; the main loop fires it between commands.
    if (SIGINT < TASH_MAX_SIGNAL) pending_traps[SIGINT] = 1;
}

static void sigchld_handler(int) {
    sigchld_received = 1;
}

// Generic handler for signals the user has registered a trap for but
// which the shell doesn't otherwise care about (e.g. SIGTERM, SIGUSR1).
// Just records the signal; the main loop runs the trap command.
static void trap_only_handler(int signum) {
    if (signum >= 0 && signum < TASH_MAX_SIGNAL) pending_traps[signum] = 1;
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

// ── trap plumbing ─────────────────────────────────────────────

void install_trap_handler(int signum) {
    if (signum <= 0 || signum >= TASH_MAX_SIGNAL) return;
    // SIGINT already has a richer handler; it sets pending_traps itself.
    if (signum == SIGINT) return;
    struct sigaction sa;
    sa.sa_handler = trap_only_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(signum, &sa, nullptr);
}

void uninstall_trap_handler(int signum) {
    if (signum <= 0 || signum >= TASH_MAX_SIGNAL) return;
    if (signum == SIGINT) return;      // keep the shell's own handler
    struct sigaction sa;
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(signum, &sa, nullptr);
}

void ignore_signal(int signum) {
    if (signum <= 0 || signum >= TASH_MAX_SIGNAL) return;
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(signum, &sa, nullptr);
}

// Drain pending_traps[] and run the matching trap commands. Called
// from the executor between commands, so trap commands run in normal
// shell context (not async-signal-safe context).
void check_and_fire_traps(ShellState &state) {
    // SIGINT delivery is logged from the safe main-loop side (the handler
    // itself only sets the pending flag — debug() is not async-signal-safe).
    if (pending_traps[SIGINT]) {
        pid_t fg = fg_child_pid.load(std::memory_order_acquire);
        tash::io::debug("SIGINT received (pid=" + std::to_string(::getpid()) +
                        ", fg_child=" + std::to_string(fg) + ")");
    }
    for (int signum = 1; signum < TASH_MAX_SIGNAL; ++signum) {
        if (!pending_traps[signum]) continue;
        pending_traps[signum] = 0;
        auto it = state.exec.traps.find(signum);
        if (it == state.exec.traps.end()) continue;
        const std::string &cmd = it->second;
        if (cmd.empty()) continue;     // "ignore" form
        tash::io::debug("firing trap for signal " + std::to_string(signum));
        std::vector<CommandSegment> segs = parse_command_line(cmd);
        execute_command_line(segs, state);
    }
}

// Run the EXIT pseudo-trap (signum 0). Called from builtin_exit before
// the shell terminates.
void fire_exit_trap(ShellState &state) {
    auto it = state.exec.traps.find(0);
    if (it == state.exec.traps.end() || it->second.empty()) return;
    std::vector<CommandSegment> segs = parse_command_line(it->second);
    execute_command_line(segs, state);
}
