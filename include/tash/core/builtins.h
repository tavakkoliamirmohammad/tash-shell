#ifndef TASH_CORE_BUILTINS_H
#define TASH_CORE_BUILTINS_H

// Builtin registry surface. The per-builtin function declarations live
// in `tash/builtins.h`; this header only exposes the registry lookup
// helpers that the executor uses to decide whether a command is a
// builtin. Split out of the old mega-header tash/core.h.

#include "tash/shell.h"

#include <string>
#include <unordered_map>

const std::unordered_map<std::string, BuiltinFn>& get_builtins();
bool is_builtin(const std::string &name);

#endif // TASH_CORE_BUILTINS_H
