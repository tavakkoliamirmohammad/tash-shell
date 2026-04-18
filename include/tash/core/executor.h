#ifndef TASH_CORE_EXECUTOR_H
#define TASH_CORE_EXECUTOR_H

// Executor surface: process spawn, pipeline orchestration, script
// sourcing, hook-aware capture. Structs describing the command IR
// (Redirection, Command, CommandSegment, PipelineSegment, PendingHeredoc)
// live in tash/shell.h since the state struct needs to see them too.
//
// Split out of the old mega-header tash/core.h. `tash/core.h` still
// includes this so existing #include paths keep working.

#include "tash/shell.h"

#include <string>
#include <unordered_map>
#include <vector>

// Open a private, unlinked tmpfile seeded with `body`, positioned at
// offset 0 for reading. Used for stdin heredoc redirection. Returns
// the fd, or -1 on error. See src/core/process.cpp for semantics.
int open_heredoc_fd(const std::string &body);

void setup_child_io(const std::vector<Redirection> &redirections);
int foreground_process(const std::vector<std::string> &argv,
                       const std::vector<Redirection> &redirections,
                       std::string *captured_stderr = nullptr);
void background_process(const std::vector<std::string> &argv,
                        ShellState &state,
                        const std::vector<Redirection> &redirections);
void check_background_process_finished(std::unordered_map<pid_t, std::string> &background_processes);
void reap_background_processes(std::unordered_map<pid_t, std::string> &background_processes);
[[nodiscard]] int execute_pipeline(std::vector<std::vector<std::string>> &pipeline_cmds,
                                   const std::string &filename, bool redirect_flag,
                                   ShellState *state = nullptr);

// Richer pipeline entry: each segment carries its own redirections
// (including heredocs) and may declare itself a subshell via
// subshell_body. Used when the caller has already parsed per-segment
// structure; the legacy overload above is a thin wrapper.
[[nodiscard]] int execute_pipeline(std::vector<PipelineSegment> &segments,
                                   ShellState *state);

// ── Safety-hook-aware command execution ────────────────────────
//
// Runs `raw_cmd` via /bin/sh -c with stdout captured, after firing the
// plugin registry's before_command hooks. If a hook sets
// state.skip_execution, the command does not run and `skipped=true` is
// returned. after_command hooks fire even for non-zero exits so AI
// recovery / logging providers see the result.
//
// Used by expand_command_substitution ($(...)) and structured pipelines
// (|>) so that hooks see the inner command they would otherwise miss.
// The raw captured stdout is returned verbatim; callers strip trailing
// newlines if their context requires it.
struct [[nodiscard]] HookedCaptureResult {
    int exit_code;
    std::string captured_stdout;
    bool skipped;
};
HookedCaptureResult run_command_with_hooks_capture(const std::string &raw_cmd,
                                                    ShellState &state);

[[nodiscard]] int execute_single_command(std::string command, ShellState &state,
                                         std::vector<PendingHeredoc> *heredocs = nullptr);
void execute_command_line(const std::vector<CommandSegment> &segments, ShellState &state);
int execute_script_file(const std::string &path, ShellState &state);

#endif // TASH_CORE_EXECUTOR_H
