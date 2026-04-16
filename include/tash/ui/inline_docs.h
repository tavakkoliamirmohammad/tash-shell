#ifndef TASH_UI_INLINE_DOCS_H
#define TASH_UI_INLINE_DOCS_H

#include <string>
#include <vector>
#include <unordered_map>

// ── Inline documentation engine ──────────────────────────────
//
// Provides real-time command explanations and flag descriptions
// from a built-in knowledge base. No external dependencies.

// ── Types ────────────────────────────────────────────────────

struct FlagExplanation {
    std::string flag;        // e.g., "-x"
    std::string description; // e.g., "Extract files from archive"
};

// ── Public API ───────────────────────────────────────────────

// Returns explanations for each recognized flag/argument.
// Unknown flags get an empty description. Unknown commands
// return an empty list.
std::vector<FlagExplanation> explain_command(
    const std::string &command,
    const std::vector<std::string> &args);

// Returns a one-line description of the command, or "" if unknown.
std::string get_command_hint(const std::string &command);

// ── Database access (for testing / extension) ────────────────

const std::unordered_map<std::string,
    std::unordered_map<std::string, std::string>> &get_flag_db();

const std::unordered_map<std::string, std::string> &get_cmd_hints();

#endif // TASH_UI_INLINE_DOCS_H
