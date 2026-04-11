#include "shell.h"

int foreground_process(vector<char *> args, const string &filename, int flag,
                       const string &input_filename, int input_flag, int append_flag,
                       const string &stderr_filename, int stderr_flag, int stderr_to_stdout) {
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
        if (stderr_to_stdout) {
            dup2(STDOUT_FILENO, STDERR_FILENO);
        } else if (stderr_flag) {
            int err = open(stderr_filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (err < 0) {
                write_stderr("An error has occurred\n");
                exit(1);
            }
            dup2(err, STDERR_FILENO);
            close(err);
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
                        const string &input_filename, int input_flag, int append_flag,
                        const string &stderr_filename, int stderr_flag, int stderr_to_stdout) {
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
        if (stderr_to_stdout) {
            dup2(STDOUT_FILENO, STDERR_FILENO);
        } else if (stderr_flag) {
            int err = open(stderr_filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (err < 0) {
                write_stderr("An error has occurred\n");
                exit(1);
            }
            dup2(err, STDERR_FILENO);
            close(err);
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
