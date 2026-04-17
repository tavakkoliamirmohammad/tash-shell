#include "tash/core.h"
#include "tash/ui.h"
#include "tash/history.h"
#include "tash/plugin.h"
#include "tash/plugins/safety_hook_provider.h"
#include "tash/plugins/alias_suggest_provider.h"
#include "tash/plugins/manpage_completion_provider.h"
#include "tash/util/benchmark.h"
#include "theme.h"
#include <cstring>
#include <sys/stat.h>
#include <sys/time.h>
#include <termios.h>

#ifdef TASH_AI_ENABLED
#include "tash/ai.h"
#include "tash/ai/contextual_ai.h"
#include "tash/core/structured_pipe.h"
#endif

using namespace std;
using namespace replxx;

// ── Signal-related globals (must be global for signal handlers) ─

volatile sig_atomic_t sigchld_received = 0;
volatile sig_atomic_t fg_child_pid = 0;

// ── Utility functions ──────────────────────────────────────────

void exit_with_message(const string &message, int exit_status) {
    if (write(STDERR_FILENO, message.c_str(), message.length())) {}
    exit(exit_status);
}

void write_stderr(const string &message) {
    if (write(STDERR_FILENO, message.c_str(), message.length())) {}
}

void write_stdout(const string &message) {
    if (write(STDOUT_FILENO, message.c_str(), message.length())) {}
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
        if (state.skip_execution) {
            state.skip_execution = false;
            state.last_exit_status = 1;
            return 1;
        }
    }

#ifdef TASH_AI_ENABLED
    // Structured pipeline (`cmd |> where ... |> sort-by ...`) short-circuits
    // before normal parsing so the `|>` operator isn't confused with `|`.
    if (tash::structured_pipe::has_structured_pipe(command)) {
        string out = tash::structured_pipe::execute_pipeline(command);
        write_stdout(out);
        if (!out.empty() && out.back() != '\n') write_stdout("\n");
        return 0;
    }
#endif

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
        return execute_pipeline(pipeline_cmds, redirect_file, redirect_flag, &state);
    }

    const auto &builtins = get_builtins();
    auto it = builtins.find(cmd.argv[0]);
    if (it != builtins.end()) {
        // Apply redirections to the parent process for the duration of the
        // builtin call so `pwd > file`, `theme list >/tmp/x`, etc. write to
        // the target file instead of the shell's stdout.
        int saved_stdin = -1, saved_stdout = -1, saved_stderr = -1;
        for (const Redirection &r : cmd.redirections) {
            if (r.dup_to_stdout) {
                if (saved_stderr == -1) saved_stderr = dup(STDERR_FILENO);
                dup2(STDOUT_FILENO, STDERR_FILENO);
                continue;
            }
            if (r.fd == 0) {
                int in = open(r.filename.c_str(), O_RDONLY);
                if (in < 0) {
                    write_stderr("tash: " + r.filename + ": " +
                                 strerror(errno) + "\n");
                    return 1;
                }
                if (saved_stdin == -1) saved_stdin = dup(STDIN_FILENO);
                dup2(in, STDIN_FILENO);
                close(in);
            } else if (r.fd == 1) {
                int flags = O_WRONLY | O_CREAT |
                            (r.append ? O_APPEND : O_TRUNC);
                int out = open(r.filename.c_str(), flags, 0644);
                if (out < 0) {
                    write_stderr("tash: " + r.filename + ": " +
                                 strerror(errno) + "\n");
                    return 1;
                }
                if (saved_stdout == -1) saved_stdout = dup(STDOUT_FILENO);
                dup2(out, STDOUT_FILENO);
                close(out);
            } else if (r.fd == 2) {
                int err = open(r.filename.c_str(),
                               O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (err < 0) {
                    write_stderr("tash: " + r.filename + ": " +
                                 strerror(errno) + "\n");
                    return 1;
                }
                if (saved_stderr == -1) saved_stderr = dup(STDERR_FILENO);
                dup2(err, STDERR_FILENO);
                close(err);
            }
        }

        int result = it->second(cmd.argv, state);

        if (saved_stdin  != -1) { dup2(saved_stdin,  STDIN_FILENO);  close(saved_stdin);  }
        if (saved_stdout != -1) { dup2(saved_stdout, STDOUT_FILENO); close(saved_stdout); }
        if (saved_stderr != -1) { dup2(saved_stderr, STDERR_FILENO); close(saved_stderr); }
        return result;
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

    state.last_stderr_output.clear();
    int result = foreground_process(cmd.argv, cmd.redirections, &state.last_stderr_output);

    if (result == 127) {
        string suggestion = suggest_command(cmd.argv[0]);
        if (!suggestion.empty()) {
            write_stderr(SUGGEST_TEXT + "tash: did you mean '" CAT_RESET + SUGGEST_CMD +
                        suggestion + CAT_RESET + SUGGEST_TEXT + "'?" CAT_RESET "\n");
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
        if (write(STDOUT_FILENO, "\n", 1)) {}
    }
}

void sigchld_handler(int) {
    sigchld_received = 1;
}

// ── Hint callback with history access ──────────────────────────

// Shared between hint callback and right-arrow handler
static string current_hint;

static Replxx::hints_t history_hint_callback(const string &input, int &context_len, Replxx::Color &color, Replxx &rx, const ShellState &state) {
    Replxx::hints_t hints;
    current_hint.clear();

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
            best = entry;
        }
    }
#ifdef TASH_AI_ENABLED
    // If no history prefix match, try context-aware suggestion
    if (best.empty() && !state.last_executed_cmd.empty()) {
        string ctx = context_suggest(state.last_executed_cmd, get_transition_map());
        if (!ctx.empty() && ctx.size() > input.size() &&
            ctx.compare(0, input.size(), input) == 0) {
            best = ctx;
        }
    }
#endif

    if (!best.empty()) {
        hints.push_back(best);
        current_hint = best;
    }

    return hints;
}

// ── Main ───────────────────────────────────────────────────────

#ifndef TESTING_BUILD
int main(int argc, char *argv[]) {
    // --benchmark prints a startup-stage breakdown and exits. Parsed before
    // argc > 2 check so `tash --benchmark` always wins.
    bool benchmark_mode = (argc == 2 && string(argv[1]) == "--benchmark");

    if (argc > 2) {
        exit_with_message("An error has occurred\n", 1);
    }

    StartupBenchmark bench;
    if (benchmark_mode) bench.start("Theme load");

    // Load theme strings (reads ~/.config/tash/theme.toml if present).
    load_user_theme();

    if (benchmark_mode) { bench.end(); bench.start("Plugin registration"); }

    // Register built-in hook providers.
    global_plugin_registry().register_hook_provider(
        std::make_unique<SafetyHookProvider>());
    global_plugin_registry().register_hook_provider(
        std::make_unique<AliasSuggestProvider>());
    global_plugin_registry().register_completion_provider(
        std::make_unique<ManpageCompletionProvider>());

    if (benchmark_mode) { bench.end(); bench.start("Shell state init"); }

    ShellState state;

    if (benchmark_mode) {
        bench.end();
        bench.start("Command cache");
        build_command_cache();
        bench.end();
        bench.start("History load");
        (void)history_file_path();
        bench.end();
        write_stdout(bench.report());
        return 0;
    }

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

#ifdef TASH_AI_ENABLED
    // Build context-aware suggestion map from history
    {
        string hist = history_file_path();
        if (!hist.empty()) {
            build_transition_map(hist, get_transition_map());
        }
    }
#endif

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

    // Interactive mode banner — Option A with Catppuccin Mocha palette
    if (isatty(STDIN_FILENO)) {
        write_stdout("\n");
        write_stdout(BANNER_FRAME + "   ╔══════════════════════════════════════════════╗" CAT_RESET "\n");
        write_stdout(BANNER_FRAME + "   ║" CAT_RESET "                                              " + BANNER_FRAME + "║" CAT_RESET "\n");
        write_stdout(BANNER_FRAME + "   ║" CAT_RESET "   " + BANNER_LOGO + "████████╗ █████╗ ███████╗██╗  ██╗" CAT_RESET "          " + BANNER_FRAME + "║" CAT_RESET "\n");
        write_stdout(BANNER_FRAME + "   ║" CAT_RESET "   " + BANNER_LOGO + "╚══██╔══╝██╔══██╗██╔════╝██║  ██║" CAT_RESET "          " + BANNER_FRAME + "║" CAT_RESET "\n");
        write_stdout(BANNER_FRAME + "   ║" CAT_RESET "   " + BANNER_LOGO + "   ██║   ███████║███████╗███████║" CAT_RESET "          " + BANNER_FRAME + "║" CAT_RESET "\n");
        write_stdout(BANNER_FRAME + "   ║" CAT_RESET "   " + BANNER_LOGO + "   ██║   ██╔══██║╚════██║██╔══██║" CAT_RESET "          " + BANNER_FRAME + "║" CAT_RESET "\n");
        write_stdout(BANNER_FRAME + "   ║" CAT_RESET "   " + BANNER_LOGO + "   ██║   ██║  ██║███████║██║  ██║" CAT_RESET "          " + BANNER_FRAME + "║" CAT_RESET "\n");
        write_stdout(BANNER_FRAME + "   ║" CAT_RESET "   " + BANNER_LOGO + "   ╚═╝   ╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝" CAT_RESET "          " + BANNER_FRAME + "║" CAT_RESET "\n");
        write_stdout(BANNER_FRAME + "   ║" CAT_RESET "                                              " + BANNER_FRAME + "║" CAT_RESET "\n");
        write_stdout(BANNER_FRAME + "   ║" CAT_RESET "   " + BANNER_TITLE + "Tavakkoli's Shell" CAT_RESET " " CAT_DIM "───" CAT_RESET " " + BANNER_VERSION + "v" TASH_VERSION_STRING CAT_RESET "               " + BANNER_FRAME + "║" CAT_RESET "\n");
        write_stdout(BANNER_FRAME + "   ║" CAT_RESET "   " + BANNER_FEATURE + "▸ syntax highlighting  ▸ autosuggestions" CAT_RESET "   " + BANNER_FRAME + "║" CAT_RESET "\n");
        write_stdout(BANNER_FRAME + "   ║" CAT_RESET "   " + BANNER_FEATURE + "▸ smart completions    ▸ catppuccin theme" CAT_RESET "  " + BANNER_FRAME + "║" CAT_RESET "\n");
#ifdef TASH_AI_ENABLED
        write_stdout(BANNER_FRAME + "   ║" CAT_RESET "   " + BANNER_FEATURE + "▸ AI powered          ▸ @ai to get started" CAT_RESET " " + BANNER_FRAME + "║" CAT_RESET "\n");
#endif
        write_stdout(BANNER_FRAME + "   ║" CAT_RESET "                                              " + BANNER_FRAME + "║" CAT_RESET "\n");
        write_stdout(BANNER_FRAME + "   ╚══════════════════════════════════════════════╝" CAT_RESET "\n");
        write_stdout("\n");

#ifdef TASH_AI_ENABLED
        // Prompt user to configure AI if not set up yet
        {
            string provider = ai_get_provider();
            string key = ai_load_provider_key(provider);
            if (key.empty() && provider != "ollama") {
                write_stdout(AI_LABEL + "tash ai" CAT_RESET + AI_SEPARATOR + " ─ " CAT_RESET
                             "AI features available! Set up now? [y/n] ");
                char setup_ch = 0;
                struct termios old_t, new_t;
                tcgetattr(STDIN_FILENO, &old_t);
                new_t = old_t;
                new_t.c_lflag &= ~(ICANON | ECHO);
                new_t.c_cc[VMIN] = 1;
                new_t.c_cc[VTIME] = 0;
                tcsetattr(STDIN_FILENO, TCSANOW, &new_t);
                if (read(STDIN_FILENO, &setup_ch, 1) != 1) setup_ch = 'n';
                tcsetattr(STDIN_FILENO, TCSANOW, &old_t);
                write_stdout(string(1, setup_ch) + "\n");
                if (setup_ch == 'y' || setup_ch == 'Y') {
                    ai_run_setup_wizard();
                } else {
                    write_stdout(CAT_DIM "  Tip: run @ai config anytime to set up.\n" CAT_RESET);
                }
                write_stdout("\n");
            }
        }
#endif
    }

    // ── Setup replxx ───────────────────────────────────────────
    Replxx rx;
    rx.set_max_history_size(1000);

    // Load persistent history only in interactive mode
    string hist_path = history_file_path();
    if (!hist_path.empty() && isatty(STDIN_FILENO)) {
        rx.history_load(hist_path);
    }

    // Tab = command/file completion only (no history suggestions)
    rx.set_completion_callback(
        [](const string &input, int &ctx) {
            return completion_callback(input, ctx);
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
            [&rx, &state](const string &input, int &ctx, Replxx::Color &color) {
                return history_hint_callback(input, ctx, color, rx, state);
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
    rx.set_max_hint_rows(1);
    rx.set_immediate_completion(true);
    rx.set_beep_on_ambiguous_completion(false);

    // Right arrow at end of line accepts the full hint (fish-style)
    rx.bind_key(Replxx::KEY::RIGHT,
        [&rx](char32_t code) {
            Replxx::State st(rx.get_state());
            int len = (int)strlen(st.text());
            if (st.cursor_position() >= len && !current_hint.empty()) {
                rx.set_state(Replxx::State(current_hint.c_str(), (int)current_hint.size()));
                current_hint.clear();
                return Replxx::ACTION_RESULT::CONTINUE;
            }
            return rx.invoke(Replxx::ACTION::MOVE_CURSOR_RIGHT, code);
        }
    );

    // Alt+Right: accept one word from the hint
    rx.bind_key(Replxx::KEY::meta(Replxx::KEY::RIGHT),
        [&rx](char32_t code) {
            Replxx::State st(rx.get_state());
            string current(st.text());
            int len = (int)current.size();
            if (st.cursor_position() >= len && !current_hint.empty() &&
                current_hint.size() > current.size()) {
                // Find the next word boundary in the hint after current text
                size_t pos = current.size();
                // Skip spaces
                while (pos < current_hint.size() && current_hint[pos] == ' ') pos++;
                // Skip word
                while (pos < current_hint.size() && current_hint[pos] != ' ') pos++;
                string partial = current_hint.substr(0, pos);
                rx.set_state(Replxx::State(partial.c_str(), (int)partial.size()));
                return Replxx::ACTION_RESULT::CONTINUE;
            }
            return rx.invoke(Replxx::ACTION::MOVE_CURSOR_ONE_WORD_RIGHT, code);
        }
    );

    // Alt+. : insert last argument of previous command
    rx.bind_key(Replxx::KEY::meta('.'),
        [&rx](char32_t) {
            Replxx::HistoryScan hs(rx.history_scan());
            string last_arg;
            // Find the most recent history entry
            while (hs.next()) {
                Replxx::HistoryEntry he(hs.get());
                string entry(he.text());
                // Extract last argument (last space-delimited token)
                size_t last_space = entry.find_last_of(" \t");
                if (last_space != string::npos && last_space + 1 < entry.size()) {
                    last_arg = entry.substr(last_space + 1);
                } else {
                    last_arg = entry;
                }
            }
            if (!last_arg.empty()) {
                // Insert the last argument at cursor position
                Replxx::State st(rx.get_state());
                string current(st.text());
                int cursor = st.cursor_position();
                string new_text = current.substr(0, cursor) + last_arg + current.substr(cursor);
                rx.set_state(Replxx::State(new_text.c_str(), cursor + (int)last_arg.size()));
            }
            return Replxx::ACTION_RESULT::CONTINUE;
        }
    );

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

#ifdef TASH_AI_ENABLED
        // Intercept @ai commands before normal execution
        if (is_ai_command(expanded)) {
            string prefill;
            state.last_exit_status = handle_ai_command(expanded, state, &prefill);
            if (!prefill.empty()) {
                rx.set_state(Replxx::State(prefill.c_str(), (int)prefill.size()));
            }
            continue;
        }
        // Natural-language question with trailing '?' → route through AI.
        if (is_ai_question(expanded)) {
            string prefill;
            state.last_exit_status =
                handle_ai_command("@ai " + expanded, state, &prefill);
            if (!prefill.empty()) {
                rx.set_state(Replxx::State(prefill.c_str(), (int)prefill.size()));
            }
            continue;
        }
#else
        // AI not compiled in — show a helpful message
        {
            string ai_check = expanded;
            while (!ai_check.empty() && ai_check.front() == ' ') ai_check.erase(ai_check.begin());
            if (ai_check.size() >= 3 && ai_check.substr(0, 3) == "@ai" &&
                (ai_check.size() == 3 || ai_check[3] == ' ')) {
                write_stderr("tash: AI features not available (built without OpenSSL)\n");
                state.last_exit_status = 1;
                continue;
            }
        }
#endif

        reap_background_processes(state.background_processes);

        double start_time = get_time_s();

        vector<CommandSegment> segments = parse_command_line(expanded);
        execute_command_line(segments, state);

        // Track last command for @ai explain
        state.last_command_text = expanded;
#ifdef TASH_AI_ENABLED
        state.last_executed_cmd = expanded;
#endif

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
