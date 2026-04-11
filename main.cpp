#include "shell.h"

// TODO fix string space bug
//TODO add piping

unordered_set<string> colorful_commands = {"ls", "la", "ll", "less", "grep", "egrep", "fgrep", "zgrep"};

char hostname[MAX_SIZE];

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

void execute_commands(const vector<string> &commands, unordered_map<pid_t, string> &background_processes,
                      int maximum_background_process) {
    for (string command:commands) {
        vector<string> temp = tokenize_string(command, ">");
        int flag = 0;
        string filename;
        if (temp.size() > 1) {
            command = temp[0];
            filename = temp[1];
            flag = 1;
        }

        vector<string> tokenize_command = tokenize_string(command, " ");
        if (colorful_commands.find(tokenize_command[0]) != colorful_commands.end()) {
            tokenize_command.emplace_back("--color=auto");
        }
        vector<char *> arguments;
        arguments.reserve(tokenize_command.size() + 2);
        for (const string &token : tokenize_command) {
            arguments.push_back(const_cast<char *>(token.c_str()));
        }
        arguments.push_back(nullptr);
        string file = arguments[0];
        if (file == "cd") {
            change_directory(arguments);
        } else if (file == "pwd") {
            show_current_directory(arguments);
        } else if (file == "exit") {
            write_stdout("GoodBye! See you soon!\n");
            exit(0);
        } else if (file == "bglist") {
            show_background_process(background_processes);
        } else if (file == "bgkill") {
            pid_t pid = get_nth_background_process(background_processes, stoi(arguments[1]));
            if (pid == -1) {
                stringstream ss;
                ss << file << ": " << "Invalid n number" << endl;
                write_stderr(ss.str());
                return;
            }
            background_process_signal(pid, SIGTERM);

        } else if (file == "bgstop") {
            pid_t pid = get_nth_background_process(background_processes, stoi(arguments[1]));
            if (pid == -1) {
                stringstream ss;
                ss << file << ": " << "Invalid n number" << endl;
                write_stderr(ss.str());
                return;
            }
            background_process_signal(pid, SIGSTOP);

        } else if (file == "bgstart") {
            pid_t pid = get_nth_background_process(background_processes, stoi(arguments[1]));
            if (pid == -1) {
                stringstream ss;
                ss << file << ": " << "Invalid n number" << endl;
                write_stderr(ss.str());
                return;
            }
            background_process_signal(pid, SIGCONT);

        } else if (file == "bg") {
            background_process(arguments, background_processes, maximum_background_process, filename, flag);
        } else {
            foreground_process(arguments, filename, flag);
        }
    }
}

void f(int signum) {

}

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
    signal(SIGINT, f);
    while (true) {
        line = readline(write_shell_prefix().c_str());
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
        check_background_process_finished(background_processes);
        vector<string> commands = tokenize_string(line, "&&");
        execute_commands(commands, background_processes, maximum_background_process);
        check_background_process_finished(background_processes);
        free(line);
    }
    return 0;
}
