// Background-job builtins: bglist, bgkill, bgstop, bgstart, fg.

#include "tash/builtins.h"
#include "tash/core.h"

#include <cstring>
#include <sstream>
#include <stdexcept>
#include <sys/wait.h>

using namespace std;

namespace {

pid_t get_nth_background_process(unordered_map<pid_t, string> &background_processes, int n) {
    int i = 1;
    for (auto &process : background_processes) {
        if (i == n) return process.first;
        ++i;
    }
    return -1;
}

int parse_job_number(const vector<string> &argv, const string &cmd_name) {
    if (argv.size() < 2) {
        write_stderr(cmd_name + ": missing process number\n");
        return -1;
    }
    try {
        return stoi(argv[1]);
    } catch (const invalid_argument&) {
        write_stderr(cmd_name + ": invalid process number\n");
        return -1;
    } catch (const out_of_range&) {
        write_stderr(cmd_name + ": process number out of range\n");
        return -1;
    }
}

} // anonymous namespace

int builtin_bglist(const vector<string> &, ShellState &state) {
    int i = 0;
    for (auto &process : state.background_processes) {
        ++i;
        stringstream ss;
        ss << "(" << i << ")" << " " << process.second << endl;
        write_stdout(ss.str());
    }
    stringstream ss;
    ss << "Total Background Jobs: " << i << endl;
    write_stdout(ss.str());
    return 0;
}

int builtin_bgkill(const vector<string> &argv, ShellState &state) {
    int n = parse_job_number(argv, "bgkill");
    if (n < 0) return 1;
    pid_t pid = get_nth_background_process(state.background_processes, n);
    if (pid == -1) { write_stderr("bgkill: invalid job number\n"); return 1; }
    if (kill(pid, SIGTERM) == -1) { write_stderr(string(strerror(errno)) + "\n"); return 1; }
    return 0;
}

int builtin_bgstop(const vector<string> &argv, ShellState &state) {
    int n = parse_job_number(argv, "bgstop");
    if (n < 0) return 1;
    pid_t pid = get_nth_background_process(state.background_processes, n);
    if (pid == -1) { write_stderr("bgstop: invalid job number\n"); return 1; }
    if (kill(pid, SIGSTOP) == -1) { write_stderr(string(strerror(errno)) + "\n"); return 1; }
    return 0;
}

int builtin_bgstart(const vector<string> &argv, ShellState &state) {
    int n = parse_job_number(argv, "bgstart");
    if (n < 0) return 1;
    pid_t pid = get_nth_background_process(state.background_processes, n);
    if (pid == -1) { write_stderr("bgstart: invalid job number\n"); return 1; }
    if (kill(pid, SIGCONT) == -1) { write_stderr(string(strerror(errno)) + "\n"); return 1; }
    return 0;
}

int builtin_fg(const vector<string> &argv, ShellState &state) {
    if (state.background_processes.empty()) {
        write_stderr("fg: no background jobs\n");
        return 1;
    }
    pid_t pid;
    if (argv.size() < 2) {
        pid = -1;
        for (auto &p : state.background_processes) {
            if (p.first > pid) pid = p.first;
        }
    } else {
        int n;
        try { n = stoi(argv[1]); }
        catch (const invalid_argument&) { write_stderr("fg: invalid job number\n"); return 1; }
        catch (const out_of_range&) { write_stderr("fg: job number out of range\n"); return 1; }
        pid = get_nth_background_process(state.background_processes, n);
        if (pid == -1) { write_stderr("fg: invalid job number\n"); return 1; }
    }
    kill(pid, SIGCONT);
    fg_child_pid = pid;
    int status;
    waitpid(pid, &status, WUNTRACED);
    fg_child_pid = 0;
    state.background_processes.erase(pid);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}
