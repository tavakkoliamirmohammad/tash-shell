#ifndef TASH_UTIL_SAFE_EXEC_H
#define TASH_UTIL_SAFE_EXEC_H

// Safe argv-based subprocess runner.
//
// Replaces ad-hoc popen(cmd_string, "r") call sites scattered across the
// shell. popen() hands its argument to /bin/sh -c, so any shell
// metacharacter in interpolated data (user-selected directory path,
// branch name, clipboard text, ...) is a command-injection vector. This
// helper forks and calls execvp with an argv vector directly -- no
// shell, no parsing.
//
// Behaviour:
//   * argv[0] is looked up via $PATH (execvp).
//   * stdout is captured into ExecResult::stdout_text.
//   * stderr is inherited by default. Pass suppress_stderr=true to
//     route the child's stderr to /dev/null — useful for probe-style
//     calls (e.g. `git rev-parse` in the prompt, which otherwise
//     leaks "fatal: not a git repository" to the terminal every
//     prompt render in a non-repo cwd).
//   * timeout_ms <= 0  ->  block until the child exits.
//   * timeout_ms >  0  ->  poll() the pipe; on timeout the child is
//                          killed (SIGKILL) and exit_code is set to -1.
//   * On fork/pipe failure the result carries exit_code = -1 and empty
//     stdout.

#include <string>
#include <vector>

namespace tash::util {

struct ExecResult {
    int exit_code = -1;
    std::string stdout_text;
};

// Runs argv[0] with argv[1..] as args. No shell is invoked. Captures
// stdout. stderr is inherited unless suppress_stderr is true, in
// which case it is redirected to /dev/null in the child.
ExecResult safe_exec(const std::vector<std::string>& argv,
                     int timeout_ms = -1,
                     bool suppress_stderr = false);

} // namespace tash::util

#endif // TASH_UTIL_SAFE_EXEC_H
