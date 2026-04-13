#ifndef TASH_UI_H
#define TASH_UI_H

#include "tash/shell.h"

#include "replxx.hxx"

// ── prompt.cpp ─────────────────────────────────────────────────

std::string write_shell_prefix(const ShellState &state);
std::string get_git_branch();
std::string get_git_status_indicators();
void set_terminal_title(const std::string &title);

// ── completion.cpp ────────────────────────────────────────────

replxx::Replxx::completions_t completion_callback(const std::string &input, int &context_len);

// ── suggest.cpp ───────────────────────────────────────────────

void build_command_cache();
const std::vector<std::string>& get_path_commands();
std::string suggest_command(const std::string &cmd);
bool command_exists_on_path(const std::string &cmd);

// ── highlight.cpp ─────────────────────────────────────────────

void syntax_highlighter(const std::string &input, replxx::Replxx::colors_t &colors);
replxx::Replxx::hints_t hint_callback(const std::string &input, int &context_len, replxx::Replxx::Color &color);

#endif // TASH_UI_H
