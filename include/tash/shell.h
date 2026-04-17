#ifndef TASH_SHELL_H
#define TASH_SHELL_H

#include <unistd.h>
#include <string>
#include <vector>
#include <signal.h>
#include <sys/types.h>
#include <atomic>
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

// Collected heredoc body, one per `<<` seen in a command. Lives here
// (before CommandSegment) so segments can carry a vector of them.
struct PendingHeredoc {
    std::string delim;
    bool strip_tabs = false;
    bool expand = true;
    std::string body;  // populated by the line-reader
};

struct CommandSegment {
    std::string command;
    OperatorType op;
    // Heredoc bodies declared by this segment, in appearance order.
    // Filled by the REPL / script reader before execution.
    std::vector<PendingHeredoc> heredocs;
};

// ── Redirection and Command IR ─────────────────────────────────

struct Redirection {
    int fd;
    std::string filename;
    bool append;
    bool dup_to_stdout;

    // Heredoc fields. When is_heredoc is true, the redirection's body is
    // streamed from heredoc_body rather than opened from filename.
    //   heredoc_delim       end-of-body marker (as the user wrote it)
    //   heredoc_strip_tabs  true for `<<-` form (leading tabs stripped)
    //   heredoc_expand      false when the delimiter was quoted
    //   heredoc_body        the accumulated body text (fills after read)
    bool is_heredoc = false;
    std::string heredoc_delim;
    bool heredoc_strip_tabs = false;
    bool heredoc_expand = true;
    std::string heredoc_body;
};

struct Command {
    std::vector<std::string> argv;
    // One entry per argv element: true when the raw token was enclosed in
    // single/double quotes (e.g. `echo '*'` → argv[1] = "*", quoted[1] = true).
    // Glob expansion must skip quoted tokens so `echo '*'` stays literal.
    std::vector<bool> argv_quoted;
    std::vector<Redirection> redirections;
};

// One stage of a pipeline. Either a normal command (argv set) or a
// subshell (subshell_body set to the inner source between parens).
// redirections includes heredocs; the child process consumes them via
// setup_child_io after the pipe fds are wired.
struct PipelineSegment {
    std::vector<std::string> argv;
    std::string subshell_body;
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

    // POSIX `trap` table: signum → command string. Signum 0 is the
    // EXIT pseudo-signal (run when the shell exits). Empty string is
    // "ignore" (SIG_IGN); absence is "default" (SIG_DFL).
    std::unordered_map<int, std::string> traps;
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
extern std::atomic<pid_t> fg_child_pid;

// Enforced in the header so every TU reading this atomic from a signal
// handler inherits the lock-free check.
static_assert(std::atomic<pid_t>::is_always_lock_free,
              "fg_child_pid must be lock-free for async-signal-safety");

// ── Builtin function type ──────────────────────────────────────

using BuiltinFn = std::function<int(const std::vector<std::string>&, ShellState&)>;

#endif // TASH_SHELL_H
