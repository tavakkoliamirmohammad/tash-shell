// POSIX/shell meta builtins: exit, source/., which/type, explain.
//
// These are the thin glue builtins that every POSIX shell carries. Split
// out of the old src/builtins/shell.cpp blob so signal/trap logic and
// tash-specific config/theme/session live in their own files (see
// trap.cpp and config.cpp).

#include "tash/builtins.h"
#include "tash/core.h"
#include "tash/ui/inline_docs.h"

#include <unistd.h>

using namespace std;

int builtin_exit(const vector<string> &, ShellState &state) {
    // POSIX: run the EXIT trap (if any) before the shell actually exits.
    fire_exit_trap(state);
    write_stdout("GoodBye! See you soon!\n");
    exit(0);
    return 0;
}

int builtin_which(const vector<string> &argv, ShellState &state) {
    if (argv.size() < 2) {
        write_stderr(argv[0] + ": missing argument\n");
        return 1;
    }
    string name = argv[1];
    if (is_builtin(name)) {
        write_stdout(name + ": shell builtin\n");
        return 0;
    }
    if (state.aliases.count(name)) {
        write_stdout(name + " is aliased to '" + state.aliases[name] + "'\n");
        return 0;
    }
    const char *path_env = getenv("PATH");
    if (path_env) {
        string path_str = path_env;
        size_t start = 0;
        while (start < path_str.size()) {
            size_t end = path_str.find(':', start);
            if (end == string::npos) end = path_str.size();
            string dir = path_str.substr(start, end - start);
            string full_path = dir + "/" + name;
            if (access(full_path.c_str(), X_OK) == 0) {
                write_stdout(full_path + "\n");
                return 0;
            }
            start = end + 1;
        }
    }
    write_stderr(name + " not found\n");
    return 1;
}

int builtin_source(const vector<string> &argv, ShellState &state) {
    if (argv.size() < 2) {
        write_stderr("source: missing file argument\n");
        return 1;
    }
    return execute_script_file(argv[1], state);
}

int builtin_explain(const vector<string> &argv, ShellState &) {
    if (argv.size() < 2) {
        write_stderr("explain: usage: explain <command> [args...]\n");
        return 1;
    }
    const string &cmd = argv[1];
    vector<string> rest(argv.begin() + 2, argv.end());

    string hint = get_command_hint(cmd);
    if (hint.empty()) {
        write_stderr("explain: no entry for '" + cmd + "'\n");
        return 1;
    }
    write_stdout("  " + cmd + string(cmd.size() < 6 ? 6 - cmd.size() : 0, ' ') +
                 "  " + hint + "\n");

    auto flags = explain_command(cmd, rest);
    for (const auto &f : flags) {
        if (f.description.empty()) continue;
        string pad(f.flag.size() < 6 ? 6 - f.flag.size() : 0, ' ');
        write_stdout("  " + f.flag + pad + "  " + f.description + "\n");
    }
    return 0;
}
