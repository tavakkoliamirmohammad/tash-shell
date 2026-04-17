#include "tash/plugins/safety_hook_provider.h"
#include "tash/core.h"
#include "tash/shell.h"
#include "theme.h"
#include <algorithm>
#include <cctype>

// ── Helpers ──────────────────────────────────────────────────

namespace {

// Trim leading and trailing whitespace
std::string trim(const std::string &s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
        ++start;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
        --end;
    return s.substr(start, end - start);
}

// Check if string starts with prefix
bool starts_with(const std::string &s, const std::string &prefix) {
    return s.size() >= prefix.size() &&
           s.compare(0, prefix.size(), prefix) == 0;
}

// Check if string contains substring
bool contains(const std::string &s, const std::string &sub) {
    return s.find(sub) != std::string::npos;
}

// Check if rm flags contain both 'r' and 'f' (in any order)
// flags is the token after "rm" that starts with '-'
bool rm_flags_have_rf(const std::string &flags) {
    bool has_r = false;
    bool has_f = false;
    for (size_t i = 1; i < flags.size(); ++i) {
        if (flags[i] == 'r' || flags[i] == 'R') has_r = true;
        if (flags[i] == 'f') has_f = true;
    }
    return has_r && has_f;
}

// Check if rm flags contain 'r' (recursive)
bool rm_flags_have_r(const std::string &flags) {
    for (size_t i = 1; i < flags.size(); ++i) {
        if (flags[i] == 'r' || flags[i] == 'R') return true;
    }
    return false;
}

// Simple tokenizer: split on whitespace
std::vector<std::string> tokenize(const std::string &s) {
    std::vector<std::string> tokens;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
            ++i;
        if (i >= s.size()) break;
        size_t start = i;
        while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i])))
            ++i;
        tokens.push_back(s.substr(start, i - start));
    }
    return tokens;
}

} // anonymous namespace

// ── classify_command ─────────────────────────────────────────

RiskLevel classify_command(const std::string &cmd) {
    std::string trimmed = trim(cmd);

    if (trimmed.empty()) {
        return SAFE;
    }

    // Backslash bypass: if command starts with '\', skip all checks
    if (trimmed[0] == '\\') {
        return SAFE;
    }

    std::vector<std::string> tokens = tokenize(trimmed);
    if (tokens.empty()) {
        return SAFE;
    }

    // ── Truncation / redirect detection ──────────────────────
    // "> existing_file" -- output truncation
    if (tokens[0] == ">" || starts_with(trimmed, ">")) {
        return MEDIUM;
    }

    // ── rm patterns ──────────────────────────────────────────
    if (tokens[0] == "rm") {
        // Gather all flag tokens and find the first non-flag argument
        bool has_r = false;
        bool has_f = false;
        std::string path_arg;

        for (size_t i = 1; i < tokens.size(); ++i) {
            if (tokens[i][0] == '-') {
                // This is a flag token
                for (size_t j = 1; j < tokens[i].size(); ++j) {
                    char c = tokens[i][j];
                    if (c == 'r' || c == 'R') has_r = true;
                    if (c == 'f') has_f = true;
                }
            } else {
                // First non-flag argument is the path
                if (path_arg.empty()) {
                    path_arg = tokens[i];
                }
            }
        }

        if (has_r && has_f) {
            // rm -rf / or rm -rf /*
            if (path_arg == "/" || path_arg == "/*") {
                return BLOCKED;
            }
            // rm -rf <path> -> HIGH
            return HIGH;
        }

        if (has_r) {
            // rm -r <something> -> MEDIUM
            return MEDIUM;
        }

        // rm without recursive flags -> SAFE
        return SAFE;
    }

    // ── chmod patterns ───────────────────────────────────────
    if (tokens[0] == "chmod") {
        bool has_R = false;
        bool has_777 = false;

        for (size_t i = 1; i < tokens.size(); ++i) {
            if (tokens[i] == "-R") {
                has_R = true;
            } else if (tokens[i] == "777") {
                has_777 = true;
            }
        }

        if (has_R && has_777) {
            return HIGH;
        }
        if (has_R) {
            return MEDIUM;
        }
        return SAFE;
    }

    // ── git patterns ─────────────────────────────────────────
    if (tokens[0] == "git" && tokens.size() >= 2) {
        if (tokens[1] == "push") {
            for (size_t i = 2; i < tokens.size(); ++i) {
                if (tokens[i] == "--force" || tokens[i] == "-f") {
                    return HIGH;
                }
            }
        }
        if (tokens[1] == "reset") {
            for (size_t i = 2; i < tokens.size(); ++i) {
                if (tokens[i] == "--hard") {
                    return HIGH;
                }
            }
        }
    }

    // ── dd pattern ───────────────────────────────────────────
    if (tokens[0] == "dd") {
        for (size_t i = 1; i < tokens.size(); ++i) {
            if (starts_with(tokens[i], "if=")) {
                return HIGH;
            }
        }
    }

    // ── mkfs pattern ─────────────────────────────────────────
    if (starts_with(tokens[0], "mkfs")) {
        return HIGH;
    }

    return SAFE;
}

// ── SafetyHookProvider implementation ────────────────────────

std::string SafetyHookProvider::name() const {
    return "safety";
}

void SafetyHookProvider::on_before_command(const std::string &command,
                                           ShellState &state) {
    // Reset skip flag
    state.skip_execution = false;

    RiskLevel level = classify_command(command);

    if (level == SAFE) {
        return;
    }

    // Build the warning message based on what was detected
    std::string trimmed = trim(command);
    std::vector<std::string> tokens = tokenize(trimmed);
    std::string warning;

    if (level == BLOCKED) {
        warning = "BLOCKED: This will delete your entire filesystem. Blocked.";
        write_stderr(CAT_RED + warning + CAT_RESET + "\n");
        state.skip_execution = true;
        return;
    }

    // Build context-specific warning for HIGH/MEDIUM
    if (tokens[0] == "rm") {
        // Find path argument
        std::string path_arg;
        bool has_r = false;
        bool has_f = false;
        for (size_t i = 1; i < tokens.size(); ++i) {
            if (tokens[i][0] == '-') {
                for (size_t j = 1; j < tokens[i].size(); ++j) {
                    if (tokens[i][j] == 'r' || tokens[i][j] == 'R') has_r = true;
                    if (tokens[i][j] == 'f') has_f = true;
                }
            } else if (path_arg.empty()) {
                path_arg = tokens[i];
            }
        }
        if (has_r && has_f) {
            warning = "Recursive delete of " + path_arg + ". Proceed? [y/N]";
        } else if (has_r) {
            warning = "Recursive delete. Proceed? [y/N]";
        }
    } else if (tokens[0] == "chmod") {
        bool has_R = false;
        bool has_777 = false;
        for (size_t i = 1; i < tokens.size(); ++i) {
            if (tokens[i] == "-R") has_R = true;
            if (tokens[i] == "777") has_777 = true;
        }
        if (has_R && has_777) {
            warning = "Grants full permissions recursively. Proceed? [y/N]";
        } else if (has_R) {
            warning = "Recursive permission change. Proceed? [y/N]";
        }
    } else if (tokens[0] == "git" && tokens.size() >= 2) {
        if (tokens[1] == "push") {
            warning = "Force push overwrites remote history. Proceed? [y/N]";
        } else if (tokens[1] == "reset") {
            warning = "Discards all uncommitted changes. Proceed? [y/N]";
        }
    } else if (tokens[0] == "dd") {
        warning = "dd writes directly to device. Verify target. Proceed? [y/N]";
    } else if (starts_with(tokens[0], "mkfs")) {
        warning = "Formats a filesystem. All data lost. Proceed? [y/N]";
    } else if (tokens[0] == ">" || starts_with(trimmed, ">")) {
        warning = "Output truncation may overwrite file contents. Proceed? [y/N]";
    }

    if (warning.empty()) {
        return;
    }

    // Print warning and prompt for confirmation. Use theme-aware colors
    // so palette swaps propagate. HIGH → red, MEDIUM → yellow.
    const std::string &color = (level == HIGH) ? CAT_RED : CAT_YELLOW;
    write_stderr(color + "WARNING: " + warning + CAT_RESET + "\n");

    // Read single char from /dev/tty (terminal, not stdin which may be piped)
    FILE *tty = fopen("/dev/tty", "r");
    if (!tty) {
        // Can't read terminal -- err on the side of caution, skip
        state.skip_execution = true;
        return;
    }

    int ch = fgetc(tty);
    fclose(tty);

    if (ch != 'y' && ch != 'Y') {
        state.skip_execution = true;
    }
}

void SafetyHookProvider::on_after_command(const std::string & /*command*/,
                                          int /*exit_code*/,
                                          const std::string & /*stderr_output*/,
                                          ShellState & /*state*/) {
    // No-op: safety checks only happen before command execution
}
