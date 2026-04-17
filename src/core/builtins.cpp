// Builtin registry: maps command names to their implementation
// functions. Individual builtins live under src/builtins/*.cpp and are
// declared in include/tash/builtins.h. Adding a new builtin means
// writing the function in the appropriate group file and adding one
// line to the map below.

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
    };
    return builtins;
}

bool is_builtin(const string &name) {
    return get_builtins().count(name) > 0;
}
