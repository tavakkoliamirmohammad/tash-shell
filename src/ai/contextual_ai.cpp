#ifdef TASH_AI_ENABLED

#include "tash/ai/contextual_ai.h"
#include "tash/core/builtins.h"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

using namespace std;

// ── Helpers ──────────────────────────────────────────────────

// Check whether the given name is found in $PATH as an executable.
static bool is_in_path(const string &name) {
    if (name.empty()) return false;

    const char *path_env = getenv("PATH");
    if (!path_env) return false;

    string path_str(path_env);
    istringstream ss(path_str);
    string dir;

    while (getline(ss, dir, ':')) {
        if (dir.empty()) continue;
        string full = dir + "/" + name;
        if (access(full.c_str(), X_OK) == 0) {
            return true;
        }
    }
    return false;
}

// Check whether a character at a given position in the string is inside
// single or double quotes.
static bool is_inside_quotes(const string &input, size_t pos) {
    bool in_single = false;
    bool in_double = false;

    for (size_t i = 0; i < pos && i < input.size(); ++i) {
        char c = input[i];
        if (c == '\'' && !in_double) {
            in_single = !in_single;
        } else if (c == '"' && !in_single) {
            in_double = !in_double;
        } else if (c == '\\' && in_double && i + 1 < input.size()) {
            ++i; // skip escaped character
        }
    }

    return in_single || in_double;
}

// Extract the first word from trimmed input.
static string first_word(const string &input) {
    size_t start = 0;
    while (start < input.size() && (input[start] == ' ' || input[start] == '\t'))
        ++start;

    size_t end = start;
    while (end < input.size() && input[end] != ' ' && input[end] != '\t')
        ++end;

    return input.substr(start, end - start);
}

// Check whether input contains at least one space (outside of leading/trailing whitespace).
// More precisely: the trimmed input must contain at least one space.
static bool has_space(const string &input) {
    // Trim
    size_t start = 0;
    while (start < input.size() && (input[start] == ' ' || input[start] == '\t'))
        ++start;
    size_t end = input.size();
    while (end > start && (input[end - 1] == ' ' || input[end - 1] == '\t'))
        --end;

    for (size_t i = start; i < end; ++i) {
        if (input[i] == ' ' || input[i] == '\t')
            return true;
    }
    return false;
}

// Check if a file exists.
static bool file_exists(const string &path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

// ── is_ai_question ───────────────────────────────────────────

bool is_ai_question(const string &input) {
    // Trim the input
    string trimmed = input;
    while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t'))
        trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t'))
        trimmed.pop_back();

    // Must be non-empty and end with '?'
    if (trimmed.empty() || trimmed.back() != '?')
        return false;

    // The trailing '?' must NOT be inside quotes
    // Find the position of the last '?' in the original (trimmed) string
    size_t q_pos = trimmed.size() - 1;
    if (is_inside_quotes(trimmed, q_pos))
        return false;

    // Must contain at least one space (natural language, not "test?")
    if (!has_space(trimmed))
        return false;

    // Extract first word and check if it's a known command
    string cmd = first_word(trimmed);
    // Strip trailing '?' from single-word edge cases (shouldn't reach here
    // due to space check, but be defensive)
    if (!cmd.empty() && cmd.back() == '?')
        cmd.pop_back();

    if (cmd.empty())
        return false;

    // If the first word is a builtin or found in PATH, it's a shell command
    if (is_builtin(cmd) || is_in_path(cmd))
        return false;

    return true;
}

// ── Project type detection ───────────────────────────────────

std::string detect_project_type(const string &directory) {
    if (directory.empty())
        return "";

    // Check marker files in priority order (most specific first)
    if (file_exists(directory + "/CMakeLists.txt"))
        return "C++ (CMake)";
    if (file_exists(directory + "/Cargo.toml"))
        return "Rust";
    if (file_exists(directory + "/package.json"))
        return "Node.js";
    if (file_exists(directory + "/pyproject.toml"))
        return "Python";
    if (file_exists(directory + "/requirements.txt"))
        return "Python";
    if (file_exists(directory + "/go.mod"))
        return "Go";
    if (file_exists(directory + "/pom.xml"))
        return "Java (Maven)";
    if (file_exists(directory + "/build.gradle"))
        return "Java/Kotlin (Gradle)";
    if (file_exists(directory + "/Makefile"))
        return "Make project";

    return "";
}

// ── Git branch detection ─────────────────────────────────────

std::string ai_get_git_branch() {
    return ai_get_git_branch(".git/HEAD");
}

std::string ai_get_git_branch(const string &git_head_path) {
    ifstream f(git_head_path);
    if (!f.is_open())
        return "";

    string line;
    if (!getline(f, line))
        return "";

    // Expected format: "ref: refs/heads/<branch>"
    const string prefix = "ref: refs/heads/";
    if (line.size() > prefix.size() && line.substr(0, prefix.size()) == prefix) {
        string branch = line.substr(prefix.size());
        // Trim trailing whitespace
        while (!branch.empty() && (branch.back() == '\n' || branch.back() == '\r' || branch.back() == ' '))
            branch.pop_back();
        return branch;
    }

    // Detached HEAD or unexpected format
    return "";
}

// ── Context building ─────────────────────────────────────────

AiContext build_context(const ShellState &state) {
    AiContext ctx;

    // Current directory
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd))) {
        ctx.directory = string(cwd);
    }

    // Recent commands: last 5 from the state's command history
    // ShellState doesn't have a command history vector, so we use
    // what's available: last_executed_cmd plus last_command_text.
    // For now, store what we have (the caller can extend this).
    if (!state.ai.last_executed_cmd.empty()) {
        ctx.recent_commands.push_back(state.ai.last_executed_cmd);
    }
    if (!state.ai.last_command_text.empty() &&
        state.ai.last_command_text != state.ai.last_executed_cmd) {
        ctx.recent_commands.push_back(state.ai.last_command_text);
    }

    // Git branch
    ctx.git_branch = ai_get_git_branch();

    // Project type
    ctx.project_type = detect_project_type(ctx.directory);

    return ctx;
}

#endif // TASH_AI_ENABLED
