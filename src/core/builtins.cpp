// Builtin registry: when adding builtins from new files, register them
// here. See src/builtins/*.cpp for implementations grouped by theme:
//
//   nav.cpp     — cd, pwd, pushd/popd/dirs, z
//   env.cpp     — export, unset, alias, unalias
//   bg.cpp      — bglist, bgkill, bgstop, bgstart, fg
//   history.cpp — history
//   ui.cpp      — clear, copy, paste, linkify, block, table
//   meta.cpp    — exit, which/type, source/., explain, help
//   config.cpp  — config, session, theme
//   trap.cpp    — trap
//
// Single source of truth is get_builtins_info() — the name→fn map
// returned by get_builtins() is derived from it at first call, so
// adding a builtin in exactly one place (the table below) is enough
// to register it and make it discoverable via `help`.

#include "tash/builtins.h"
#include "tash/core/builtins.h"
using namespace std;

const vector<BuiltinInfo>& get_builtins_info() {
    static const vector<BuiltinInfo> table = {
        // nav.cpp
        {"cd",       builtin_cd,       "cd [dir|-]",                  "Change the current working directory"},
        {"pwd",      builtin_pwd,      "pwd",                         "Print the current working directory"},
        {"pushd",    builtin_pushd,    "pushd <dir>",                 "Push current dir on the stack and cd to <dir>"},
        {"popd",     builtin_popd,     "popd",                        "Pop the directory stack and cd to the top"},
        {"dirs",     builtin_dirs,     "dirs",                        "Show the directory stack"},
        {"z",        builtin_z,        "z <pattern>",                 "Jump to a frecent directory matching pattern"},

        // env.cpp
        {"export",   builtin_export,   "export [VAR=value]",          "Set an environment variable, or list all"},
        {"unset",    builtin_unset,    "unset VAR",                   "Remove an environment variable"},
        {"alias",    builtin_alias,    "alias [name[='value']]",      "Define, show, or list shell aliases"},
        {"unalias",  builtin_unalias,  "unalias name",                "Remove a shell alias"},

        // bg.cpp
        {"bglist",   builtin_bglist,   "bglist",                      "List background jobs"},
        {"bgkill",   builtin_bgkill,   "bgkill <n>",                  "Send SIGTERM to background job #n"},
        {"bgstop",   builtin_bgstop,   "bgstop <n>",                  "Send SIGSTOP to background job #n"},
        {"bgstart",  builtin_bgstart,  "bgstart <n>",                 "Send SIGCONT to background job #n"},
        {"fg",       builtin_fg,       "fg [n]",                      "Bring background job to the foreground"},

        // history.cpp
        {"history",  builtin_history,  "history",                     "Show command history"},

        // ui.cpp
        {"clear",    builtin_clear,    "clear",                       "Clear the terminal screen"},
        {"copy",     builtin_copy,     "copy [text...]",              "Copy arguments or stdin to the clipboard"},
        {"paste",    builtin_paste,    "paste",                       "Print the clipboard contents to stdout"},
        {"linkify",  builtin_linkify,  "linkify [text...]",           "Turn URLs in text/stdin into OSC-8 hyperlinks"},
        {"block",    builtin_block,    "block <command> [args...]",   "Run a command wrapped in a header/footer block"},
        {"table",    builtin_table,    "table [--max-width N]",       "Render columnar stdin as a tidy table"},

        // meta.cpp
        {"exit",     builtin_exit,     "exit [code]",                 "Exit the shell (runs EXIT trap)"},
        {"which",    builtin_which,    "which <name>",                "Resolve a command via aliases, builtins, or $PATH"},
        {"type",     builtin_which,    "type <name>",                 "Alias for `which`: classify a command name"},
        {"source",   builtin_source,   "source <file>",               "Read and execute commands from file in current shell"},
        {".",        builtin_source,   ". <file>",                    "POSIX synonym for `source`"},
        {"explain",  builtin_explain,  "explain <cmd> [args...]",     "Describe a command and its flags inline"},
        {"help",     builtin_help,     "help [name]",                 "List builtins, or show usage for a specific builtin"},

        // config.cpp
        {"config",   builtin_config,   "config <subcommand>",         "Inspect or modify tash configuration (sync)"},
        {"session",  builtin_session,  "session <list|save|load|rm>", "Save or restore shell session state"},
        {"theme",    builtin_theme,    "theme <list|current|set|preview>", "List, switch, or preview color themes"},

        // trap.cpp
        {"trap",     builtin_trap,     "trap [cmd] [signals...]",     "Register a command to run on signal receipt"},
    };
    return table;
}

const unordered_map<string, BuiltinFn>& get_builtins() {
    static const unordered_map<string, BuiltinFn> builtins = []() {
        unordered_map<string, BuiltinFn> m;
        for (const auto &b : get_builtins_info()) {
            m.emplace(b.name, b.fn);
        }
        return m;
    }();
    return builtins;
}

bool is_builtin(const string &name) {
    return get_builtins().count(name) > 0;
}
