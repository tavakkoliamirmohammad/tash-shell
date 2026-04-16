#ifndef TASH_SHELL_H
#define TASH_SHELL_H

#include <unistd.h>
#include <string>
#include <vector>
#include <signal.h>
#include <sys/types.h>
#include <unordered_map>
#include <unordered_set>
#include <functional>

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
    int fd;
    std::string filename;
    bool append;
    bool dup_to_stdout;
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
    int ctrl_d_count;
    double last_cmd_duration;

    // AI features
    std::string last_command_text;
    std::string last_stderr_output;
    std::string last_executed_cmd;
    bool ai_enabled;

    // Safety hook
    bool skip_execution;

    ShellState()
        : last_exit_status(0)
        , colorful_commands({"ls", "la", "ll", "less", "grep", "egrep", "fgrep", "zgrep"})
        , max_background_processes(5)
        , ctrl_d_count(0)
        , last_cmd_duration(-1)
        , ai_enabled(true)
        , skip_execution(false) {}
};

// ── Signal-related globals (must be global for signal handlers) ─

extern volatile sig_atomic_t sigchld_received;
extern volatile sig_atomic_t fg_child_pid;

// ── Builtin function type ──────────────────────────────────────

using BuiltinFn = std::function<int(const std::vector<std::string>&, ShellState&)>;

#endif // TASH_SHELL_H
