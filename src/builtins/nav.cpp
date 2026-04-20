// Directory navigation builtins: cd, pwd, pushd, popd, dirs, z.

#include "tash/builtins.h"
#include "tash/core/signals.h"
#include "tash/history.h"
#include "tash/util/cwd.h"

#include <cstring>

using namespace std;

// Snapshot the cwd for our bookkeeping side — fail cleanly if the
// directory has been removed underneath us. Returns "" iff we should
// abort with an error.
static string cwd_or_fail(const char *builtin_name) {
    string cwd = tash::util::current_working_directory();
    if (cwd.empty()) {
        write_stderr(string(builtin_name) + ": cannot get current directory: "
                     + strerror(errno) + "\n");
    }
    return cwd;
}

int builtin_cd(const vector<string> &argv, ShellState &state) {
    const char *target = nullptr;

    if (argv.size() > 1) {
        if (argv[1] == "-") {
            if (state.core.previous_directory.empty()) {
                write_stderr("cd: OLDPWD not set\n");
                return 1;
            }
            target = state.core.previous_directory.c_str();
        } else {
            target = argv[1].c_str();
        }
    } else {
        target = getenv("HOME");
        if (!target) {
            write_stderr("cd: HOME not set\n");
            return 1;
        }
    }

    string cwd = cwd_or_fail("cd");
    if (cwd.empty()) return 1;

    if (chdir(target) == -1) {
        write_stderr("cd: " + string(target) + ": " + strerror(errno) + "\n");
        return 1;
    }

    state.core.previous_directory = std::move(cwd);

    string new_cwd = tash::util::current_working_directory();
    if (!new_cwd.empty()) {
        z_record_directory(new_cwd);
        if (argv.size() > 1 && argv[1] == "-") {
            write_stdout(new_cwd + "\n");
        }
    }
    return 0;
}

int builtin_pwd(const vector<string> &, ShellState &) {
    string cwd = tash::util::current_working_directory();
    if (!cwd.empty()) {
        write_stdout(cwd + "\n");
        return 0;
    }
    write_stderr("pwd: " + string(strerror(errno)) + "\n");
    return 1;
}

int builtin_pushd(const vector<string> &argv, ShellState &state) {
    if (argv.size() < 2) {
        write_stderr("pushd: no directory specified\n");
        return 1;
    }
    string cwd = cwd_or_fail("pushd");
    if (cwd.empty()) return 1;
    if (chdir(argv[1].c_str()) == -1) {
        write_stderr("pushd: " + argv[1] + ": " + strerror(errno) + "\n");
        return 1;
    }
    state.core.dir_stack.push_back(std::move(cwd));
    string new_cwd = tash::util::current_working_directory();
    if (!new_cwd.empty()) {
        string stack_str = new_cwd;
        for (int si = (int)state.core.dir_stack.size() - 1; si >= 0; si--)
            stack_str += " " + state.core.dir_stack[si];
        write_stdout(stack_str + "\n");
    }
    return 0;
}

int builtin_popd(const vector<string> &, ShellState &state) {
    if (state.core.dir_stack.empty()) {
        write_stderr("popd: directory stack empty\n");
        return 1;
    }
    string target = state.core.dir_stack.back();
    state.core.dir_stack.pop_back();
    if (chdir(target.c_str()) == -1) {
        write_stderr("popd: " + target + ": " + string(strerror(errno)) + "\n");
        return 1;
    }
    string new_cwd = tash::util::current_working_directory();
    if (!new_cwd.empty()) {
        string stack_str = new_cwd;
        for (int si = (int)state.core.dir_stack.size() - 1; si >= 0; si--)
            stack_str += " " + state.core.dir_stack[si];
        write_stdout(stack_str + "\n");
    }
    return 0;
}

int builtin_dirs(const vector<string> &, ShellState &state) {
    string cwd = tash::util::current_working_directory();
    if (!cwd.empty()) {
        string stack_str = cwd;
        for (int si = (int)state.core.dir_stack.size() - 1; si >= 0; si--)
            stack_str += " " + state.core.dir_stack[si];
        write_stdout(stack_str + "\n");
    }
    return 0;
}

int builtin_z(const vector<string> &argv, ShellState &state) {
    if (argv.size() < 2) {
        write_stderr("z: missing directory pattern\n");
        return 1;
    }
    string query;
    for (size_t i = 1; i < argv.size(); i++) {
        if (i > 1) query += " ";
        query += argv[i];
    }
    string target = z_find_directory(query);
    if (target.empty()) {
        write_stderr("z: no match for '" + query + "'\n");
        return 1;
    }

    string cwd = cwd_or_fail("z");
    if (cwd.empty()) return 1;
    if (chdir(target.c_str()) == -1) {
        write_stderr("z: " + target + ": " + strerror(errno) + "\n");
        return 1;
    }
    state.core.previous_directory = std::move(cwd);
    z_record_directory(target);
    write_stdout(target + "\n");
    return 0;
}
