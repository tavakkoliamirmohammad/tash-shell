#ifndef TASH_CORE_H
#define TASH_CORE_H

#include "tash/shell.h"

#include <sstream>
#include <iostream>
#include <functional>
#include <fcntl.h>
#include <sys/wait.h>
#include <regex>
#include <glob.h>
#include <stdexcept>
#include <cstdlib>
#include <fstream>
#include <unistd.h>

#include "replxx.hxx"

// ── parser.cpp ─────────────────────────────────────────────────

std::string &rtrim(std::string &s, const char *t = " \t\n\r\f\v");
std::string &ltrim(std::string &s, const char *t = " \t\n\r\f\v");
std::string &trim(std::string &s, const char *t = " \t\n\r\f\v");
std::vector<std::string> tokenize_string(std::string line, const std::string &delimiter);
std::string expand_variables(const std::string &input, int last_exit_status);
std::string expand_command_substitution(const std::string &input);
std::vector<std::string> expand_globs(const std::vector<std::string> &args);
std::vector<std::string> expand_globs(const std::vector<std::string> &args,
                                       const std::vector<bool> &quoted);
std::string expand_tilde(const std::string &token);
std::string strip_quotes(const std::string &s);
std::vector<CommandSegment> parse_command_line(const std::string &line);
std::string expand_history_bang(const std::string &line, replxx::Replxx &rx);
Command parse_redirections(const std::string &command_str);
Command parse_redirections(const std::string &command_str,
                           std::vector<PendingHeredoc> *bodies);
bool is_input_complete(const std::string &input);

// Walk a full command line (post-replxx input, pre-segment-split) and
// return an ordered list of heredoc markers it declares. Caller uses
// this to know whether more input lines must be read to form the body.
std::vector<PendingHeredoc> scan_pending_heredocs(const std::string &line);

// Read body lines from `read_line` until each pending heredoc's
// delimiter appears alone on a line. Returns true on success, false if
// the stream ran out before all delimiters were satisfied. Performs
// leading-tab stripping for `<<-` forms.
bool collect_heredoc_bodies(std::vector<PendingHeredoc> &pending,
                            const std::function<bool(std::string&)> &read_line);

// ── builtins.cpp ───────────────────────────────────────────────

const std::unordered_map<std::string, BuiltinFn>& get_builtins();
bool is_builtin(const std::string &name);

// ── process.cpp ────────────────────────────────────────────────

void setup_child_io(const std::vector<Redirection> &redirections);
int foreground_process(const std::vector<std::string> &argv,
                       const std::vector<Redirection> &redirections,
                       std::string *captured_stderr = nullptr);
void background_process(const std::vector<std::string> &argv,
                        ShellState &state,
                        const std::vector<Redirection> &redirections);
void check_background_process_finished(std::unordered_map<pid_t, std::string> &background_processes);
void reap_background_processes(std::unordered_map<pid_t, std::string> &background_processes);
int execute_pipeline(std::vector<std::vector<std::string>> &pipeline_cmds,
                     const std::string &filename, bool redirect_flag,
                     ShellState *state = nullptr);

// Richer pipeline entry: each segment carries its own redirections
// (including heredocs) and may declare itself a subshell via
// subshell_body. Used when the caller has already parsed per-segment
// structure; the legacy overload above is a thin wrapper.
int execute_pipeline(std::vector<PipelineSegment> &segments,
                     ShellState *state);

// ── I/O primitives ─────────────────────────────────────────────
//
// Defined inline so plugin tests that don't link shell_lib (e.g.
// TEST_STANDALONE targets) still resolve them. Literally every other
// TU includes this header and calls at least one of these.

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

// ── main.cpp ───────────────────────────────────────────────────

int execute_single_command(std::string command, ShellState &state,
                           std::vector<PendingHeredoc> *heredocs = nullptr);
void execute_command_line(const std::vector<CommandSegment> &segments, ShellState &state);
int execute_script_file(const std::string &path, ShellState &state);

// Install SIGINT + SIGCHLD handlers. Idempotent-safe to call once per
// process lifetime (main/startup does this).
void install_signal_handlers();

// ── trap plumbing (signals.cpp) ────────────────────────────────

void install_trap_handler(int signum);
void uninstall_trap_handler(int signum);
void ignore_signal(int signum);
void check_and_fire_traps(ShellState &state);
void fire_exit_trap(ShellState &state);

#endif // TASH_CORE_H
