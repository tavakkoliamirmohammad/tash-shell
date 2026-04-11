#include "shell.h"

// ── Global variable definitions ─────────────────────────────────

string previous_directory;
int last_exit_status = 0;
volatile sig_atomic_t sigchld_received = 0;
unordered_set<string> colorful_commands = {"ls", "la", "ll", "less", "grep", "egrep", "fgrep", "zgrep"};
unordered_map<string, string> aliases;
volatile sig_atomic_t fg_child_pid = 0;
char hostname[MAX_SIZE];

// ── Utility functions ───────────────────────────────────────────

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

void show_error_command(const vector<char *> &args) {
    write_stderr(args[0]);
    write_stderr(": ");
    write_stderr(strerror(errno));
    write_stderr("\n");
}

// ── Command execution ───────────────────────────────────────────

int execute_single_command(string command, unordered_map<pid_t, string> &background_processes,
                           int maximum_background_process) {
    command = expand_variables(command);
    command = expand_command_substitution(command);
    int flag = 0, append_flag = 0, input_flag = 0;
    int stderr_flag = 0, stderr_to_stdout = 0;
    string filename, input_filename, stderr_filename;

    // Check for 2>&1 (stderr to stdout) before other redirection parsing
    {
        string tmp_cmd;
        bool in_double_quotes = false;
        bool in_single_quotes = false;
        size_t i = 0;
        while (i < command.size()) {
            if (command[i] == '"' && !in_single_quotes) {
                in_double_quotes = !in_double_quotes;
                tmp_cmd += command[i];
                ++i;
            } else if (command[i] == '\'' && !in_double_quotes) {
                in_single_quotes = !in_single_quotes;
                tmp_cmd += command[i];
                ++i;
            } else if (!in_double_quotes && !in_single_quotes &&
                       i + 4 <= command.size() && command.compare(i, 4, "2>&1") == 0) {
                stderr_to_stdout = 1;
                i += 4;
            } else {
                tmp_cmd += command[i];
                ++i;
            }
        }
        command = tmp_cmd;
    }

    // Check for 2> file (stderr to file) — only if 2>&1 was not found
    if (!stderr_to_stdout) {
        string tmp_cmd;
        bool in_double_quotes = false;
        bool in_single_quotes = false;
        size_t i = 0;
        while (i < command.size()) {
            if (command[i] == '"' && !in_single_quotes) {
                in_double_quotes = !in_double_quotes;
                tmp_cmd += command[i];
                ++i;
            } else if (command[i] == '\'' && !in_double_quotes) {
                in_single_quotes = !in_single_quotes;
                tmp_cmd += command[i];
                ++i;
            } else if (!in_double_quotes && !in_single_quotes &&
                       i + 2 <= command.size() && command.compare(i, 2, "2>") == 0) {
                // Extract the filename after 2>
                i += 2;
                // Skip whitespace
                while (i < command.size() && command[i] == ' ') ++i;
                string fname;
                while (i < command.size() && command[i] != ' ' && command[i] != '\t') {
                    fname += command[i];
                    ++i;
                }
                fname = trim(fname);
                if (!fname.empty()) {
                    stderr_filename = fname;
                    stderr_flag = 1;
                }
            } else {
                tmp_cmd += command[i];
                ++i;
            }
        }
        command = tmp_cmd;
    }

    vector<string> temp = tokenize_string(command, ">>");
    if (temp.size() > 1) {
        command = temp[0]; filename = temp[1]; flag = 1; append_flag = 1;
    } else {
        temp = tokenize_string(command, ">");
        if (temp.size() > 1) { command = temp[0]; filename = temp[1]; flag = 1; }
    }
    temp = tokenize_string(command, "<");
    if (temp.size() > 1) { command = temp[0]; input_filename = temp[1]; input_flag = 1; }

    vector<string> pipe_segments = tokenize_string(command, "|");
    if (pipe_segments.size() > 1) {
        vector<vector<string>> all_tokens(pipe_segments.size());
        vector<vector<char *>> pipeline_args(pipe_segments.size());
        for (size_t i = 0; i < pipe_segments.size(); i++) {
            all_tokens[i] = tokenize_string(pipe_segments[i], " ");
            for (string &t : all_tokens[i])
                t = strip_quotes(t);
            // Alias expansion for each pipe segment
            if (!all_tokens[i].empty() && aliases.count(all_tokens[i][0])) {
                string expanded = aliases[all_tokens[i][0]];
                for (size_t j = 1; j < all_tokens[i].size(); j++)
                    expanded += " " + all_tokens[i][j];
                all_tokens[i] = tokenize_string(expanded, " ");
            }
            if (colorful_commands.find(all_tokens[i][0]) != colorful_commands.end())
                all_tokens[i].insert(all_tokens[i].begin() + 1, COLOR_FLAG);
            for (const string &token : all_tokens[i])
                pipeline_args[i].push_back(const_cast<char *>(token.c_str()));
            pipeline_args[i].push_back(nullptr);
        }
        execute_pipeline(pipeline_args, filename, flag);
        return 0;
    }

    vector<string> tokenize_command = tokenize_string(command, " ");
    for (string &t : tokenize_command)
        t = strip_quotes(t);

    // Alias expansion: if the first token is an alias, replace it
    if (!tokenize_command.empty() && aliases.count(tokenize_command[0])) {
        string expanded = aliases[tokenize_command[0]];
        for (size_t i = 1; i < tokenize_command.size(); i++)
            expanded += " " + tokenize_command[i];
        tokenize_command = tokenize_string(expanded, " ");
    }

    tokenize_command = expand_globs(tokenize_command);
    if (colorful_commands.find(tokenize_command[0]) != colorful_commands.end())
        tokenize_command.insert(tokenize_command.begin() + 1, COLOR_FLAG);
    vector<char *> arguments;
    arguments.reserve(tokenize_command.size() + 2);
    for (const string &token : tokenize_command)
        arguments.push_back(const_cast<char *>(token.c_str()));
    arguments.push_back(nullptr);

    string file = arguments[0];
    if (file == "cd") { change_directory(arguments); return 0; }
    else if (file == "pwd") { show_current_directory(arguments); return 0; }
    else if (file == "exit") { write_stdout("GoodBye! See you soon!\n"); exit(0); }
    else if (file == "export") {
        if (arguments[1] == nullptr) {
            for (char **env = environ; *env != nullptr; env++) write_stdout(string(*env) + "\n");
        } else {
            string arg = arguments[1];
            size_t eq_pos = arg.find('=');
            if (eq_pos != string::npos) {
                setenv(arg.substr(0, eq_pos).c_str(), arg.substr(eq_pos + 1).c_str(), 1);
            } else {
                write_stderr("export: invalid format. Usage: export VAR=value\n");
            }
        }
        return 0;
    } else if (file == "unset") {
        if (arguments[1] != nullptr) unsetenv(arguments[1]);
        else write_stderr("unset: missing variable name\n");
        return 0;
    } else if (file == "alias") {
        if (arguments[1] == nullptr) {
            // List all aliases
            for (auto &pair : aliases) {
                write_stdout("alias " + pair.first + "='" + pair.second + "'\n");
            }
        } else {
            // Parse alias definition: name='value' or name=value
            string arg = arguments[1];
            size_t eq_pos = arg.find('=');
            if (eq_pos != string::npos) {
                string name = arg.substr(0, eq_pos);
                string value = arg.substr(eq_pos + 1);
                // Strip surrounding quotes (single or double)
                if (value.size() >= 2 &&
                    ((value.front() == '\'' && value.back() == '\'') ||
                     (value.front() == '"' && value.back() == '"'))) {
                    value = value.substr(1, value.size() - 2);
                }
                aliases[name] = value;
            } else {
                // alias name — show that single alias
                if (aliases.count(arg)) {
                    write_stdout("alias " + arg + "='" + aliases[arg] + "'\n");
                } else {
                    write_stderr("alias: " + arg + ": not found\n");
                }
            }
        }
        return 0;
    } else if (file == "unalias") {
        if (arguments[1] != nullptr) {
            string name = arguments[1];
            if (aliases.count(name)) {
                aliases.erase(name);
            } else {
                write_stderr("unalias: " + name + ": not found\n");
            }
        } else {
            write_stderr("unalias: missing alias name\n");
        }
        return 0;
    } else if (file == "clear") {
        write_stdout("\033[2J\033[H");
        return 0;
    } else if (file == "bglist") { show_background_process(background_processes); return 0; }
    else if (file == "bgkill") {
        if (arguments.size() < 3) { write_stderr("bgkill: missing process number\n"); return 1; }
        int n; try { n = stoi(arguments[1]); }
        catch (const std::invalid_argument&) { write_stderr("bgkill: invalid process number\n"); return 1; }
        catch (const std::out_of_range&) { write_stderr("bgkill: process number out of range\n"); return 1; }
        pid_t pid = get_nth_background_process(background_processes, n);
        if (pid == -1) { write_stderr(file + ": Invalid n number\n"); return 1; }
        background_process_signal(pid, SIGTERM); return 0;
    } else if (file == "bgstop") {
        if (arguments.size() < 3) { write_stderr("bgstop: missing process number\n"); return 1; }
        int n; try { n = stoi(arguments[1]); }
        catch (const std::invalid_argument&) { write_stderr("bgstop: invalid process number\n"); return 1; }
        catch (const std::out_of_range&) { write_stderr("bgstop: process number out of range\n"); return 1; }
        pid_t pid = get_nth_background_process(background_processes, n);
        if (pid == -1) { write_stderr(file + ": Invalid n number\n"); return 1; }
        background_process_signal(pid, SIGSTOP); return 0;
    } else if (file == "bgstart") {
        if (arguments.size() < 3) { write_stderr("bgstart: missing process number\n"); return 1; }
        int n; try { n = stoi(arguments[1]); }
        catch (const std::invalid_argument&) { write_stderr("bgstart: invalid process number\n"); return 1; }
        catch (const std::out_of_range&) { write_stderr("bgstart: process number out of range\n"); return 1; }
        pid_t pid = get_nth_background_process(background_processes, n);
        if (pid == -1) { write_stderr(file + ": Invalid n number\n"); return 1; }
        background_process_signal(pid, SIGCONT); return 0;
    } else if (file == "history") {
        int len = history_length;
        for (int i = 0; i < len; i++) {
            HIST_ENTRY *entry = history_get(history_base + i);
            if (entry) { stringstream ss; ss << "  " << (i + 1) << "  " << entry->line << endl; write_stdout(ss.str()); }
        }
        return 0;
    } else if (file == "source" || file == ".") {
        if (arguments[1] == nullptr) {
            write_stderr("source: missing file argument\n");
            return 1;
        }
        return execute_script_file(arguments[1], background_processes, maximum_background_process);
    } else if (file == "bg") {
        background_process(arguments, background_processes, maximum_background_process, filename, flag,
                           input_filename, input_flag, append_flag,
                           stderr_filename, stderr_flag, stderr_to_stdout);
        return 0;
    } else {
        return foreground_process(arguments, filename, flag, input_filename, input_flag, append_flag,
                                  stderr_filename, stderr_flag, stderr_to_stdout);
    }
}

void execute_command_line(const vector<CommandSegment> &segments,
                          unordered_map<pid_t, string> &background_processes,
                          int maximum_background_process) {
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
            last_exit = execute_single_command(segments[i].command, background_processes, maximum_background_process);
    }
    last_exit_status = last_exit;
}

// ── Script file execution ──────────────────────────────────────

int execute_script_file(const string &path,
                        unordered_map<pid_t, string> &background_processes,
                        int maximum_background_process) {
    ifstream file(path);
    if (!file.is_open()) {
        write_stderr("tash: cannot open script: " + path + "\n");
        return 1;
    }
    string line;
    while (getline(file, line)) {
        if (line.empty()) continue;
        vector<CommandSegment> segments = parse_command_line(line);
        execute_command_line(segments, background_processes, maximum_background_process);
    }
    return 0;
}

// ── Signal handlers ─────────────────────────────────────────────

void sigint_handler(int signum) {
    if (fg_child_pid > 0) {
        kill(fg_child_pid, SIGINT);
    } else {
        write(STDOUT_FILENO, "\n", 1);
    }
}

void sigchld_handler(int signum) {
    sigchld_received = 1;
}

// ── Readline helpers ────────────────────────────────────────────

int clear_screen(int count, int key) {
    write(STDOUT_FILENO, "\033[2J\033[H", 7);
    rl_on_new_line();
    rl_redisplay();
    return 0;
}

// ── Main ────────────────────────────────────────────────────────

#ifndef TESTING_BUILD
int main(int argc, char *argv[]) {
    if (argc > 2) {
        string error_message = "An error has occurred\n";
        exit_with_message(error_message, 1);
    }
    unordered_map<pid_t, string> background_processes;
    int maximum_background_process = 5;

    signal(SIGINT, sigint_handler);

    // Install SIGCHLD handler with SA_RESTART so readline is not interrupted
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, nullptr);

    // Script mode: execute the given script file and exit
    if (argc == 2) {
        int rc = execute_script_file(argv[1], background_processes, maximum_background_process);
        return rc;
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
                    execute_command_line(rc_segments, background_processes, maximum_background_process);
                }
            }
            tashrc.close();
        }
    }

    // Interactive mode
    if (isatty(STDIN_FILENO)) {
        write_stdout("\n");
        write_stdout("  Welcome to Tash (Tavakkoli's Shell) v1.0.0\n");
        write_stdout("  Type 'exit' to quit, 'history' to see command history.\n");
        write_stdout("\n");
    }

    char *line;
    rl_initialize();
    rl_bind_key(12, clear_screen);
    using_history();
    stifle_history(10);

    while (true) {
        // Reap any background processes that finished while user was typing
        reap_background_processes(background_processes);

        line = readline(write_shell_prefix().c_str());
        if (line == NULL) {
            printf("\n");
            break;
        }
        if (!*line) {
            free(line);
            continue;
        }
        // Expand history references (!! and !N) before adding to history
        string raw_line(line);
        free(line);
        string expanded = expand_history(raw_line);
        if (expanded.empty()) {
            continue;
        }
        if (expanded != raw_line) {
            write_stdout(expanded + "\n");
        }
        // Add the expanded command to history (not the raw !! / !N)
        int offset = where_history();
        if (offset >= 1 && expanded != string(history_get(offset)->line)) {
            add_history(expanded.c_str());
        } else if (offset == 0) {
            add_history(expanded.c_str());
        }
        reap_background_processes(background_processes);
        vector<CommandSegment> segments = parse_command_line(expanded);
        execute_command_line(segments, background_processes, maximum_background_process);
        reap_background_processes(background_processes);
    }
    return 0;
}
#endif // TESTING_BUILD
