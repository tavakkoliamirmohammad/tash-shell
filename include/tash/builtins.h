// Declarations of every shell builtin implementation. The registry
// (src/core/builtins.cpp) picks these up to populate the name→function
// map; each file under src/builtins/*.cpp owns a related group.

#ifndef TASH_BUILTINS_H
#define TASH_BUILTINS_H

#include "tash/shell.h"
#include <string>
#include <vector>

// ── src/builtins/nav.cpp ───────────────────────────────────────
int builtin_cd(const std::vector<std::string> &argv, ShellState &state);
int builtin_pwd(const std::vector<std::string> &argv, ShellState &state);
int builtin_pushd(const std::vector<std::string> &argv, ShellState &state);
int builtin_popd(const std::vector<std::string> &argv, ShellState &state);
int builtin_dirs(const std::vector<std::string> &argv, ShellState &state);
int builtin_z(const std::vector<std::string> &argv, ShellState &state);

// ── src/builtins/env.cpp ───────────────────────────────────────
int builtin_export(const std::vector<std::string> &argv, ShellState &state);
int builtin_unset(const std::vector<std::string> &argv, ShellState &state);
int builtin_alias(const std::vector<std::string> &argv, ShellState &state);
int builtin_unalias(const std::vector<std::string> &argv, ShellState &state);

// ── src/builtins/bg.cpp ────────────────────────────────────────
int builtin_bglist(const std::vector<std::string> &argv, ShellState &state);
int builtin_bgkill(const std::vector<std::string> &argv, ShellState &state);
int builtin_bgstop(const std::vector<std::string> &argv, ShellState &state);
int builtin_bgstart(const std::vector<std::string> &argv, ShellState &state);
int builtin_fg(const std::vector<std::string> &argv, ShellState &state);

// ── src/builtins/history.cpp ───────────────────────────────────
int builtin_history(const std::vector<std::string> &argv, ShellState &state);

// ── src/builtins/ui.cpp ────────────────────────────────────────
int builtin_clear(const std::vector<std::string> &argv, ShellState &state);
int builtin_copy(const std::vector<std::string> &argv, ShellState &state);
int builtin_paste(const std::vector<std::string> &argv, ShellState &state);
int builtin_linkify(const std::vector<std::string> &argv, ShellState &state);
int builtin_block(const std::vector<std::string> &argv, ShellState &state);
int builtin_table(const std::vector<std::string> &argv, ShellState &state);

// ── src/builtins/meta.cpp ──────────────────────────────────────
// POSIX/shell meta: exit, source/., which/type, explain.
int builtin_exit(const std::vector<std::string> &argv, ShellState &state);
int builtin_which(const std::vector<std::string> &argv, ShellState &state);
int builtin_source(const std::vector<std::string> &argv, ShellState &state);
int builtin_explain(const std::vector<std::string> &argv, ShellState &state);

// ── src/builtins/config.cpp ────────────────────────────────────
// tash-specific config/state: config (sync), session, theme.
int builtin_config(const std::vector<std::string> &argv, ShellState &state);
int builtin_session(const std::vector<std::string> &argv, ShellState &state);
int builtin_theme(const std::vector<std::string> &argv, ShellState &state);

// ── src/builtins/trap.cpp ──────────────────────────────────────
// Signal/exit trap builtin.
int builtin_trap(const std::vector<std::string> &argv, ShellState &state);

#endif // TASH_BUILTINS_H
