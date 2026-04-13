#ifndef TASH_HISTORY_H
#define TASH_HISTORY_H

#include <string>

#include "replxx.hxx"

// ── history.cpp ───────────────────────────────────────────────

std::string history_file_path();
bool should_record_history(const std::string &line, replxx::Replxx &rx);

// ── frecency.cpp ──────────────────────────────────────────────

void z_record_directory(const std::string &dir);
std::string z_find_directory(const std::string &query);

#endif // TASH_HISTORY_H
