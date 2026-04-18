#ifndef TASH_CORE_BUILTINS_H
#define TASH_CORE_BUILTINS_H

// Builtin registry surface. The per-builtin function declarations live
// in `tash/builtins.h`; this header only exposes the registry lookup
// helpers that the executor uses to decide whether a command is a
// builtin. Split out of the old mega-header tash/core.h.

#include "tash/shell.h"

#include <string>
#include <unordered_map>
#include <vector>

// Per-builtin metadata used by the `help` builtin and any future
// introspection. Lifetime note: `name`, `usage`, and `brief` are
// initialised from string literals in the registry table, so raw
// `const char*` is both safe and zero-alloc at startup.
struct BuiltinInfo {
    const char* name;
    BuiltinFn   fn;
    const char* usage;  // e.g. "exit [code]"
    const char* brief;  // one-line description, aim for <=70 chars
};

// Canonical list of registered builtins with their metadata. The
// name→fn map returned by get_builtins() is derived from this table,
// so adding a new builtin here is sufficient to register it.
const std::vector<BuiltinInfo>& get_builtins_info();

const std::unordered_map<std::string, BuiltinFn>& get_builtins();
bool is_builtin(const std::string &name);

#endif // TASH_CORE_BUILTINS_H
