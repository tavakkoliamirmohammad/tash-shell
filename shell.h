#ifndef SHELL_H
#define SHELL_H

#include <unistd.h>
#include <string>
#include <sstream>
#include <vector>
#include <iostream>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <regex>
#include <glob.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <stdexcept>
#include <cstdlib>
#include <fstream>

#include "colors.h"

// ── Constants ───────────────────────────────────────────────────
#define MAX_SIZE 1024

#ifdef __APPLE__
#define COLOR_FLAG "-G"
#else
#define COLOR_FLAG "--color=auto"
#endif

// ── Operator types for command separators ───────────────────────

enum OperatorType {
    OP_NONE,
    OP_AND,
    OP_OR,
    OP_SEMICOLON
};

struct CommandSegment {
    std::string command;
    OperatorType op;
};

// ── Redirection and Command IR ─────────────────────────────────

struct Redirection {
    int fd;               // 0=stdin, 1=stdout, 2=stderr
    std::string filename;
    bool append;          // true for >>
    bool dup_to_stdout;   // true for 2>&1
};

struct Command {
    std::vector<std::string> argv;
    std::vector<Redirection> redirections;
};

// ── Shell state ────────────────────────────────────────────────

struct ShellState {
    std::string previous_directory;
    int last_exit_status;
    std::unordered_set<std::string> colorful_commands;
    std::unordered_map<std::string, std::string> aliases;
    std::vector<std::string> dir_stack;
    std::unordered_map<pid_t, std::string> background_processes;
    int max_background_processes;

    ShellState()
        : last_exit_status(0)
        , colorful_commands({"ls", "la", "ll", "less", "grep", "egrep", "fgrep", "zgrep"})
        , max_background_processes(5) {}
};

// ── Signal-related globals (must be global for signal handlers) ─

extern volatile sig_atomic_t sigchld_received;
extern volatile sig_atomic_t fg_child_pid;

// ── Builtin function type ──────────────────────────────────────

using BuiltinFn = std::function<int(const std::vector<std::string>&, ShellState&)>;

// ── prompt.cpp ─────────────────────────────────────────────────

std::string write_shell_prefix();
std::string get_git_branch();

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
std::string expand_history(const std::string &line);
Command parse_redirections(const std::string &command_str);

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

// ── completion.cpp ────────────────────────────────────────────

char **tash_completion(const char *text, int start, int end);

// ── main.cpp ───────────────────────────────────────────────────

void exit_with_message(const std::string &message, int exit_status);
void write_stderr(const std::string &message);
void write_stdout(const std::string &message);
int execute_single_command(std::string command, ShellState &state);
void execute_command_line(const std::vector<CommandSegment> &segments, ShellState &state);
void sigint_handler(int signum);
void sigchld_handler(int signum);
int execute_script_file(const std::string &path, ShellState &state);

#endif // SHELL_H
