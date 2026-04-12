#include "shell.h"

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

// ── Command execution ──────────────────────────────────────────

int execute_single_command(string command, ShellState &state) {
    if (command.empty() || command.find_first_not_of(" \t") == string::npos) return 0;

    command = expand_variables(command, state.last_exit_status);
    command = expand_command_substitution(command);

    // Parse redirections and tokenize into Command struct
    Command cmd = parse_redirections(command);
    if (cmd.argv.empty()) return 0;

    // Alias expansion: if the first token is an alias, re-parse
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

    // Glob expansion
    cmd.argv = expand_globs(cmd.argv);

    // Color flag injection for known commands
    if (!cmd.argv.empty() && state.colorful_commands.count(cmd.argv[0])) {
        cmd.argv.insert(cmd.argv.begin() + 1, COLOR_FLAG);
    }

    // Check for pipe segments (pipes are handled before builtins)
    // We need to re-check the original command for pipes since
    // parse_redirections consumed the command text
    vector<string> pipe_segments = tokenize_string(command, "|");
    if (pipe_segments.size() > 1) {
        vector<vector<string>> pipeline_cmds;
        string redirect_file;
        bool redirect_flag = false;

        for (size_t i = 0; i < pipe_segments.size(); i++) {
            Command seg_cmd = parse_redirections(pipe_segments[i]);

            // Alias expansion for each pipe segment
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

            // Check if last segment has stdout redirection
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

    // bg is special: it uses process.cpp's background_process
    if (cmd.argv[0] == "bg") {
        background_process(cmd.argv, state, cmd.redirections);
        return 0;
    }

    // External command
    return foreground_process(cmd.argv, cmd.redirections);
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
        if (should_run)
            last_exit = execute_single_command(segments[i].command, state);
    }
    state.last_exit_status = last_exit;
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
    }
}

void sigchld_handler(int) {
    sigchld_received = 1;
}

// ── Readline helpers ───────────────────────────────────────────

static int clear_screen(int, int) {
    write(STDOUT_FILENO, "\033[2J\033[H", 7);
    rl_on_new_line();
    rl_redisplay();
    return 0;
}

// ── Main ───────────────────────────────────────────────────────

#ifndef TESTING_BUILD
int main(int argc, char *argv[]) {
    if (argc > 2) {
        exit_with_message("An error has occurred\n", 1);
    }

    ShellState state;

    signal(SIGINT, sigint_handler);

    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, nullptr);

    // Script mode
    if (argc == 2) {
        return execute_script_file(argv[1], state);
    }

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
    rl_bind_key(12, clear_screen);
    rl_attempted_completion_function = tash_completion;
    using_history();
    stifle_history(500);

    while (true) {
        reap_background_processes(state.background_processes);

        line = readline(write_shell_prefix().c_str());
        if (line == NULL) {
            printf("\n");
            break;
        }
        if (!*line) {
            free(line);
            continue;
        }
        string raw_line(line);
        free(line);
        while (!raw_line.empty() && raw_line.back() == '\\') {
            raw_line.pop_back();
            char *cont = readline("> ");
            if (cont == NULL) break;
            raw_line += string(cont);
            free(cont);
        }
        string expanded = expand_history(raw_line);
        if (expanded.empty()) {
            continue;
        }
        if (expanded != raw_line) {
            write_stdout(expanded + "\n");
        }
        int offset = where_history();
        if (offset >= 1 && expanded != string(history_get(offset)->line)) {
            add_history(expanded.c_str());
        } else if (offset == 0) {
            add_history(expanded.c_str());
        }
        reap_background_processes(state.background_processes);
        vector<CommandSegment> segments = parse_command_line(expanded);
        execute_command_line(segments, state);
        reap_background_processes(state.background_processes);
    }
    return 0;
}
#endif // TESTING_BUILD
