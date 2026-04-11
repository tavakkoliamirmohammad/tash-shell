#include "shell.h"

void change_directory(vector<char *> args) {
    char cwd[MAX_SIZE];
    const char *target = nullptr;

    if (args.size() > 1 && args[1]) {
        string arg = args[1];
        if (arg == "-") {
            if (previous_directory.empty()) {
                write_stderr("cd: OLDPWD not set\n");
                return;
            }
            target = previous_directory.c_str();
        } else {
            target = args[1];
        }
    } else {
        target = getenv("HOME");
        if (!target) {
            write_stderr("cd: HOME not set\n");
            return;
        }
    }

    // Save current directory before changing
    if (getcwd(cwd, MAX_SIZE) == nullptr) {
        show_error_command(args);
        return;
    }

    int res = chdir(target);
    if (res == -1) {
        show_error_command(args);
    } else {
        previous_directory = string(cwd);
        // If cd -, print the new directory
        if (args.size() > 1 && args[1] && string(args[1]) == "-") {
            char new_cwd[MAX_SIZE];
            if (getcwd(new_cwd, MAX_SIZE) != nullptr) {
                write_stdout(string(new_cwd) + "\n");
            }
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

void background_process_signal(pid_t pid, int signal) {
    int res = kill(pid, signal);
    if (res == -1) {
        write_stderr(strerror(errno));
        write_stderr("\n");
    }
}
