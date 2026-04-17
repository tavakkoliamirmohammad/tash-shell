// Directory navigation builtins: cd, pwd, pushd, popd, dirs, z.

#include "tash/builtins.h"
#include "tash/core.h"
#include "tash/history.h"

#include <cstring>

using namespace std;

int builtin_cd(const vector<string> &argv, ShellState &state) {
    char cwd[MAX_SIZE];
    const char *target = nullptr;

    if (argv.size() > 1) {
        if (argv[1] == "-") {
            if (state.previous_directory.empty()) {
                write_stderr("cd: OLDPWD not set\n");
                return 1;
            }
            target = state.previous_directory.c_str();
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

    if (getcwd(cwd, MAX_SIZE) == nullptr) {
        write_stderr("cd: " + string(strerror(errno)) + "\n");
        return 1;
    }

    if (chdir(target) == -1) {
        write_stderr("cd: " + string(target) + ": " + strerror(errno) + "\n");
        return 1;
    }

    state.previous_directory = string(cwd);

    char new_cwd[MAX_SIZE];
    if (getcwd(new_cwd, MAX_SIZE) != nullptr) {
        z_record_directory(string(new_cwd));
        if (argv.size() > 1 && argv[1] == "-") {
            write_stdout(string(new_cwd) + "\n");
        }
    }
    return 0;
}

int builtin_pwd(const vector<string> &, ShellState &) {
    char temp[MAX_SIZE];
    if (getcwd(temp, MAX_SIZE) != nullptr) {
        write_stdout(string(temp) + "\n");
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
    char cwd[MAX_SIZE];
    if (getcwd(cwd, MAX_SIZE) == nullptr) {
        write_stderr("pushd: cannot get current directory\n");
        return 1;
    }
    if (chdir(argv[1].c_str()) == -1) {
        write_stderr("pushd: " + argv[1] + ": " + strerror(errno) + "\n");
        return 1;
    }
    state.dir_stack.push_back(string(cwd));
    char new_cwd[MAX_SIZE];
    if (getcwd(new_cwd, MAX_SIZE) != nullptr) {
        string stack_str = string(new_cwd);
        for (int si = (int)state.dir_stack.size() - 1; si >= 0; si--)
            stack_str += " " + state.dir_stack[si];
        write_stdout(stack_str + "\n");
    }
    return 0;
}

int builtin_popd(const vector<string> &, ShellState &state) {
    if (state.dir_stack.empty()) {
        write_stderr("popd: directory stack empty\n");
        return 1;
    }
    string target = state.dir_stack.back();
    state.dir_stack.pop_back();
    if (chdir(target.c_str()) == -1) {
        write_stderr("popd: " + target + ": " + string(strerror(errno)) + "\n");
        return 1;
    }
    char new_cwd[MAX_SIZE];
    if (getcwd(new_cwd, MAX_SIZE) != nullptr) {
        string stack_str = string(new_cwd);
        for (int si = (int)state.dir_stack.size() - 1; si >= 0; si--)
            stack_str += " " + state.dir_stack[si];
        write_stdout(stack_str + "\n");
    }
    return 0;
}

int builtin_dirs(const vector<string> &, ShellState &state) {
    char cwd[MAX_SIZE];
    if (getcwd(cwd, MAX_SIZE) != nullptr) {
        string stack_str = string(cwd);
        for (int si = (int)state.dir_stack.size() - 1; si >= 0; si--)
            stack_str += " " + state.dir_stack[si];
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

    char cwd[MAX_SIZE];
    if (getcwd(cwd, MAX_SIZE) == nullptr) {
        write_stderr("z: cannot get current directory\n");
        return 1;
    }
    if (chdir(target.c_str()) == -1) {
        write_stderr("z: " + target + ": " + strerror(errno) + "\n");
        return 1;
    }
    state.previous_directory = string(cwd);
    z_record_directory(target);
    write_stdout(target + "\n");
    return 0;
}
