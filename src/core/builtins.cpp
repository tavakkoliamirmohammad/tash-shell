// Builtin registry: when adding builtins from new files, register them
// here. See src/builtins/*.cpp for implementations grouped by theme:
//
//   nav.cpp     — cd, pwd, pushd/popd/dirs, z
//   env.cpp     — export, unset, alias, unalias
//   bg.cpp      — bglist, bgkill, bgstop, bgstart, fg
//   history.cpp — history
//   ui.cpp      — clear, copy, paste, linkify, block, table
//   meta.cpp    — exit, which/type, source/., explain
//   config.cpp  — config, session, theme
//   trap.cpp    — trap

#include "tash/builtins.h"
#include "tash/core.h"

using namespace std;

const unordered_map<string, BuiltinFn>& get_builtins() {
    static const unordered_map<string, BuiltinFn> builtins = {
        {"cd",       builtin_cd},
        {"pwd",      builtin_pwd},
        {"exit",     builtin_exit},
        {"export",   builtin_export},
        {"unset",    builtin_unset},
        {"alias",    builtin_alias},
        {"unalias",  builtin_unalias},
        {"clear",    builtin_clear},
        {"which",    builtin_which},
        {"type",     builtin_which},
        {"pushd",    builtin_pushd},
        {"popd",     builtin_popd},
        {"dirs",     builtin_dirs},
        {"bglist",   builtin_bglist},
        {"bgkill",   builtin_bgkill},
        {"bgstop",   builtin_bgstop},
        {"bgstart",  builtin_bgstart},
        {"fg",       builtin_fg},
        {"history",  builtin_history},
        {"source",   builtin_source},
        {".",        builtin_source},
        {"z",        builtin_z},
        {"theme",    builtin_theme},
        {"explain",  builtin_explain},
        {"copy",     builtin_copy},
        {"paste",    builtin_paste},
        {"linkify",  builtin_linkify},
        {"table",    builtin_table},
        {"block",    builtin_block},
        {"session",  builtin_session},
        {"config",   builtin_config},
        {"trap",     builtin_trap},
    };
    return builtins;
}

bool is_builtin(const string &name) {
    return get_builtins().count(name) > 0;
}
