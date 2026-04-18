#include "tash/core/builtins.h"
#include "tash/core/executor.h"
#include "tash/core/parser.h"
#include "tash/core/signals.h"
#include "tash/ui/rich_output.h"
#include "tash/util/io.h"
#include "tash/util/limits.h"
#include "tash/util/safe_tmpdir.h"
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unordered_set>
#include <vector>

using namespace std;

// Open a private, unlinked temp file seeded with `body`, positioned at
// offset 0 for reading. Used for stdin heredoc redirection. Returns the
// fd, or -1 on error. Matches the bash/dash/ksh approach and avoids the
// pipe-buffer deadlock for heredoc bodies larger than PIPE_BUF.
//
// Security (deep-review finding #2): TMPDIR is honoured only when it
// is owned by us with mode 0700; otherwise we fall back to /tmp. The
// previous code trusted $TMPDIR unconditionally, which was a
// symlink/TOCTOU race if an attacker could set TMPDIR to a directory
// they also controlled.
//
// Security (deep-review finding #4): body size is capped at
// TASH_MAX_HEREDOC_BYTES -- a runaway redirection can't fill /tmp.
int open_heredoc_fd(const std::string &body) {
    if (body.size() > tash::util::TASH_MAX_HEREDOC_BYTES) {
        write_stderr("tash: heredoc body exceeds maximum size (100 MiB)\n");
        return -1;
    }

    std::string base = tash::util::resolve_safe_tmpdir();
    std::string pattern = base + "/tash-hd-XXXXXX";
    std::vector<char> buf(pattern.begin(), pattern.end());
    buf.push_back('\0');
    int fd = ::mkstemp(buf.data());
    if (fd < 0 && errno == ENOENT && base != "/tmp") {
        // Fallback: resolved tmp dir is invalid for some reason, try /tmp.
        std::string fallback = "/tmp/tash-hd-XXXXXX";
        std::vector<char> fb(fallback.begin(), fallback.end());
        fb.push_back('\0');
        fd = ::mkstemp(fb.data());
        if (fd < 0) return -1;
        buf = std::move(fb);  // for the unlink below
    }
    if (fd < 0) return -1;
    // Unlink immediately: the file is now private to our process tree.
    ::unlink(buf.data());
    // Tighten perms explicitly so umask can't widen the file.
    (void)::fchmod(fd, 0600);
    // CLOEXEC via fcntl (two-step F_GETFD + F_SETFD). Linux offers
    // O_CLOEXEC via mkostemp, but macOS lacks mkostemp as of this writing,
    // so we stick with the portable path.
    int flags = ::fcntl(fd, F_GETFD);
    if (flags >= 0) ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);

    size_t off = 0;
    while (off < body.size()) {
        ssize_t w = ::write(fd, body.data() + off, body.size() - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            ::close(fd);
            return -1;
        }
        off += static_cast<size_t>(w);
    }
    if (::lseek(fd, 0, SEEK_SET) == static_cast<off_t>(-1)) {
        ::close(fd);
        return -1;
    }
    return fd;
}

void setup_child_io(const vector<Redirection> &redirections) {
    for (const Redirection &r : redirections) {
        if (r.dup_to_stdout) {
            dup2(STDOUT_FILENO, STDERR_FILENO);
            continue;
        }
        if (r.fd == 0) {
            int in;
            if (r.is_heredoc) {
                in = open_heredoc_fd(r.heredoc_body);
                if (in < 0) {
                    write_stderr("tash: heredoc: tmpfile failed\n");
                    _exit(1);
                }
            } else {
                in = open(r.filename.c_str(), O_RDONLY);
                if (in < 0) {
                    write_stderr("tash: " + r.filename + ": No such file or directory\n");
                    _exit(1);
                }
            }
            dup2(in, STDIN_FILENO);
            close(in);
        } else if (r.fd == 1) {
            int out;
            if (r.append) {
                out = open(r.filename.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
            } else {
                out = open(r.filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            }
            if (out < 0) {
                write_stderr("tash: " + r.filename + ": Cannot open file\n");
                _exit(1);
            }
            dup2(out, STDOUT_FILENO);
            close(out);
        } else if (r.fd == 2) {
            int err = open(r.filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (err < 0) {
                write_stderr("tash: " + r.filename + ": Cannot open file\n");
                _exit(1);
            }
            dup2(err, STDERR_FILENO);
            close(err);
        }
    }
}

// Commands we never intercept stdout for — they rely on a real TTY
// (cursor addressing, alternate screen, raw input) and a buffering wrapper
// breaks them.
static bool is_interactive_cmd(const std::string &cmd) {
    static const std::unordered_set<std::string> interactive = {
        "vim", "vi", "nvim", "emacs", "nano", "less", "more",
        "man", "top", "htop", "btop", "tmux", "screen", "ssh",
        "fzf", "watch", "mc", "ranger", "tig",
    };
    // Basename only (strip path).
    size_t slash = cmd.find_last_of('/');
    std::string base = (slash == std::string::npos) ? cmd : cmd.substr(slash + 1);
    return interactive.count(base) > 0;
}

static bool auto_linkify_enabled() {
    const char *v = getenv("TASH_AUTO_LINKIFY");
    return v && *v && string(v) != "0";
}

int foreground_process(const vector<string> &argv,
                       const vector<Redirection> &redirections,
                       string *captured_stderr) {
    // Build C-style args for execvp
    vector<const char *> c_args;
    for (const string &a : argv) c_args.push_back(a.c_str());
    c_args.push_back(nullptr);

    int stderr_pipe[2] = {-1, -1};
    if (captured_stderr) {
        captured_stderr->clear();
        if (pipe(stderr_pipe) < 0) {
            tash::io::warning("could not capture stderr");
            captured_stderr = nullptr; // fall back to no capture
        }
    }

    // Optional stdout interception for URL linkification.
    int stdout_pipe[2] = {-1, -1};
    bool intercept_stdout = !argv.empty() &&
                            auto_linkify_enabled() &&
                            !is_interactive_cmd(argv[0]) &&
                            redirections.empty();
    if (intercept_stdout) {
        if (pipe(stdout_pipe) < 0) {
            intercept_stdout = false;
        }
    }

    int status;
    pid_t pid = fork();
    if (pid < 0) {
        exit_with_message("Error: Fork failed!\n", 1);
    } else if (pid == 0) {
        // Child
        if (captured_stderr && stderr_pipe[1] >= 0) {
            close(stderr_pipe[0]); // close read end in child
            dup2(stderr_pipe[1], STDERR_FILENO);
            close(stderr_pipe[1]);
        }
        if (intercept_stdout) {
            close(stdout_pipe[0]);
            dup2(stdout_pipe[1], STDOUT_FILENO);
            close(stdout_pipe[1]);
        }
        setup_child_io(redirections);
        execvp(c_args[0], const_cast<char *const *>(c_args.data()));
        string err_msg = string(c_args[0]) + ": " + strerror(errno) + "\n";
        write_stderr(err_msg);
        // _exit, not exit: the forked child must not run C++ global
        // destructors (replxx, sqlite, curl, plugin registry) — those
        // hold resources the parent still owns, and tearing them down
        // in the child occasionally segfaults (tash issue: $? = 139
        // instead of 127 on command-not-found, ~1 in 5 on macOS).
        _exit(127);
    } else {
        // Parent
        if (stderr_pipe[1] >= 0) close(stderr_pipe[1]); // close write end
        if (intercept_stdout) close(stdout_pipe[1]);

        fg_child_pid.store(pid, std::memory_order_release);

        // Drain stdout first (line-buffered linkify) so stderr capture below
        // doesn't deadlock on a child that writes a lot of stdout.
        if (intercept_stdout) {
            char buf[4096];
            std::string carry;
            ssize_t n;
            while ((n = read(stdout_pipe[0], buf, sizeof(buf))) > 0) {
                carry.append(buf, static_cast<size_t>(n));
                size_t start = 0;
                while (true) {
                    size_t nl = carry.find('\n', start);
                    if (nl == std::string::npos) break;
                    std::string line = carry.substr(start, nl - start + 1);
                    std::string linked = tash::ui::linkify_urls(line);
                    // Short write to STDOUT at shell exit is survivable;
                    // suppressing -Wunused-result with `if(...){}` matches
                    // the rest of this TU.
                    if (write(STDOUT_FILENO, linked.data(), linked.size())) {}
                    start = nl + 1;
                }
                carry.erase(0, start);
            }
            if (!carry.empty()) {
                std::string linked = tash::ui::linkify_urls(carry);
                if (write(STDOUT_FILENO, linked.data(), linked.size())) {}
            }
            close(stdout_pipe[0]);
        }

        // Read stderr BEFORE waitpid to prevent deadlock on large output
        if (captured_stderr && stderr_pipe[0] >= 0) {
            char buf[4096];
            ssize_t n;
            while ((n = read(stderr_pipe[0], buf, sizeof(buf) - 1)) > 0) {
                buf[n] = '\0';
                captured_stderr->append(buf, static_cast<size_t>(n));
                // Also show to user on real stderr
                if (write(STDERR_FILENO, buf, static_cast<size_t>(n))) {}
                if (captured_stderr->size() >= 4096) break;
            }
            close(stderr_pipe[0]);
        }

        waitpid(pid, &status, WUNTRACED);
        fg_child_pid.store(0, std::memory_order_release);

        // Check exit status properly
        if (WIFEXITED(status)) return WEXITSTATUS(status);
        if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
        return 0; // stopped
    }
    return 1;
}

void background_process(const vector<string> &argv,
                        ShellState &state,
                        const vector<Redirection> &redirections) {
    if (argv.size() < 2) {
        tash::io::error("bg: usage: bg <command> [args...]");
        return;
    }
    if ((int)state.core.background_processes.size() >= state.core.max_background_processes) {
        tash::io::error("Maximum number of background processes");
        return;
    }

    // argv[0] is "bg", actual command starts at argv[1]
    vector<const char *> c_args;
    for (size_t i = 1; i < argv.size(); i++) c_args.push_back(argv[i].c_str());
    c_args.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        exit_with_message("Error: Fork failed!\n", 1);
    } else if (pid == 0) {
        setup_child_io(redirections);
        execvp(c_args[0], const_cast<char *const *>(c_args.data()));
        string err_msg = string(c_args[0]) + ": " + strerror(errno) + "\n";
        write_stderr(err_msg);
        _exit(127);  // see note above foreground exec — skip C++ dtors in child
    } else {
        state.core.background_processes[pid] = argv[1];
        write_stdout("Background process with " + to_string(pid) + " Executing\n");
    }
}

void check_background_process_finished(unordered_map<pid_t, string> &background_processes) {
    // Drain all ready children in one call. Unix signals coalesce —
    // if 5 SIGCHLDs arrive while blocked, only one delivery is
    // observable. Previously this reaped a single pid per invocation,
    // which left zombies in the tracking map after batch completions
    // and eventually tripped the max-bg cap. Loop with WNOHANG until
    // waitpid reports no-more-ready.
    while (true) {
        int status;
        pid_t pid_finished = waitpid(-1, &status, WNOHANG | WCONTINUED | WUNTRACED);
        if (pid_finished <= 0) break;
        if (WIFCONTINUED(status)) {
            write_stdout("Background process with " + to_string(pid_finished) + " Continued\n");
        } else if (WIFSTOPPED(status)) {
            write_stdout("Background process with " + to_string(pid_finished) + " Stopped\n");
        } else if (WIFEXITED(status) || WIFSIGNALED(status)) {
            background_processes.erase(pid_finished);
            write_stdout("Background process with " + to_string(pid_finished) + " finished\n");
        }
    }
}

void reap_background_processes(unordered_map<pid_t, string> &background_processes) {
    while (sigchld_received) {
        sigchld_received = 0;
        check_background_process_finished(background_processes);
    }
}

int execute_pipeline(vector<PipelineSegment> &segments, ShellState *state) {
    int num_cmds = (int)segments.size();
    vector<int> pipefds(2 * (num_cmds - 1));
    for (int i = 0; i < num_cmds - 1; i++) {
        if (pipe(&pipefds[2 * i]) < 0) {
            exit_with_message("Error: Pipe creation failed!\n", 1);
        }
    }

    vector<pid_t> pids(num_cmds);
    for (int i = 0; i < num_cmds; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            exit_with_message("Error: Fork failed!\n", 1);
        } else if (pids[i] == 0) {
            // Wire pipe stdin/stdout first so per-segment redirections
            // (applied after) can still override (e.g. 2>/dev/null).
            if (i > 0) {
                dup2(pipefds[2 * (i - 1)], STDIN_FILENO);
            }
            if (i < num_cmds - 1) {
                dup2(pipefds[2 * i + 1], STDOUT_FILENO);
            }
            for (int j = 0; j < 2 * (num_cmds - 1); j++) {
                close(pipefds[j]);
            }
            setup_child_io(segments[i].redirections);

            // Subshell segment: parse + run the inner source as a
            // command line, exit with its last status.
            if (!segments[i].subshell_body.empty()) {
                ShellState fallback;
                ShellState &st = state ? *state : fallback;
                // Same rationale as executor.cpp subshell fork: the child
                // must not record history while sharing a post-fork SQLite
                // connection with the parent.
                st.exec.in_subshell = true;
                std::vector<CommandSegment> segs =
                    parse_command_line(segments[i].subshell_body);
                execute_command_line(segs, st);
                // Flush stdio before _exit — the child may have buffered
                // echo output (stdout is a pipe → fully buffered). _exit
                // bypasses libc cleanup, so we must flush explicitly.
                std::fflush(nullptr);
                _exit(st.core.last_exit_status);
            }

            // Normal segment: builtin or external.
            const vector<string> &argv = segments[i].argv;
            if (argv.empty()) { std::fflush(nullptr); _exit(0); }
            const auto &builtins = get_builtins();
            auto bit = builtins.find(argv[0]);
            if (bit != builtins.end()) {
                ShellState fallback;
                ShellState &st = state ? *state : fallback;
                int rc = bit->second(argv, st);
                std::fflush(nullptr);
                _exit(rc);
            }
            vector<const char *> c_args;
            for (const string &a : argv) c_args.push_back(a.c_str());
            c_args.push_back(nullptr);
            execvp(c_args[0], const_cast<char *const *>(c_args.data()));
            string err_msg = string(c_args[0]) + ": " + strerror(errno) + "\n";
            if (write(STDERR_FILENO, err_msg.c_str(), err_msg.size())) {}
            _exit(127);
        }
    }

    for (int j = 0; j < 2 * (num_cmds - 1); j++) {
        close(pipefds[j]);
    }

    int last_status = 0;
    for (int i = 0; i < num_cmds; i++) {
        int status;
        waitpid(pids[i], &status, 0);
        if (i == num_cmds - 1) {
            if (WIFEXITED(status)) last_status = WEXITSTATUS(status);
            else if (WIFSIGNALED(status)) last_status = 128 + WTERMSIG(status);
        }
    }
    return last_status;
}

// Back-compat overload. Wraps each argv in a PipelineSegment and
// attaches a single stdout redirect to the last stage when requested.
int execute_pipeline(vector<vector<string>> &pipeline_cmds,
                     const string &filename, bool redirect_flag,
                     ShellState *state) {
    vector<PipelineSegment> segs;
    segs.reserve(pipeline_cmds.size());
    for (auto &argv : pipeline_cmds) {
        PipelineSegment s;
        s.argv = argv;
        segs.push_back(std::move(s));
    }
    if (redirect_flag && !segs.empty()) {
        Redirection r;
        r.fd = 1;
        r.filename = filename;
        r.append = false;
        r.dup_to_stdout = false;
        segs.back().redirections.push_back(r);
    }
    return execute_pipeline(segs, state);
}
