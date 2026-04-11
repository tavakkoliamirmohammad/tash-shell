#include "shell.h"

void change_directory(vector<char *> args) {
    if (args.size() > 1 && args[1]) {
        int res = chdir(args[1]);
        if (res == -1) {
            show_error_command(args);
        }
    } else {
        int res = chdir(getenv("HOME"));
        if (res == -1) {
            show_error_command(args);
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
