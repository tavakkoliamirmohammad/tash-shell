// Signal-handler setup and the globals they toggle. Extracted from
// main.cpp so the REPL doesn't have to know how signals are wired, only
// that `install_signal_handlers()` makes the shell Ctrl-C friendly and
// SIGCHLD-aware.

#include "tash/core.h"

#include <atomic>
#include <cassert>
#include <csignal>
#include <sys/types.h>

// Globals the handlers touch. Defined here (not main.cpp) so anything
// linking the shell library sees them.
volatile sig_atomic_t sigchld_received = 0;
std::atomic<pid_t> fg_child_pid{0};

// fg_child_pid is read from an async signal handler; that's only safe if
// the atomic never takes a lock. pid_t is int on every supported target,
// so std::atomic<pid_t> is always lock-free — enforce that at build time.
// We target C++14, where std::atomic<T>::is_always_lock_free (C++17) isn't
// available yet; use the C++11 ATOMIC_INT_LOCK_FREE macro instead (value
// 2 means "always lock-free").
static_assert(sizeof(pid_t) == sizeof(int),
              "fg_child_pid static_assert assumes pid_t is int");
static_assert(ATOMIC_INT_LOCK_FREE == 2,
              "fg_child_pid must be lock-free for async-signal-safety");

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
    assert(fg_child_pid.is_lock_free());
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
    for (int signum = 1; signum < TASH_MAX_SIGNAL; ++signum) {
        if (!pending_traps[signum]) continue;
        pending_traps[signum] = 0;
        auto it = state.traps.find(signum);
        if (it == state.traps.end()) continue;
        const std::string &cmd = it->second;
        if (cmd.empty()) continue;     // "ignore" form
        std::vector<CommandSegment> segs = parse_command_line(cmd);
        execute_command_line(segs, state);
    }
}

// Run the EXIT pseudo-trap (signum 0). Called from builtin_exit before
// the shell terminates.
void fire_exit_trap(ShellState &state) {
    auto it = state.traps.find(0);
    if (it == state.traps.end() || it->second.empty()) return;
    std::vector<CommandSegment> segs = parse_command_line(it->second);
    execute_command_line(segs, state);
}
