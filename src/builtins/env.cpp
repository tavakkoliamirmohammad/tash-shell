// Environment + alias builtins: export, unset, alias, unalias.

#include "tash/builtins.h"
#include "tash/core.h"

using namespace std;

extern char **environ;

int builtin_export(const vector<string> &argv, ShellState &) {
    if (argv.size() < 2) {
        for (char **env = environ; *env != nullptr; env++)
            write_stdout(string(*env) + "\n");
    } else {
        string arg = argv[1];
        size_t eq_pos = arg.find('=');
        if (eq_pos != string::npos) {
            setenv(arg.substr(0, eq_pos).c_str(), arg.substr(eq_pos + 1).c_str(), 1);
        } else {
            write_stderr("export: invalid format. Usage: export VAR=value\n");
            return 1;
        }
    }
    return 0;
}

int builtin_unset(const vector<string> &argv, ShellState &) {
    if (argv.size() >= 2) {
        unsetenv(argv[1].c_str());
    } else {
        write_stderr("unset: missing variable name\n");
        return 1;
    }
    return 0;
}

int builtin_alias(const vector<string> &argv, ShellState &state) {
    if (argv.size() < 2) {
        for (auto &pair : state.aliases) {
            write_stdout("alias " + pair.first + "='" + pair.second + "'\n");
        }
    } else {
        string arg = argv[1];
        size_t eq_pos = arg.find('=');
        if (eq_pos != string::npos) {
            string name = arg.substr(0, eq_pos);
            string value = arg.substr(eq_pos + 1);
            if (value.size() >= 2 &&
                ((value.front() == '\'' && value.back() == '\'') ||
                 (value.front() == '"' && value.back() == '"'))) {
                value = value.substr(1, value.size() - 2);
            }
            state.aliases[name] = value;
        } else {
            if (state.aliases.count(arg)) {
                write_stdout("alias " + arg + "='" + state.aliases[arg] + "'\n");
            } else {
                write_stderr("alias: " + arg + ": not found\n");
                return 1;
            }
        }
    }
    return 0;
}

int builtin_unalias(const vector<string> &argv, ShellState &state) {
    if (argv.size() >= 2) {
        if (state.aliases.count(argv[1])) {
            state.aliases.erase(argv[1]);
        } else {
            write_stderr("unalias: " + argv[1] + ": not found\n");
            return 1;
        }
    } else {
        write_stderr("unalias: missing alias name\n");
        return 1;
    }
    return 0;
}
