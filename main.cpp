#include "shell.h"
#include <sys/stat.h>
#include <sys/time.h>

using namespace std;

// ── Signal-related globals (must be global for signal handlers) ─

volatile sig_atomic_t sigchld_received = 0;
volatile sig_atomic_t fg_child_pid = 0;

// ── Utility functions ──────────────────────────────────────────

void exit_with_message(const string &message, int exit_status) {
    write(STDERR_FILENO, message.c_str(), message.length());
    exit(exit_status);
}

void write_stderr(const string &message) {
    write(STDERR_FILENO, message.c_str(), message.length());
}

void write_stdout(const string &message) {
    write(STDOUT_FILENO, message.c_str(), message.length());
}

// ── Time measurement ───────────────────────────────────────────

static double get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

// ── Auto-cd: check if token is a directory ─────────────────────

static bool try_auto_cd(const string &token, ShellState &state) {
    struct stat st;
    if (stat(token.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        char cwd[MAX_SIZE];
        if (getcwd(cwd, MAX_SIZE) != nullptr) {
            if (chdir(token.c_str()) == 0) {
                state.previous_directory = string(cwd);
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

// ── Command execution ──────────────────────────────────────────

int execute_single_command(string command, ShellState &state) {
    if (command.empty() || command.find_first_not_of(" \t") == string::npos) return 0;

    command = expand_variables(command, state.last_exit_status);
    command = expand_command_substitution(command);

    Command cmd = parse_redirections(command);
    if (cmd.argv.empty()) return 0;

    // Alias expansion
    if (state.aliases.count(cmd.argv[0])) {
        string expanded = state.aliases[cmd.argv[0]];
        for (size_t i = 1; i < cmd.argv.size(); i++)
            expanded += " " + cmd.argv[i];
        vector<string> new_tokens = tokenize_string(expanded, " ");
        for (string &t : new_tokens) {
            t = expand_tilde(t);
            t = strip_quotes(t);
        }
        cmd.argv = new_tokens;
    }

    cmd.argv = expand_globs(cmd.argv);

    if (!cmd.argv.empty() && state.colorful_commands.count(cmd.argv[0])) {
        cmd.argv.insert(cmd.argv.begin() + 1, COLOR_FLAG);
    }

    // Check for pipes
    vector<string> pipe_segments = tokenize_string(command, "|");
    if (pipe_segments.size() > 1) {
        vector<vector<string>> pipeline_cmds;
        string redirect_file;
        bool redirect_flag = false;

        for (size_t i = 0; i < pipe_segments.size(); i++) {
            Command seg_cmd = parse_redirections(pipe_segments[i]);

            if (!seg_cmd.argv.empty() && state.aliases.count(seg_cmd.argv[0])) {
                string expanded = state.aliases[seg_cmd.argv[0]];
                for (size_t j = 1; j < seg_cmd.argv.size(); j++)
                    expanded += " " + seg_cmd.argv[j];
                vector<string> new_tokens = tokenize_string(expanded, " ");
                for (string &t : new_tokens) {
                    t = expand_tilde(t);
                    t = strip_quotes(t);
                }
                seg_cmd.argv = new_tokens;
            }

            if (!seg_cmd.argv.empty() && state.colorful_commands.count(seg_cmd.argv[0]))
                seg_cmd.argv.insert(seg_cmd.argv.begin() + 1, COLOR_FLAG);

            if (i == pipe_segments.size() - 1) {
                for (const Redirection &r : seg_cmd.redirections) {
                    if (r.fd == 1) {
                        redirect_file = r.filename;
                        redirect_flag = true;
                    }
                }
            }

            pipeline_cmds.push_back(seg_cmd.argv);
        }
        return execute_pipeline(pipeline_cmds, redirect_file, redirect_flag);
    }

    // Dispatch builtins
    const auto &builtins = get_builtins();
    auto it = builtins.find(cmd.argv[0]);
    if (it != builtins.end()) {
        return it->second(cmd.argv, state);
    }

    if (cmd.argv[0] == "bg") {
        background_process(cmd.argv, state, cmd.redirections);
        return 0;
    }

    // Auto-cd: if not a builtin or command, check if it's a directory
    if (cmd.argv.size() == 1 && cmd.redirections.empty()) {
        string token = cmd.argv[0];
        // Expand tilde for auto-cd check
        string expanded_token = expand_tilde(token);
        if (try_auto_cd(expanded_token, state)) {
            return 0;
        }
    }

    // External command
    int result = foreground_process(cmd.argv, cmd.redirections);

    // "Did you mean?" suggestion on command-not-found
    if (result == 127) {
        string suggestion = suggest_command(cmd.argv[0]);
        if (!suggestion.empty()) {
            write_stderr("\033[2mtash: did you mean '\033[0m\033[1;33m" +
                        suggestion + "\033[0m\033[2m'?\033[0m\n");
        }
    }

    return result;
}

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
            last_exit = execute_single_command(segments[i].command, state);
            state.last_exit_status = last_exit;
        }
    }
}

// ── Script file execution ─────────────────────────────────────

int execute_script_file(const string &path, ShellState &state) {
    ifstream file(path);
    if (!file.is_open()) {
        write_stderr("tash: cannot open script: " + path + "\n");
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
        vector<CommandSegment> segments = parse_command_line(line);
        execute_command_line(segments, state);
    }
    return 0;
}

// ── Signal handlers ────────────────────────────────────────────

void sigint_handler(int) {
    if (fg_child_pid > 0) {
        kill(fg_child_pid, SIGINT);
    } else {
        write(STDOUT_FILENO, "\n", 1);
        rl_on_new_line();
        rl_redisplay();
    }
}

void sigchld_handler(int) {
    sigchld_received = 1;
}

// ── Readline helpers ───────────────────────────────────────────

static int clear_screen_handler(int, int) {
    write(STDOUT_FILENO, "\033[2J\033[H", 7);
    rl_on_new_line();
    rl_redisplay();
    return 0;
}

// ── Bracketed paste ────────────────────────────────────────────

static void enable_bracketed_paste() {
    if (isatty(STDOUT_FILENO)) {
        write(STDOUT_FILENO, "\033[?2004h", 8);
    }
}

static void disable_bracketed_paste() {
    if (isatty(STDOUT_FILENO)) {
        write(STDOUT_FILENO, "\033[?2004l", 8);
    }
}

// ── Main ───────────────────────────────────────────────────────

#ifndef TESTING_BUILD
int main(int argc, char *argv[]) {
    if (argc > 2) {
        exit_with_message("An error has occurred\n", 1);
    }

    ShellState state;

    struct sigaction sa_int;
    sa_int.sa_handler = sigint_handler;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;
    sigaction(SIGINT, &sa_int, nullptr);

    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, nullptr);

    // Script mode
    if (argc == 2) {
        return execute_script_file(argv[1], state);
    }

    // Build command cache for "did you mean?" suggestions
    build_command_cache();

    // Load ~/.tashrc if it exists
    const char *home_env = getenv("HOME");
    if (home_env) {
        string tashrc_path = string(home_env) + "/.tashrc";
        ifstream tashrc(tashrc_path);
        if (tashrc.is_open()) {
            string rc_line;
            while (getline(tashrc, rc_line)) {
                if (!rc_line.empty()) {
                    vector<CommandSegment> rc_segments = parse_command_line(rc_line);
                    execute_command_line(rc_segments, state);
                }
            }
            tashrc.close();
        }
    }

    // Interactive mode
    if (isatty(STDIN_FILENO)) {
        write_stdout("\n");
        write_stdout(bold(cyan("   ████████╗ █████╗ ███████╗██╗  ██╗\n")));
        write_stdout(bold(cyan("   ╚══██╔══╝██╔══██╗██╔════╝██║  ██║\n")));
        write_stdout(bold(cyan("      ██║   ███████║███████╗███████║\n")));
        write_stdout(bold(cyan("      ██║   ██╔══██║╚════██║██╔══██║\n")));
        write_stdout(bold(cyan("      ██║   ██║  ██║███████║██║  ██║\n")));
        write_stdout(bold(cyan("      ╚═╝   ╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝\n")));
        write_stdout("\n");
        write_stdout("   " + bold(white("Tavakkoli's Shell")) + " " + yellow("v1.0.0") + "\n");
        write_stdout("   " + string("Type ") + green("exit") + " to quit, " + green("history") + " for command history.\n");
        write_stdout("\n");
    }

    char *line;
    rl_initialize();
    rl_bind_key(12, clear_screen_handler);
    rl_attempted_completion_function = tash_completion;
    using_history();
    stifle_history(1000);

    // Only load persistent history and advanced features in interactive mode
    if (isatty(STDIN_FILENO)) {
        load_persistent_history();
        setup_prefix_history_search();
        enable_bracketed_paste();
    }

    while (true) {
        reap_background_processes(state.background_processes);

        line = readline(write_shell_prefix(state).c_str());
        if (line == NULL) {
            // Ctrl-D protection: require double Ctrl-D
            state.ctrl_d_count++;
            if (state.ctrl_d_count >= 2) {
                write_stdout("\n");
                disable_bracketed_paste();
                break;
            }
            write_stdout("\n");
            write_stderr("tash: press Ctrl-D again or type 'exit' to quit\n");
            continue;
        }
        state.ctrl_d_count = 0;

        if (!*line) {
            free(line);
            continue;
        }

        // Backslash line continuation
        string raw_line(line);
        free(line);
        while (!raw_line.empty() && raw_line.back() == '\\') {
            raw_line.pop_back();
            char *cont = readline("> ");
            if (cont == NULL) break;
            raw_line += string(cont);
            free(cont);
        }

        // Multiline editing: auto-continue on unclosed quotes or trailing operators
        while (!is_input_complete(raw_line)) {
            char *cont = readline("> ");
            if (cont == NULL) break;
            raw_line += "\n" + string(cont);
            free(cont);
        }

        // Replace newlines with semicolons for execution
        for (size_t i = 0; i < raw_line.size(); i++) {
            if (raw_line[i] == '\n') raw_line[i] = ';';
        }

        string expanded = expand_history(raw_line);
        if (expanded.empty()) {
            continue;
        }
        if (expanded != raw_line) {
            write_stdout(expanded + "\n");
        }

        // Record in history (with dedup and ignore-space)
        if (should_record_history(expanded)) {
            add_history(expanded.c_str());
            save_history_line(expanded);
        }

        // Reset prefix search state for next command
        reset_prefix_search();

        reap_background_processes(state.background_processes);

        // Measure command duration
        double start_time = get_time_ms();

        vector<CommandSegment> segments = parse_command_line(expanded);
        execute_command_line(segments, state);

        double end_time = get_time_ms();
        state.last_cmd_duration = end_time - start_time;

        reap_background_processes(state.background_processes);
    }

    disable_bracketed_paste();
    return 0;
}
#endif // TESTING_BUILD
