#ifndef TASH_UTIL_IO_H
#define TASH_UTIL_IO_H

// Unified diagnostic I/O for tash.
//
// Previously every TU called write_stderr() with an ad-hoc "tash: ..."
// prefix, so severity, coloring and gating were inconsistent. This header
// centralises:
//   * a 4-level severity (Debug/Info/Warning/Error);
//   * a single dynamic log level (set once at startup from the
//     TASH_LOG_LEVEL env var, silently drops anything below the threshold);
//   * TTY-aware coloring of the severity tag (red/yellow/dim) gated on
//     isatty(STDERR_FILENO) so piped/logged output stays plain.
//
// Non-diagnostic output (shell stdout, prompt rendering) continues to use
// write_stdout(). Signal handlers and post-fork children MUST NOT call
// into this namespace — those paths still use the async-signal-safe
// write_stderr() / raw write(2). Deep-review finding: C3.6.

#include <string>
#include <string_view>

namespace tash::io {

enum class Level { Debug = 0, Info = 1, Warning = 2, Error = 3 };

// Emit a diagnostic at the given severity. Each call ends with a single
// trailing newline; callers pass the message without one.
void error(std::string_view msg);
void warning(std::string_view msg);
void info(std::string_view msg);
void debug(std::string_view msg);

// Set the global log level. Messages strictly below this level are
// dropped silently. Called once at startup; not thread-safe beyond that.
void set_log_level(Level l);

// Parse a level string (case-insensitive). Unknown → Info. Accepts:
// "debug", "info", "warning"/"warn", "error". Empty string → Info.
Level parse_log_level(std::string_view s);

// Query the current level (primarily for tests and debug introspection).
Level current_log_level();

} // namespace tash::io

#endif // TASH_UTIL_IO_H
