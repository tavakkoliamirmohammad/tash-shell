// Async-signal-safe crash-dump handler. See include/tash/util/crash_dump.h
// for the rationale. Deep-review finding O7.4.
//
// Everything inside crash_handler() runs in signal context. That means:
//   * no malloc / free (rules out std::string, std::cerr, printf family,
//     tash::io::error — they all allocate or lock);
//   * no mutexes / condvars;
//   * only calls from the POSIX async-signal-safe list.
// We restrict ourselves to write(2), getcwd(3), strlen(3), raise(3),
// sigaction(2), and backtrace()/backtrace_symbols_fd() on platforms
// that provide <execinfo.h>. The int → decimal conversion is done by
// hand into a stack buffer so we don't need snprintf.
//
// The handler re-raises the signal after dumping so the OS still
// produces a core dump / normal termination. SA_RESETHAND restores the
// default disposition before the handler returns, so raise() hits
// SIG_DFL on the second delivery — no infinite loop even if the
// backtrace path itself faults.

#include "tash/util/crash_dump.h"

#include <csignal>
#include <cstring>
#include <unistd.h>

// execinfo.h is glibc on Linux (Alpine / musl does NOT ship it), and
// the BSD libc on macOS. Probe with __has_include so we don't have to
// keep a libc allowlist here.
#if defined(__has_include)
#  if __has_include(<execinfo.h>)
#    include <execinfo.h>
#    define TASH_HAVE_EXECINFO 1
#  endif
#endif

namespace tash::util {

// Single process-wide pointer, set once by install_crash_handler().
// Reading a const ShellState* is async-signal-safe; reading a const
// std::string& and calling .c_str() is safe as long as the string is
// not being concurrently mutated — ShellState lives on the main
// thread's stack and tash is single-threaded for command execution,
// so by the time we SEGV we either have a stable string or garbage we
// can't print safely anyway. In the latter case we'd SEGV inside the
// handler, SA_RESETHAND kicks us into SIG_DFL, and a core dump is
// still produced.
static const ShellState *g_crash_state = nullptr;

// ── Async-signal-safe helpers ─────────────────────────────────

// write(2) a NUL-terminated C string. Return value ignored — we're
// already crashing, nothing sensible to do if stderr is closed.
static void write_cstr(const char *s) {
    if (!s) return;
    size_t n = std::strlen(s);
    if (n == 0) return;
    // Loop in case of partial writes (unlikely on stderr but cheap).
    while (n > 0) {
        ssize_t w = ::write(STDERR_FILENO, s, n);
        if (w <= 0) return;
        s += w;
        n -= static_cast<size_t>(w);
    }
}

// Format a non-negative integer into the given buffer as decimal,
// returning the number of bytes written. Handles negatives with a
// leading '-'. Buffer must be at least 24 bytes (enough for int64_t).
// Does not NUL-terminate — caller writes the exact slice.
static size_t int_to_dec(long long v, char *buf) {
    char tmp[24];
    size_t n = 0;
    bool negative = v < 0;
    // Use unsigned math so INT64_MIN negates cleanly.
    unsigned long long u = negative ? static_cast<unsigned long long>(-(v + 1)) + 1
                                    : static_cast<unsigned long long>(v);
    if (u == 0) {
        tmp[n++] = '0';
    } else {
        while (u > 0) {
            tmp[n++] = static_cast<char>('0' + (u % 10));
            u /= 10;
        }
    }
    size_t out = 0;
    if (negative) buf[out++] = '-';
    // tmp was built low-digit-first; reverse into buf.
    for (size_t i = 0; i < n; ++i) {
        buf[out++] = tmp[n - 1 - i];
    }
    return out;
}

static void write_int(long long v) {
    char buf[24];
    size_t n = int_to_dec(v, buf);
    if (n) (void)::write(STDERR_FILENO, buf, n);
}

// ── Handler ────────────────────────────────────────────────────

static void crash_handler(int sig) {
    const char *signame = "?";
    switch (sig) {
        case SIGSEGV: signame = "SIGSEGV"; break;
        case SIGABRT: signame = "SIGABRT"; break;
        case SIGBUS:  signame = "SIGBUS";  break;
        default:      signame = "?";       break;
    }

    write_cstr("\n=== tash crash (signal ");
    write_int(sig);
    write_cstr(" ");
    write_cstr(signame);
    write_cstr(") ===\n");

    if (g_crash_state) {
        // Direct field on ShellState (verified against include/tash/shell.h).
        // Copying the pointer is cheap and avoids dereferencing twice.
        const char *cmd = g_crash_state->last_executed_cmd.c_str();
        write_cstr("last command: ");
        write_cstr((cmd && *cmd) ? cmd : "(none)");
        write_cstr("\n");

        write_cstr("last exit status: ");
        write_int(g_crash_state->last_exit_status);
        write_cstr("\n");
    } else {
        write_cstr("last command: (state unavailable)\n");
    }

    // getcwd into a stack buffer — POSIX lists getcwd as async-signal-safe.
    char cwd_buf[4096];
    if (::getcwd(cwd_buf, sizeof(cwd_buf)) != nullptr) {
        write_cstr("cwd: ");
        write_cstr(cwd_buf);
        write_cstr("\n");
    } else {
        write_cstr("cwd: (unavailable)\n");
    }

#ifdef TASH_HAVE_EXECINFO
    write_cstr("backtrace:\n");
    void *frames[64];
    int n = ::backtrace(frames, 64);
    if (n > 0) {
        // backtrace_symbols_fd writes straight to the fd and does not
        // malloc — the key reason we use it over backtrace_symbols.
        ::backtrace_symbols_fd(frames, n, STDERR_FILENO);
    } else {
        write_cstr("(empty)\n");
    }
#else
    write_cstr("backtrace: (not available on this platform)\n");
#endif

    write_cstr("=== run 'tash --debug' for more info, "
               "or report at https://github.com/tavakkoliamirmohammad/"
               "tash-shell/issues ===\n");

    // SA_RESETHAND restored SIG_DFL before entry; raise() now terminates
    // the process with the default disposition (core dump / signal exit).
    ::raise(sig);
}

// ── Install ────────────────────────────────────────────────────

static void install_one(int sig) {
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &crash_handler;
    sigemptyset(&sa.sa_mask);
    // SA_RESETHAND: after the first delivery, reset to SIG_DFL so a
    //   recursive fault (including from our own raise()) terminates
    //   normally and produces a core dump.
    // SA_NODEFER: don't block the same signal while we're handling it;
    //   combined with SA_RESETHAND, a recursive fault delivers via
    //   SIG_DFL immediately instead of being queued.
    sa.sa_flags = SA_RESETHAND | SA_NODEFER;
    // Failure is non-fatal: we'd rather have a shell that runs than one
    // that refuses to start because sigaction rejected our request.
    (void)::sigaction(sig, &sa, nullptr);
}

void install_crash_handler(const ShellState &state) {
    g_crash_state = &state;
    install_one(SIGSEGV);
    install_one(SIGABRT);
    install_one(SIGBUS);
    // SIGINT / SIGCHLD are intentionally NOT touched — they're owned
    // by install_signal_handlers() in src/core/signals.cpp.
}

} // namespace tash::util
