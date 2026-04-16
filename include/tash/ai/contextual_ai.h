#ifndef TASH_CONTEXTUAL_AI_H
#define TASH_CONTEXTUAL_AI_H

#ifdef TASH_AI_ENABLED

#include "tash/shell.h"
#include <string>
#include <vector>

// ── Contextual AI: ? suffix operator ─────────────────────────
//
// Detects natural-language questions typed directly in the shell
// (e.g. "how to compress files?") and routes them to the AI.

// ── Question detection ───────────────────────────────────────

// Returns true when the input looks like a natural-language question:
//   1. Ends with '?'
//   2. First word is NOT a known shell command (builtin or in PATH)
//   3. Contains at least one space (reject bare "test?")
// Edge cases handled:
//   "test -f file?"   -> false  (first word is a valid command)
//   "echo 'what?'"    -> false  (? is inside quotes)
bool is_ai_question(const std::string &input);

// ── Context structures ───────────────────────────────────────

struct AiContext {
    std::string directory;
    std::vector<std::string> recent_commands; // last 5
    std::string git_branch;                   // current branch or empty
    std::string project_type;                 // detected from files in cwd
};

AiContext build_context(const ShellState &state);

// ── Project type detection ───────────────────────────────────

// Inspects the given directory for well-known marker files and returns
// a human-readable project type string, or empty if none detected.
//   CMakeLists.txt      -> "C++ (CMake)"
//   Cargo.toml          -> "Rust"
//   package.json        -> "Node.js"
//   requirements.txt    -> "Python"
//   pyproject.toml      -> "Python"
//   go.mod              -> "Go"
//   Makefile            -> "Make project"
//   pom.xml             -> "Java (Maven)"
//   build.gradle        -> "Java/Kotlin (Gradle)"
std::string detect_project_type(const std::string &directory);

// ── Git branch detection ─────────────────────────────────────

// Reads .git/HEAD in the current working directory and parses
// "ref: refs/heads/<branch>".  Returns the branch name or "".
std::string ai_get_git_branch();

// Overload that reads from an explicit .git/HEAD path (for testing).
std::string ai_get_git_branch(const std::string &git_head_path);

#endif // TASH_AI_ENABLED
#endif // TASH_CONTEXTUAL_AI_H
