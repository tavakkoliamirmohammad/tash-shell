#include "shell.h"
#include <sys/stat.h>
#include <sys/time.h>

using namespace std;
using namespace replxx;

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

static double get_time_s() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

// ── Auto-cd ────────────────────────────────────────────────────

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

    const auto &builtins = get_builtins();
    auto it = builtins.find(cmd.argv[0]);
    if (it != builtins.end()) {
        return it->second(cmd.argv, state);
    }

    if (cmd.argv[0] == "bg") {
        background_process(cmd.argv, state, cmd.redirections);
        return 0;
    }

    // Auto-cd
    if (cmd.argv.size() == 1 && cmd.redirections.empty()) {
        string expanded_token = expand_tilde(cmd.argv[0]);
        if (try_auto_cd(expanded_token, state)) {
            return 0;
        }
    }

    int result = foreground_process(cmd.argv, cmd.redirections);

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
    }
}

void sigchld_handler(int) {
    sigchld_received = 1;
}

// ── Hint callback with history access ──────────────────────────

static Replxx::hints_t history_hint_callback(const string &input, int &context_len, Replxx::Color &color, Replxx &rx) {
    Replxx::hints_t hints;
    if (input.size() < 2) return hints;

    color = Replxx::Color::GRAY;
    context_len = (int)input.size();

    // Scan all history, keep the most recent (last) prefix match
    string best;
    Replxx::HistoryScan hs(rx.history_scan());
    while (hs.next()) {
        Replxx::HistoryEntry he(hs.get());
        string entry(he.text());
        if (entry.size() > input.size() && entry.compare(0, input.size(), input) == 0) {
            best = entry;  // keep overwriting — last match is most recent
        }
    }
    if (!best.empty()) {
        hints.push_back(best);
    }

    return hints;
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

    // Build command cache for suggestions and highlighting
    build_command_cache();

    // Load ~/.tashrc
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

    // Interactive mode banner
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

    // ── Setup replxx ───────────────────────────────────────────
    Replxx rx;
    rx.set_max_history_size(1000);

    // Load persistent history only in interactive mode
    string hist_path = history_file_path();
    if (!hist_path.empty() && isatty(STDIN_FILENO)) {
        rx.history_load(hist_path);
    }

    // Set up callbacks — completion includes history prefix matches as fallback
    rx.set_completion_callback(
        [&rx](const string &input, int &ctx) {
            auto results = completion_callback(input, ctx);
            // If no builtins/subcommands matched, try history prefix match
            if (results.empty() && input.size() >= 2) {
                ctx = (int)input.size();
                string best;
                Replxx::HistoryScan hs(rx.history_scan());
                while (hs.next()) {
                    Replxx::HistoryEntry he(hs.get());
                    string entry(he.text());
                    if (entry.size() > input.size() &&
                        entry.compare(0, input.size(), input) == 0) {
                        best = entry;  // last match = most recent
                    }
                }
                if (!best.empty()) {
                    results.emplace_back(best);
                }
            }
            return results;
        }
    );

    // Only enable highlighting and hints in interactive mode
    if (isatty(STDIN_FILENO)) {
        rx.set_highlighter_callback(
            [](const string &input, Replxx::colors_t &colors) {
                syntax_highlighter(input, colors);
            }
        );

        rx.set_hint_callback(
            [&rx](const string &input, int &ctx, Replxx::Color &color) {
                return history_hint_callback(input, ctx, color, rx);
            }
        );
    }

    // Enable bracketed paste only in interactive mode
    if (isatty(STDIN_FILENO)) {
        rx.enable_bracketed_paste();
    }

    // Ctrl-L clears screen
    rx.bind_key(Replxx::KEY::control('L'),
        [&rx](char32_t) {
            rx.clear_screen();
            return Replxx::ACTION_RESULT::CONTINUE;
        }
    );

    // Configure hint behavior
    rx.set_max_hint_rows(1);              // show only 1 hint (fish-style)
    rx.set_immediate_completion(true);    // Tab accepts immediately without second press
    rx.set_beep_on_ambiguous_completion(false);

    // ── Main loop ──────────────────────────────────────────────
    while (true) {
        reap_background_processes(state.background_processes);

        string prompt = write_shell_prefix(state);
        char const *line = rx.input(prompt);

        if (line == nullptr) {
            // Ctrl-D / EOF
            state.ctrl_d_count++;
            if (state.ctrl_d_count >= 2) {
                write_stdout("\n");
                break;
            }
            write_stdout("\n");
            write_stderr("tash: press Ctrl-D again or type 'exit' to quit\n");
            continue;
        }
        state.ctrl_d_count = 0;

        string raw_line(line);
        if (raw_line.empty()) continue;

        // Multiline: auto-continue on incomplete input
        while (!is_input_complete(raw_line)) {
            char const *cont = rx.input("> ");
            if (cont == nullptr) break;
            raw_line += "\n" + string(cont);
        }

        // Replace newlines with semicolons for execution
        for (size_t i = 0; i < raw_line.size(); i++) {
            if (raw_line[i] == '\n') raw_line[i] = ';';
        }

        string expanded = expand_history_bang(raw_line, rx);
        if (expanded.empty()) continue;
        if (expanded != raw_line) {
            write_stdout(expanded + "\n");
        }

        // Record in history
        if (should_record_history(expanded, rx)) {
            rx.history_add(expanded);
            if (!hist_path.empty()) {
                rx.history_save(hist_path);
            }
        }

        reap_background_processes(state.background_processes);

        double start_time = get_time_s();

        vector<CommandSegment> segments = parse_command_line(expanded);
        execute_command_line(segments, state);

        state.last_cmd_duration = get_time_s() - start_time;

        reap_background_processes(state.background_processes);
    }

    // Save history on exit
    if (!hist_path.empty()) {
        rx.history_save(hist_path);
    }

    return 0;
}
#endif // TESTING_BUILD
