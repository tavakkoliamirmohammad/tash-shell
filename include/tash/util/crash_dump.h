#ifndef TASH_UTIL_CRASH_DUMP_H
#define TASH_UTIL_CRASH_DUMP_H

// Async-signal-safe crash-dump handler for tash.
//
// install_crash_handler() registers SIGSEGV/SIGABRT/SIGBUS handlers that,
// on fault, print a short diagnostic to stderr (signal name, last
// executed command, last exit status, cwd, and — where supported — a
// backtrace) before re-raising the signal so the OS still produces a
// core dump / normal termination. Handlers are registered with
// SA_RESETHAND, so a recursive fault hits the default disposition and
// we don't loop. Addresses deep-review finding O7.4.
//
// The handler body is strictly async-signal-safe: only write(2),
// getcwd(3), raise(3), sigaction(2), strlen(3) (pure, no locks), and
// backtrace()/backtrace_symbols_fd() from <execinfo.h>. No allocations,
// no std::cerr, no tash::io::error, no locks.

#include "tash/shell.h"

namespace tash::util {

// Register crash handlers for SIGSEGV, SIGABRT, SIGBUS. Call once at
// startup after `state` has been constructed. The reference is stashed
// in a process-wide pointer so the async-signal-safe handler can read
// `last_executed_cmd` / `last_exit_status` without locking — it only
// reads a const std::string* and calls .c_str(), both safe after the
// string's initial construction.
//
// SIGINT / SIGCHLD are intentionally left alone: those are already
// handled by install_signal_handlers(). This function never overrides
// them.
void install_crash_handler(const ShellState &state);

} // namespace tash::util

#endif // TASH_UTIL_CRASH_DUMP_H
