#include <unistd.h>
#include <string>
#include <sstream>
#include <vector>
#include <iostream>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <signal.h>
#include <regex>
#include <glob.h>
#include "colors.h"

#include <readline/readline.h>
#include <readline/history.h>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>
#include <cstdlib>

extern char **environ;

#ifndef SHELL_H
enum OperatorType {
    OP_NONE,
    OP_AND,
    OP_OR,
    OP_SEMICOLON
};

struct CommandSegment {
    string command;
    OperatorType op;
};
#endif

volatile sig_atomic_t sigchld_received = 0;

void sigchld_handler(int signum) {
    sigchld_received = 1;
}


#define MAX_SIZE 1024

#ifdef __APPLE__
#define COLOR_FLAG "-G"
#else
#define COLOR_FLAG "--color=auto"
#endif

unordered_set<string> colorful_commands = {"ls", "la", "ll", "less", "grep", "egrep", "fgrep", "zgrep"};

volatile sig_atomic_t fg_child_pid = 0;

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

char hostname[MAX_SIZE];

string write_shell_prefix() {
    stringstream ss;
    gethostname(hostname, MAX_SIZE);
    char cwd[MAX_SIZE];
    getcwd(cwd, MAX_SIZE);
    const char *login = getlogin();
    const char *home = getenv("HOME");
    string user = login ? login : "user";
    string cwd_display = string(cwd);
    if (home) {
        cwd_display = regex_replace(cwd_display, regex(string(home)), "~");
    }
    ss << bold(red("\u21aa ")) << bold(green(user)) << bold(cyan("@")) << bold(green(hostname)) << " "
       << bold(cyan(cwd_display))
       << bold(yellow(" shell> "));
    return ss.str();
}

string &rtrim(std::string &s, const char *t = " \t\n\r\f\v") {
    return s.erase(s.find_last_not_of(t) + 1);
}

string &ltrim(std::string &s, const char *t = " \t\n\r\f\v") {
    return s.erase(0, s.find_first_not_of(t));
}

string &trim(std::string &s, const char *t = " \t\n\r\f\v") {
    return ltrim(rtrim(s, t), t);
}

string expand_variables(const string &input) {
    string result;
    size_t i = 0;
    while (i < input.size()) {
        if (input[i] == '$') {
            i++;
            if (i < input.size() && input[i] == '{') {
                // ${VAR} syntax
                i++; // skip '{'
                string var_name;
                while (i < input.size() && input[i] != '}') {
                    var_name += input[i];
                    i++;
                }
                if (i < input.size()) {
                    i++; // skip '}'
                }
                const char *val = getenv(var_name.c_str());
                if (val) {
                    result += val;
                }
            } else {
                // $VAR syntax: read alphanumeric + underscore
                string var_name;
                while (i < input.size() && (isalnum(input[i]) || input[i] == '_')) {
                    var_name += input[i];
                    i++;
                }
                if (!var_name.empty()) {
                    const char *val = getenv(var_name.c_str());
                    if (val) {
                        result += val;
                    }
                } else {
                    // lone '$' with no valid var name following
                    result += '$';
                }
            }
        } else {
            result += input[i];
            i++;
        }
    }
    return result;
}

void show_error_command(const vector<char *> &args) {
    write_stderr(args[0]);
    write_stderr(": ");
    write_stderr(strerror(errno));
    write_stderr("\n");
}

vector<string> tokenize_string(string line, const string &delimiter) {
    vector<string> tokens;
    string current;
    bool in_double_quotes = false;
    bool in_single_quotes = false;
    size_t i = 0;
    size_t len = line.size();
    size_t dlen = delimiter.size();

    while (i < len) {
        if (i + 1 < len && line[i] == '\\' && (line[i + 1] == '"' || line[i + 1] == '\'' || line[i + 1] == '\\')) {
            current += line[i + 1];
            i += 2;
            continue;
        }
        if (line[i] == '"' && !in_single_quotes) {
            in_double_quotes = !in_double_quotes;
            current += line[i];
            ++i;
            continue;
        }
        if (line[i] == '\'' && !in_double_quotes) {
            in_single_quotes = !in_single_quotes;
            current += line[i];
            ++i;
            continue;
        }
        if (!in_double_quotes && !in_single_quotes &&
            i + dlen <= len && line.compare(i, dlen, delimiter) == 0) {
            string token = current;
            token = trim(token);
            if (!token.empty()) {
                tokens.push_back(token);
            }
            current.clear();
            i += dlen;
            continue;
        }
        current += line[i];
        ++i;
    }

    string token = current;
    token = trim(token);
    if (!token.empty()) {
        tokens.push_back(token);
    }

    const char *home = getenv("HOME");
    if (home) {
        for (string &t : tokens) {
            if (!t.empty() && t[0] == '~') {
                t = string(home) + t.substr(1);
            }
        }
    }

    return tokens;
}

vector<string> expand_globs(const vector<string> &args) {
    vector<string> expanded;
    for (const string &arg : args) {
        if (arg.find_first_of("*?[") != string::npos) {
            glob_t glob_result;
            int ret = glob(arg.c_str(), GLOB_NOCHECK | GLOB_TILDE, nullptr, &glob_result);
            if (ret == 0) {
                for (size_t i = 0; i < glob_result.gl_pathc; i++) {
                    expanded.push_back(glob_result.gl_pathv[i]);
                }
            } else {
                expanded.push_back(arg);
            }
            globfree(&glob_result);
        } else {
            expanded.push_back(arg);
        }
    }
    return expanded;
}

int foreground_process(vector<char *> args, const string &filename, int flag,
                       const string &input_filename, int input_flag, int append_flag) {
    int status;
    int pid = fork();
    if (pid < 0) {
        exit_with_message("Error: Fork failed!", 1);
    } else if (pid == 0) {
        if (input_flag) {
            int in = open(input_filename.c_str(), O_RDONLY);
            if (in < 0) {
                write_stderr("An error has occurred\n");
                exit(1);
            }
            dup2(in, STDIN_FILENO);
            close(in);
        }
        int out;
        if (flag) {
            if (append_flag) {
                out = open(filename.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
            } else {
                out = open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            }
            if (out < 0) {
                write_stderr("An error has occurred\n");
                exit(1);
            }
            dup2(out, STDOUT_FILENO);
            close(out);
        }
        execvp(args[0], &args[0]);
        show_error_command(args);
        exit(0);
    } else {
        fg_child_pid = pid;
        waitpid(pid, &status, WUNTRACED);
        fg_child_pid = 0;
        return WEXITSTATUS(status);
    }
    return 1;
}

void background_process(vector<char *> args, unordered_map<pid_t, string> &background_processes_list,
                        int maximum_background_process, const string &filename, int flag,
                        const string &input_filename, int input_flag, int append_flag) {
    if (background_processes_list.size() == maximum_background_process) {
        write_stderr("Error: Maximum number of background processes\n");
        return;
    }
    int pid = fork();
    if (pid < 0) {
        exit_with_message("Error: Fork failed!", 1);
    } else if (pid == 0) {
        if (input_flag) {
            int in = open(input_filename.c_str(), O_RDONLY);
            if (in < 0) {
                write_stderr("An error has occurred\n");
                exit(1);
            }
            dup2(in, STDIN_FILENO);
            close(in);
        }
        int out;
        if (flag) {
            if (append_flag) {
                out = open(filename.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
            } else {
                out = open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            }
            if (out < 0) {
                write_stderr("An error has occurred\n");
                exit(1);
            }
            dup2(out, STDOUT_FILENO);
            close(out);
        }
        execvp(args[1], &args[1]);
        show_error_command(vector<char *>(args.begin() + 1, args.end()));
        exit(1);
    } else {
        background_processes_list[pid] = args[1];
        write_stdout("Background process with " + to_string(pid) + " Executing\n");
    }
}

void check_background_process_finished(unordered_map<pid_t, string> &background_processes) {
    int status;
    pid_t pid_finished = waitpid(-1, &status, WNOHANG | WCONTINUED | WUNTRACED);
    if (pid_finished > 0) {
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
        // Loop again in case another SIGCHLD arrived while we were reaping
    }
}

void show_background_process(unordered_map<pid_t, string> &background_processes) {
    int i = 0;
    for (auto &process : background_processes) {
        stringstream ss;
        ++i;
        ss << "(" << i << ")" << " " << process.second << endl;
        write_stdout(ss.str());
    }
    stringstream ss;
    ss << "Total Background Jobs: " << i << endl;
    write_stdout(ss.str());
}

void background_process_signal(pid_t pid, int signal) {
    int res = kill(pid, signal);
    if (res == -1) {
        write_stderr(strerror(errno));
        write_stderr("\n");
    }
}

pid_t get_nth_background_process(unordered_map<pid_t, string> &background_processes, int n) {
    int i = 1;
    for (auto &process : background_processes) {
        if (i == n) {
            return process.first;
        }
        ++i;
    }
    return -1;
}

void change_directory(vector<char *> args) {
    if (args.size() > 1 && args[1]) {
        int res = chdir(args[1]);
        if (res == -1) {
            show_error_command(args);
        }
    } else {
        const char *home = getenv("HOME");
        if (home) {
            int res = chdir(home);
            if (res == -1) {
                show_error_command(args);
            }
        } else {
            write_stderr("cd: HOME not set\n");
        }
    }
}

void show_current_directory(vector<char *> args) {
    char temp[MAX_SIZE];
    char *res;
    res = getcwd(temp, MAX_SIZE);
    if (res != nullptr) {
        string message = string(temp);
        message += "\n";
        write_stdout(message);
    } else {
        show_error_command(args);
    }
}

vector<CommandSegment> parse_command_line(const string &line) {
    vector<CommandSegment> segments;
    string current;
    bool in_quotes = false;
    size_t i = 0;
    OperatorType next_op = OP_NONE;

    while (i < line.size()) {
        char c = line[i];
        if (c == '"') {
            in_quotes = !in_quotes;
            current += c;
            ++i;
        } else if (!in_quotes && c == '&' && i + 1 < line.size() && line[i + 1] == '&') {
            string cmd = current;
            cmd = trim(cmd);
            if (!cmd.empty()) segments.push_back({cmd, next_op});
            next_op = OP_AND;
            current.clear();
            i += 2;
        } else if (!in_quotes && c == '|' && i + 1 < line.size() && line[i + 1] == '|') {
            string cmd = current;
            cmd = trim(cmd);
            if (!cmd.empty()) segments.push_back({cmd, next_op});
            next_op = OP_OR;
            current.clear();
            i += 2;
        } else if (!in_quotes && c == ';') {
            string cmd = current;
            cmd = trim(cmd);
            if (!cmd.empty()) segments.push_back({cmd, next_op});
            next_op = OP_SEMICOLON;
            current.clear();
            ++i;
        } else {
            current += c;
            ++i;
        }
    }
    string cmd = current;
    cmd = trim(cmd);
    if (!cmd.empty()) segments.push_back({cmd, next_op});
    return segments;
}

void execute_pipeline(vector<vector<char *>> &pipeline_args, const string &filename, int redirect_flag) {
    int num_cmds = pipeline_args.size();
    // Create num_cmds-1 pipes. pipes[i] connects command i stdout to command i+1 stdin.
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
            // Child process

            // If not the first command, redirect stdin from previous pipe's read end
            if (i > 0) {
                dup2(pipefds[2 * (i - 1)], STDIN_FILENO);
            }
            // If not the last command, redirect stdout to current pipe's write end
            if (i < num_cmds - 1) {
                dup2(pipefds[2 * i + 1], STDOUT_FILENO);
            }

            // If last command and output redirection is requested
            if (i == num_cmds - 1 && redirect_flag) {
                int out = open(filename.c_str(), O_RDWR | O_CREAT, 0666);
                if (out < 0) {
                    write_stderr("An error has occurred\n");
                    exit(1);
                }
                dup2(out, STDOUT_FILENO);
                close(out);
            }

            // Close all pipe fds in child
            for (int j = 0; j < 2 * (num_cmds - 1); j++) {
                close(pipefds[j]);
            }

            execvp(pipeline_args[i][0], &pipeline_args[i][0]);
            show_error_command(pipeline_args[i]);
            exit(1);
        }
    }

    // Parent: close all pipe fds
    for (int j = 0; j < 2 * (num_cmds - 1); j++) {
        close(pipefds[j]);
    }

    // Wait for all children
    for (int i = 0; i < num_cmds; i++) {
        int status;
        waitpid(pids[i], &status, 0);
    }
}

int execute_single_command(string command, unordered_map<pid_t, string> &background_processes,
                           int maximum_background_process) {
    command = expand_variables(command);
    int flag = 0, append_flag = 0, input_flag = 0;
    string filename, input_filename;

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
    } else if (file == "bg") {
        background_process(arguments, background_processes, maximum_background_process, filename, flag,
                           input_filename, input_flag, append_flag);
        return 0;
    } else {
        return foreground_process(arguments, filename, flag, input_filename, input_flag, append_flag);
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
}

void sigint_handler(int signum) {
    if (fg_child_pid > 0) {
        kill(fg_child_pid, SIGINT);
    } else {
        write(STDOUT_FILENO, "\n", 1);
    }
}

#ifndef TESTING_BUILD
int main(int argc, char *argv[]) {
    if (argc != 1) {
        string error_message = "An error has occurred\n";
        exit_with_message(error_message, 1);
    }
    unordered_map<pid_t, string> background_processes;
    char *line;
    int maximum_background_process = 5;
    rl_initialize();
    using_history();
    stifle_history(10);
//    execute_commands({"source", "/etc"}, background_processes, maximum_background_process);
    signal(SIGINT, sigint_handler);

    // Install SIGCHLD handler with SA_RESTART so readline is not interrupted
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, nullptr);

    while (true) {
        // Reap any background processes that finished while user was typing
        reap_background_processes(background_processes);

        line = readline(write_shell_prefix().c_str());
        if (line == NULL) {
            printf("\n");
            break;
        }
        int offset = where_history();
        if (*line) {
            if (offset >= 1 && strcmp(line, history_get(offset)->line) != 0) {
                add_history(line);
            } else if (offset == 0) {
                add_history(line);
            }
        } else {
            continue;
        }
//        getline(input_stream, line);
        reap_background_processes(background_processes);
        vector<CommandSegment> segments = parse_command_line(line);
        execute_command_line(segments, background_processes, maximum_background_process);
        reap_background_processes(background_processes);
        free(line);
    }
    return 0;
}
#endif // TESTING_BUILD