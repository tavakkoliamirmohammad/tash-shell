#include "shell.h"

void foreground_process(vector<char *> args, const string &filename, int flag) {
    int status;
    int pid = fork();
    if (pid < 0) {
        exit_with_message("Error: Fork failed!", 1);
    } else if (pid == 0) {
        int out;
        if (flag) {
            out = open(filename.c_str(), O_RDWR | O_CREAT, 0666);
            if (out < 0) {
                write_stderr("An error has occurred\n");
                exit(1);
            }
            dup2(out, STDOUT_FILENO);
        }
        execvp(args[0], &args[0]);
        show_error_command(args);
        if (flag) {
            close(out);
        }
        exit(0);
    } else {
        waitpid(pid, &status, WUNTRACED);
        int child_return_code = WEXITSTATUS(status);
//        if (child_return_code != 0) {
//            exit_with_message("Error: failed", 2);
//        }
    }
}

void background_process(vector<char *> args, unordered_map<pid_t, string> &background_processes_list,
                        int maximum_background_process, const string &filename, int flag) {
    if (background_processes_list.size() == maximum_background_process) {
        write_stderr("Error: Maximum number of background processes\n");
        return;
    }
    int pid = fork();
    if (pid < 0) {
        exit_with_message("Error: Fork failed!", 1);
    } else if (pid == 0) {
        int out;
        if (flag) {
            out = open(filename.c_str(), O_RDWR | O_CREAT, 0666);
            if (out < 0) {
                write_stderr("An error has occurred\n");
                exit(1);
            }
            dup2(out, STDOUT_FILENO);
        }
        execvp(args[1], &args[1]);
        show_error_command(vector<char *>(args.begin() + 1, args.end()));
        if (flag) {
            close(out);
        }
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
