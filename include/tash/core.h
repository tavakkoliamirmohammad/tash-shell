#ifndef TASH_CORE_H
#define TASH_CORE_H

#include "tash/shell.h"

#include <sstream>
#include <iostream>
#include <fcntl.h>
#include <sys/wait.h>
#include <regex>
#include <glob.h>
#include <stdexcept>
#include <cstdlib>
#include <fstream>

#include "replxx.hxx"

// ── parser.cpp ─────────────────────────────────────────────────

std::string &rtrim(std::string &s, const char *t = " \t\n\r\f\v");
std::string &ltrim(std::string &s, const char *t = " \t\n\r\f\v");
std::string &trim(std::string &s, const char *t = " \t\n\r\f\v");
std::vector<std::string> tokenize_string(std::string line, const std::string &delimiter);
std::string expand_variables(const std::string &input, int last_exit_status);
std::string expand_command_substitution(const std::string &input);
std::vector<std::string> expand_globs(const std::vector<std::string> &args);
std::string expand_tilde(const std::string &token);
std::string strip_quotes(const std::string &s);
std::vector<CommandSegment> parse_command_line(const std::string &line);
std::string expand_history_bang(const std::string &line, replxx::Replxx &rx);
Command parse_redirections(const std::string &command_str);
bool is_input_complete(const std::string &input);

// ── builtins.cpp ───────────────────────────────────────────────

const std::unordered_map<std::string, BuiltinFn>& get_builtins();
bool is_builtin(const std::string &name);

// ── process.cpp ────────────────────────────────────────────────

void setup_child_io(const std::vector<Redirection> &redirections);
int foreground_process(const std::vector<std::string> &argv,
                       const std::vector<Redirection> &redirections);
void background_process(const std::vector<std::string> &argv,
                        ShellState &state,
                        const std::vector<Redirection> &redirections);
void check_background_process_finished(std::unordered_map<pid_t, std::string> &background_processes);
void reap_background_processes(std::unordered_map<pid_t, std::string> &background_processes);
int execute_pipeline(std::vector<std::vector<std::string>> &pipeline_cmds,
                     const std::string &filename, bool redirect_flag);

// ── main.cpp ───────────────────────────────────────────────────

void exit_with_message(const std::string &message, int exit_status);
void write_stderr(const std::string &message);
void write_stdout(const std::string &message);
int execute_single_command(std::string command, ShellState &state);
void execute_command_line(const std::vector<CommandSegment> &segments, ShellState &state);
void sigint_handler(int signum);
void sigchld_handler(int signum);
int execute_script_file(const std::string &path, ShellState &state);

#endif // TASH_CORE_H
