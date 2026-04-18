// Command execution pipeline — extracted from main.cpp as part of the
// main-split refactor. Pure command dispatch: parsing, alias expansion,
// builtin lookup with redirection, pipeline fan-out, structured-pipe
// short-circuit. Uses only public headers so future callers (scripts,
// embedded usage, tests) can reuse without pulling REPL machinery.

#include "tash/core/builtins.h"
#include "tash/core/executor.h"
#include "tash/core/parser.h"
#include "tash/core/signals.h"
#include "tash/history.h"
#include "tash/plugin.h"
#include "tash/ui.h"
#include "tash/util/fd.h"
#include "tash/util/io.h"
#include "tash/util/parse_error.h"
#include "theme.h"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>

#ifdef TASH_AI_ENABLED
#include "tash/core/structured_pipe.h"
#endif

using namespace std;

// ── Auto-cd helper ─────────────────────────────────────────────

static bool try_auto_cd(const string &token, ShellState &state) {
    struct stat st;
    if (stat(token.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        char cwd[MAX_SIZE];
        if (getcwd(cwd, MAX_SIZE) != nullptr) {
            if (chdir(token.c_str()) == 0) {
                state.core.previous_directory = string(cwd);
                char new_cwd[MAX_SIZE];
                if (getcwd(new_cwd, MAX_SIZE) != nullptr) {
                    z_record_directory(string(new_cwd));
                    write_stdout(string(new_cwd) + "\n");
                }
                return true;
            }
        }
    }
    return false;
}

// ── Apply/restore parent-side redirections for a builtin call ─
//
// Builtins run in the parent process so we can't just setup_child_io;
// we dup2 the target fds for the duration of the call and restore on
// the way out. Returns false when a file can't be opened (and already
// reported the error to stderr).
namespace {
struct BuiltinRedir {
    int saved_stdin = -1, saved_stdout = -1, saved_stderr = -1;
    bool ok = true;

    void apply(const vector<Redirection> &redirs) {
        // FileDescriptor wraps the open()/open_heredoc_fd() result so any
        // early return from a later redirection still closes the fd.
        // dup2-then-close is the classic pattern; letting the RAII guard
        // own the close keeps us from leaking on the failure paths that
        // return early (deep-review finding C3.4).
        using tash::util::FileDescriptor;
        for (const Redirection &r : redirs) {
            if (r.dup_to_stdout) {
                if (saved_stderr == -1) saved_stderr = dup(STDERR_FILENO);
                dup2(STDOUT_FILENO, STDERR_FILENO);
                continue;
            }
            if (r.fd == 0) {
                FileDescriptor in;
                if (r.is_heredoc) {
                    in = FileDescriptor(open_heredoc_fd(r.heredoc_body));
                    if (!in) { fail("heredoc"); return; }
                } else {
                    in = FileDescriptor(open(r.filename.c_str(), O_RDONLY));
                    if (!in) { fail(r.filename); return; }
                }
                if (saved_stdin == -1) saved_stdin = dup(STDIN_FILENO);
                dup2(in.get(), STDIN_FILENO);
                // `in` closes automatically at end of scope.
            } else if (r.fd == 1) {
                int flags = O_WRONLY | O_CREAT |
                            (r.append ? O_APPEND : O_TRUNC);
                FileDescriptor out(open(r.filename.c_str(), flags, 0644));
                if (!out) { fail(r.filename); return; }
                if (saved_stdout == -1) saved_stdout = dup(STDOUT_FILENO);
                dup2(out.get(), STDOUT_FILENO);
            } else if (r.fd == 2) {
                FileDescriptor err(open(r.filename.c_str(),
                                        O_WRONLY | O_CREAT | O_TRUNC, 0644));
                if (!err) { fail(r.filename); return; }
                if (saved_stderr == -1) saved_stderr = dup(STDERR_FILENO);
                dup2(err.get(), STDERR_FILENO);
            }
        }
    }

    void restore() {
        if (saved_stdin  != -1) { dup2(saved_stdin,  STDIN_FILENO);  close(saved_stdin);  }
        if (saved_stdout != -1) { dup2(saved_stdout, STDOUT_FILENO); close(saved_stdout); }
        if (saved_stderr != -1) { dup2(saved_stderr, STDERR_FILENO); close(saved_stderr); }
    }

private:
    void fail(const string &filename) {
        tash::io::error(filename + ": " + strerror(errno));
        ok = false;
    }
};
} // namespace

// ── Hook-aware captured-stdout command execution ──────────────
//
// Drop-in replacement for the `popen(cmd, "r")` calls scattered across
// command-substitution and structured-pipeline paths. Runs `raw_cmd`
// via /bin/sh -c after firing before_command hooks so the safety hook
// can block dangerous inner commands and after_command fires for AI
// error recovery. Caller strips trailing newlines.
HookedCaptureResult run_command_with_hooks_capture(const string &raw_cmd,
                                                    ShellState &state) {
    HookedCaptureResult result{0, "", false};

    global_plugin_registry().fire_before_command(raw_cmd, state);
    if (state.exec.skip_execution) {
        // Matches the skip convention in execute_single_command: reset
        // the flag and return exit_code 1 without running the command.
        state.exec.skip_execution = false;
        result.exit_code = 1;
        result.skipped = true;
        return result;
    }

    int pfd[2];
    if (::pipe(pfd) < 0) {
        tash::io::error("run_command_with_hooks_capture: pipe failed");
        result.exit_code = 1;
        return result;
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pfd[0]);
        ::close(pfd[1]);
        tash::io::error("run_command_with_hooks_capture: fork failed");
        result.exit_code = 1;
        return result;
    }

    if (pid == 0) {
        // Child: replace stdout with the pipe write end, exec /bin/sh.
        // _exit (not exit) avoids flushing the parent's inherited stdio
        // buffers a second time.
        ::close(pfd[0]);
        ::dup2(pfd[1], STDOUT_FILENO);
        ::close(pfd[1]);
        // Reset signal dispositions that the parent shell customizes.
        // POSIX shells do this in command substitution children so SIGINT
        // from the terminal reaches the child instead of being caught by
        // the parent's handler inherited across fork.
        ::signal(SIGINT, SIG_DFL);
        ::signal(SIGQUIT, SIG_DFL);
        ::execl("/bin/sh", "sh", "-c", raw_cmd.c_str(), (char *)nullptr);
        _exit(127);
    }

    // Parent: drain the pipe before waitpid to avoid deadlock on large
    // outputs (the kernel pipe buffer is finite).
    ::close(pfd[1]);
    char buf[4096];
    ssize_t n;
    while ((n = ::read(pfd[0], buf, sizeof(buf))) > 0) {
        result.captured_stdout.append(buf, static_cast<size_t>(n));
    }
    ::close(pfd[0]);

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) break;
    }

    // Convert wait status to exit code using the same convention as
    // foreground_process in src/core/process.cpp: WEXITSTATUS for normal
    // exits, 128 + signal for termination by signal, 0 otherwise (stopped).
    if (WIFEXITED(status))       result.exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) result.exit_code = 128 + WTERMSIG(status);
    else                          result.exit_code = 0;

    // The 3-arg fire_before_command has no stderr equivalent; we pass
    // empty string to the 4-arg after_command so AI-error-recovery sees
    // a non-trigger case and skips. Future work (Task 10) could capture
    // stderr here too, but Tasks 8/9 don't need it.
    global_plugin_registry().fire_after_command(raw_cmd, result.exit_code,
                                                 "", state);
    return result;
}

// ── Public: execute a single command segment ──────────────────

int execute_single_command(string command, ShellState &state,
                           vector<PendingHeredoc> *heredocs) {
    if (command.empty() || command.find_first_not_of(" \t") == string::npos) return 0;

    // Backslash prefix bypasses safety/hook checks (e.g. "\rm -rf dir/").
    bool bypass_hooks = false;
    {
        size_t first = command.find_first_not_of(" \t");
        if (first != string::npos && command[first] == '\\') {
            bypass_hooks = true;
            command.erase(first, 1);
        }
    }

    if (!bypass_hooks) {
        global_plugin_registry().fire_before_command(command, state);
        if (state.exec.skip_execution) {
            state.exec.skip_execution = false;
            state.core.last_exit_status = 1;
            return 1;
        }
    }

    // Subshell: `(cmd1; cmd2)` runs in a forked child so cd/exports
    // don't leak into the parent shell. Must come before structured
    // pipe and redirection parsing so the parens aren't misread.
    {
        size_t first = command.find_first_not_of(" \t");
        if (first != string::npos && command[first] == '(') {
            // Find matching ')' respecting quotes + nesting.
            int depth = 0;
            bool in_s = false, in_d = false;
            size_t close = string::npos;
            for (size_t k = first; k < command.size(); ++k) {
                char ch = command[k];
                if (ch == '\'' && !in_d) in_s = !in_s;
                else if (ch == '"' && !in_s) in_d = !in_d;
                else if (!in_s && !in_d && ch == '(') ++depth;
                else if (!in_s && !in_d && ch == ')') {
                    if (--depth == 0) { close = k; break; }
                }
            }
            if (close == string::npos) {
                size_t ln = 1, col = 1;
                tash::parse::offset_to_line_col(command, first, ln, col);
                tash::parse::emit_parse_error(
                    {"unmatched '(' in subshell", ln, col});
                return 1;
            }
            // If `|` appears after `)`, let the pipeline branch below
            // handle this as a multi-stage command. The standalone path
            // only runs when there's no pipe on the rest of the line.
            string trailing = command.substr(close + 1);
            bool has_pipe = false;
            {
                bool is = false, id = false;
                for (size_t k = 0; k < trailing.size(); ++k) {
                    char ch = trailing[k];
                    if (ch == '\'' && !id) is = !is;
                    else if (ch == '"' && !is) id = !id;
                    else if (!is && !id && ch == '|' &&
                             (k + 1 >= trailing.size() || trailing[k + 1] != '|')) {
                        has_pipe = true; break;
                    }
                }
            }
            if (!has_pipe) {
                string inner = command.substr(first + 1, close - first - 1);
                Command redirs_only = parse_redirections(trailing);
                const std::vector<Redirection> &redirs = redirs_only.redirections;

                pid_t pid = fork();
                if (pid < 0) {
                    tash::io::error("fork failed for subshell");
                    return 1;
                }
                if (pid == 0) {
                    setup_child_io(redirs);
                    ShellState child_state = state;
                    // Mark as subshell so execute_command_line skips
                    // history recording — SQLite forbids sharing the DB
                    // connection across fork(), and attempting to write
                    // from the child races with the parent on the file
                    // lock, dropping the child's subsequent command
                    // output ~15% of the time.
                    child_state.exec.in_subshell = true;
                    std::vector<CommandSegment> segs = parse_command_line(inner);
                    execute_command_line(segs, child_state);
                    std::exit(child_state.core.last_exit_status);
                }
                // Parent-only trace. Don't debug() in the child branch —
                // post-fork children must not touch the shared registry.
                tash::io::debug("subshell: fork pid=" + std::to_string(pid) +
                                " body='" + inner + "'");
                int status;
                waitpid(pid, &status, 0);
                int rc = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
                tash::io::debug("subshell: exited pid=" + std::to_string(pid) +
                                " status=" + std::to_string(rc));
                return rc;
            }
            // has_pipe: fall through to the pipeline branch below.
        }
    }

#ifdef TASH_AI_ENABLED
    // Structured pipeline (`cmd |> where ... |> sort-by ...`) short-circuits
    // before normal parsing so the `|>` operator isn't confused with `|`.
    if (tash::structured_pipe::has_structured_pipe(command)) {
        string out = tash::structured_pipe::execute_pipeline(command, state);
        write_stdout(out);
        if (!out.empty() && out.back() != '\n') write_stdout("\n");
        return 0;
    }
#endif

    command = expand_variables(command, state.core.last_exit_status);
    command = expand_command_substitution(command, state);

    Command cmd = parse_redirections(command, heredocs);
    if (cmd.argv.empty()) return 0;

    // Unquoted-delimiter heredocs expand $VAR and $(cmd) in the body.
    // Quoted-delimiter heredocs stay literal.
    for (auto &r : cmd.redirections) {
        if (r.is_heredoc && r.heredoc_expand) {
            r.heredoc_body = expand_variables(r.heredoc_body,
                                              state.core.last_exit_status);
            r.heredoc_body = expand_command_substitution(r.heredoc_body, state);
        }
    }

    if (state.core.aliases.count(cmd.argv[0])) {
        string expanded = state.core.aliases[cmd.argv[0]];
        for (size_t i = 1; i < cmd.argv.size(); i++)
            expanded += " " + cmd.argv[i];
        vector<string> new_tokens = tokenize_string(expanded, " ");
        for (string &t : new_tokens) {
            t = expand_tilde(t);
            t = strip_quotes(t);
        }
        cmd.argv = new_tokens;
    }

    cmd.argv = expand_globs(cmd.argv, cmd.argv_quoted);

    if (!cmd.argv.empty() && state.core.colorful_commands.count(cmd.argv[0])) {
        cmd.argv.insert(cmd.argv.begin() + 1, COLOR_FLAG);
    }

    // Paren- and quote-aware pipe split. `(echo a) | grep a` must stay
    // one segment for the subshell; the generic tokenize_string would
    // split on the `|` and break the parens.
    vector<string> pipe_segments;
    {
        string cur;
        bool in_s = false, in_d = false;
        int depth = 0;
        for (size_t k = 0; k < command.size(); ++k) {
            char ch = command[k];
            if (ch == '\'' && !in_d) in_s = !in_s;
            else if (ch == '"' && !in_s) in_d = !in_d;
            else if (!in_s && !in_d && ch == '(') ++depth;
            else if (!in_s && !in_d && ch == ')') { if (depth > 0) --depth; }
            else if (!in_s && !in_d && depth == 0 && ch == '|' &&
                     (k + 1 >= command.size() || command[k + 1] != '|')) {
                pipe_segments.push_back(cur);
                cur.clear();
                continue;
            }
            cur += ch;
        }
        pipe_segments.push_back(cur);
    }
    if (pipe_segments.size() > 1) {
        vector<PipelineSegment> pipe_out;
        size_t hd_idx = 0;  // index into *heredocs, consumed in order

        for (size_t i = 0; i < pipe_segments.size(); i++) {
            std::string seg_cmd_str = pipe_segments[i];
            std::string trimmed = seg_cmd_str;
            trimmed = trim(trimmed);

            // Subshell segment: isolate the paren body and attach
            // trailing redirections to this segment only.
            if (!trimmed.empty() && trimmed.front() == '(') {
                // Find matching ')' with quote + nesting awareness.
                int dp = 0;
                bool is = false, id = false;
                size_t close_pos = std::string::npos;
                size_t lead = trimmed.find('(');
                for (size_t k = lead; k < trimmed.size(); ++k) {
                    char ch = trimmed[k];
                    if (ch == '\'' && !id) is = !is;
                    else if (ch == '"' && !is) id = !id;
                    else if (!is && !id && ch == '(') ++dp;
                    else if (!is && !id && ch == ')') {
                        if (--dp == 0) { close_pos = k; break; }
                    }
                }
                if (close_pos == std::string::npos) {
                    // `trimmed` is the segment after the pipe split, so the
                    // column reported here is relative to that segment, not
                    // the full input line. Good enough for the diagnostic
                    // to point at the right paren.
                    size_t ln = 1, col = 1;
                    tash::parse::offset_to_line_col(trimmed, lead, ln, col);
                    tash::parse::emit_parse_error(
                        {"unmatched '(' in pipeline subshell", ln, col});
                    return 1;
                }
                PipelineSegment ps;
                ps.subshell_body =
                    trimmed.substr(lead + 1, close_pos - lead - 1);
                // Per-segment redirections come from what's after `)`.
                std::string tail = trimmed.substr(close_pos + 1);
                Command tail_cmd = parse_redirections(tail);
                ps.redirections = tail_cmd.redirections;
                pipe_out.push_back(std::move(ps));
                continue;
            }

            // Per-segment heredoc body consumption: parse once to see
            // how many the segment wants, then pop that many from the
            // caller-supplied vector.
            auto seg_pending = scan_pending_heredocs(seg_cmd_str);
            std::vector<PendingHeredoc> seg_bodies;
            for (size_t k = 0; k < seg_pending.size(); ++k) {
                if (heredocs && hd_idx < heredocs->size()) {
                    seg_bodies.push_back((*heredocs)[hd_idx++]);
                } else {
                    seg_bodies.push_back(seg_pending[k]);
                }
            }
            Command seg_cmd = parse_redirections(
                seg_cmd_str, seg_bodies.empty() ? nullptr : &seg_bodies);

            if (!seg_cmd.argv.empty() && state.core.aliases.count(seg_cmd.argv[0])) {
                string expanded = state.core.aliases[seg_cmd.argv[0]];
                for (size_t j = 1; j < seg_cmd.argv.size(); j++)
                    expanded += " " + seg_cmd.argv[j];
                vector<string> new_tokens = tokenize_string(expanded, " ");
                for (string &t : new_tokens) {
                    t = expand_tilde(t);
                    t = strip_quotes(t);
                }
                seg_cmd.argv = new_tokens;
            }

            if (!seg_cmd.argv.empty() && state.core.colorful_commands.count(seg_cmd.argv[0]))
                seg_cmd.argv.insert(seg_cmd.argv.begin() + 1, COLOR_FLAG);

            // Unquoted-delim heredoc bodies expand $VAR / $(...) before
            // the child writes them to stdin.
            for (auto &r : seg_cmd.redirections) {
                if (r.is_heredoc && r.heredoc_expand) {
                    r.heredoc_body = expand_variables(
                        r.heredoc_body, state.core.last_exit_status);
                    r.heredoc_body = expand_command_substitution(r.heredoc_body, state);
                }
            }

            PipelineSegment ps;
            ps.argv = seg_cmd.argv;
            ps.redirections = seg_cmd.redirections;
            pipe_out.push_back(std::move(ps));
        }
        return execute_pipeline(pipe_out, &state);
    }

    const auto &builtins = get_builtins();
    auto it = builtins.find(cmd.argv[0]);
    if (it != builtins.end()) {
        BuiltinRedir r;
        r.apply(cmd.redirections);
        if (!r.ok) { r.restore(); return 1; }
        int result = it->second(cmd.argv, state);
        r.restore();
        return result;
    }

    if (cmd.argv[0] == "bg") {
        background_process(cmd.argv, state, cmd.redirections);
        return 0;
    }

    // Auto-cd: a single token that names a directory becomes `cd <token>`.
    if (cmd.argv.size() == 1 && cmd.redirections.empty()) {
        string expanded_token = expand_tilde(cmd.argv[0]);
        if (try_auto_cd(expanded_token, state)) {
            return 0;
        }
    }

    state.ai.last_stderr_output.clear();
    int result = foreground_process(cmd.argv, cmd.redirections, &state.ai.last_stderr_output);

    if (result == 127) {
        auto suggestion = suggest_command(cmd.argv[0]);
        if (suggestion) {
            write_stderr(SUGGEST_TEXT + "tash: did you mean '" CAT_RESET + SUGGEST_CMD +
                        *suggestion + CAT_RESET + SUGGEST_TEXT + "'?" CAT_RESET "\n");
        }
    }

    return result;
}

// ── Public: execute a full command line (with &&, ||, ;) ──────

void execute_command_line(const vector<CommandSegment> &segments, ShellState &state) {
    int last_exit = 0;
    for (size_t i = 0; i < segments.size(); ++i) {
        bool should_run = false;
        switch (segments[i].op) {
            case OP_NONE:      should_run = true; break;
            case OP_AND:       should_run = (last_exit == 0); break;
            case OP_OR:        should_run = (last_exit != 0); break;
            case OP_SEMICOLON: should_run = true; break;
        }
        if (should_run) {
            // Segments carry heredoc bodies collected by the REPL /
            // script reader; pass them down so parse_redirections can
            // stitch them onto their Redirection entries.
            std::vector<PendingHeredoc> hd = segments[i].heredocs;
            last_exit = execute_single_command(
                segments[i].command, state, hd.empty() ? nullptr : &hd);
            state.core.last_exit_status = last_exit;
            global_plugin_registry().fire_after_command(
                segments[i].command, last_exit,
                state.ai.last_stderr_output, state);

            // Record into every registered history provider (e.g. sqlite).
            // Subshells must NOT write: SQLite explicitly forbids sharing
            // a connection across fork() (the file lock state and WAL
            // handles are duplicated and both processes race). We'd hang
            // on lock acquisition ~15% of the time, dropping subsequent
            // command output. Parent shell records the subshell command
            // as a whole ("(echo a; echo b)") already.
            if (!state.exec.in_subshell) {
                HistoryEntry entry;
                entry.command = segments[i].command;
                entry.exit_code = last_exit;
                entry.timestamp = static_cast<int64_t>(time(nullptr));
                char cwd[MAX_SIZE];
                if (getcwd(cwd, MAX_SIZE)) entry.directory = cwd;
                global_plugin_registry().record_history(entry);
            }

            // Run any trap commands queued by signals received during
            // the command. Fires at a safe point (between commands)
            // rather than from the signal handler itself.
            check_and_fire_traps(state);
        }
    }
}

// ── Public: execute every command in a script file ────────────

int execute_script_file(const string &path, ShellState &state) {
    ifstream file(path);
    if (!file.is_open()) {
        tash::io::error("cannot open script: " + path);
        return 1;
    }
    string line;
    while (getline(file, line)) {
        if (line.empty()) continue;
        while (!line.empty() && line.back() == '\\') {
            line.pop_back();
            string next;
            if (!getline(file, next)) break;
            line += next;
        }
        // Collect heredoc bodies from subsequent script lines before
        // executing. Same distribution logic as the REPL: scan, read,
        // stitch onto segments in appearance order.
        std::vector<PendingHeredoc> all_heredocs = scan_pending_heredocs(line);
        if (!all_heredocs.empty()) {
            bool ok = collect_heredoc_bodies(
                all_heredocs,
                [&file](std::string &out) -> bool {
                    return static_cast<bool>(getline(file, out));
                });
            if (!ok) {
                tash::io::error("heredoc: unexpected EOF in script");
                return 1;
            }
        }
        vector<CommandSegment> segments = parse_command_line(line);
        size_t bod_idx = 0;
        for (auto &seg : segments) {
            auto seg_pending = scan_pending_heredocs(seg.command);
            for (size_t k = 0; k < seg_pending.size() &&
                               bod_idx < all_heredocs.size(); ++k, ++bod_idx) {
                seg_pending[k].body = all_heredocs[bod_idx].body;
            }
            seg.heredocs = std::move(seg_pending);
        }
        execute_command_line(segments, state);
    }
    return 0;
}
