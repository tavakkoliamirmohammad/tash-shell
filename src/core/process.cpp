#include "tash/core.h"
#include <cstring>

using namespace std;

void setup_child_io(const vector<Redirection> &redirections) {
    for (const Redirection &r : redirections) {
        if (r.dup_to_stdout) {
            dup2(STDOUT_FILENO, STDERR_FILENO);
            continue;
        }
        if (r.fd == 0) {
            int in = open(r.filename.c_str(), O_RDONLY);
            if (in < 0) {
                write_stderr("tash: " + r.filename + ": No such file or directory\n");
                exit(1);
            }
            dup2(in, STDIN_FILENO);
            close(in);
        } else if (r.fd == 1) {
            int out;
            if (r.append) {
                out = open(r.filename.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
            } else {
                out = open(r.filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            }
            if (out < 0) {
                write_stderr("tash: " + r.filename + ": Cannot open file\n");
                exit(1);
            }
            dup2(out, STDOUT_FILENO);
            close(out);
        } else if (r.fd == 2) {
            int err = open(r.filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (err < 0) {
                write_stderr("tash: " + r.filename + ": Cannot open file\n");
                exit(1);
            }
            dup2(err, STDERR_FILENO);
            close(err);
        }
    }
}

int foreground_process(const vector<string> &argv,
                       const vector<Redirection> &redirections,
                       string *captured_stderr) {
    // Build C-style args for execvp
    vector<const char *> c_args;
    for (const string &a : argv) c_args.push_back(a.c_str());
    c_args.push_back(nullptr);

    int stderr_pipe[2] = {-1, -1};
    if (captured_stderr) {
        captured_stderr->clear();
        if (pipe(stderr_pipe) < 0) {
            write_stderr("tash: warning: could not capture stderr\n");
            captured_stderr = nullptr; // fall back to no capture
        }
    }

    int status;
    pid_t pid = fork();
    if (pid < 0) {
        exit_with_message("Error: Fork failed!\n", 1);
    } else if (pid == 0) {
        // Child
        if (captured_stderr && stderr_pipe[1] >= 0) {
            close(stderr_pipe[0]); // close read end in child
            dup2(stderr_pipe[1], STDERR_FILENO);
            close(stderr_pipe[1]);
        }
        setup_child_io(redirections);
        execvp(c_args[0], const_cast<char *const *>(c_args.data()));
        string err_msg = string(c_args[0]) + ": " + strerror(errno) + "\n";
        write_stderr(err_msg);
        exit(127);
    } else {
        // Parent
        if (stderr_pipe[1] >= 0) close(stderr_pipe[1]); // close write end

        fg_child_pid = pid;

        // Read stderr BEFORE waitpid to prevent deadlock on large output
        if (captured_stderr && stderr_pipe[0] >= 0) {
            char buf[4096];
            ssize_t n;
            while ((n = read(stderr_pipe[0], buf, sizeof(buf) - 1)) > 0) {
                buf[n] = '\0';
                captured_stderr->append(buf, static_cast<size_t>(n));
                // Also show to user on real stderr
                write(STDERR_FILENO, buf, static_cast<size_t>(n));
                if (captured_stderr->size() >= 4096) break;
            }
            close(stderr_pipe[0]);
        }

        waitpid(pid, &status, WUNTRACED);
        fg_child_pid = 0;

        // Check exit status properly
        if (WIFEXITED(status)) return WEXITSTATUS(status);
        if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
        return 0; // stopped
    }
    return 1;
}

void background_process(const vector<string> &argv,
                        ShellState &state,
                        const vector<Redirection> &redirections) {
    if ((int)state.background_processes.size() >= state.max_background_processes) {
        write_stderr("Error: Maximum number of background processes\n");
        return;
    }

    // argv[0] is "bg", actual command starts at argv[1]
    vector<const char *> c_args;
    for (size_t i = 1; i < argv.size(); i++) c_args.push_back(argv[i].c_str());
    c_args.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        exit_with_message("Error: Fork failed!\n", 1);
    } else if (pid == 0) {
        setup_child_io(redirections);
        execvp(c_args[0], const_cast<char *const *>(c_args.data()));
        string err_msg = string(c_args[0]) + ": " + strerror(errno) + "\n";
        write_stderr(err_msg);
        exit(127);
    } else {
        state.background_processes[pid] = argv[1];
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
    }
}

int execute_pipeline(vector<vector<string>> &pipeline_cmds,
                     const string &filename, bool redirect_flag) {
    int num_cmds = pipeline_cmds.size();
    vector<int> pipefds(2 * (num_cmds - 1));
    for (int i = 0; i < num_cmds - 1; i++) {
        if (pipe(&pipefds[2 * i]) < 0) {
            exit_with_message("Error: Pipe creation failed!\n", 1);
        }
    }

    vector<pid_t> pids(num_cmds);
    for (int i = 0; i < num_cmds; i++) {
        // Build C-style args
        vector<const char *> c_args;
        for (const string &a : pipeline_cmds[i]) c_args.push_back(a.c_str());
        c_args.push_back(nullptr);

        pids[i] = fork();
        if (pids[i] < 0) {
            exit_with_message("Error: Fork failed!\n", 1);
        } else if (pids[i] == 0) {
            if (i > 0) {
                dup2(pipefds[2 * (i - 1)], STDIN_FILENO);
            }
            if (i < num_cmds - 1) {
                dup2(pipefds[2 * i + 1], STDOUT_FILENO);
            }
            if (i == num_cmds - 1 && redirect_flag) {
                int out = open(filename.c_str(), O_RDWR | O_CREAT, 0666);
                if (out < 0) {
                    write_stderr("An error has occurred\n");
                    exit(1);
                }
                dup2(out, STDOUT_FILENO);
                close(out);
            }
            for (int j = 0; j < 2 * (num_cmds - 1); j++) {
                close(pipefds[j]);
            }
            execvp(c_args[0], const_cast<char *const *>(c_args.data()));
            string err_msg = string(c_args[0]) + ": " + strerror(errno) + "\n";
            if (write(STDERR_FILENO, err_msg.c_str(), err_msg.size())) {}
            exit(127);
        }
    }

    for (int j = 0; j < 2 * (num_cmds - 1); j++) {
        close(pipefds[j]);
    }

    int last_status = 0;
    for (int i = 0; i < num_cmds; i++) {
        int status;
        waitpid(pids[i], &status, 0);
        if (i == num_cmds - 1) {
            if (WIFEXITED(status)) last_status = WEXITSTATUS(status);
            else if (WIFSIGNALED(status)) last_status = 128 + WTERMSIG(status);
        }
    }
    return last_status;
}
