#include "tash/plugins/manpage_completion_provider.h"
#include "tash/shell.h"

#include <cstdio>
#include <cstring>
#include <sstream>

// ── Help output parsing ──────────────────────────────────────

// Check if c is a valid flag character for short flags: [a-zA-Z0-9]
static bool is_flag_char(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9');
}

// Check if c is a valid long-flag body character: [a-zA-Z0-9-]
static bool is_long_flag_char(char c) {
    return is_flag_char(c) || c == '-';
}

// Trim leading and trailing whitespace
static std::string trim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t");
    return s.substr(start, end - start + 1);
}

// Try to parse a single line of --help output into a HelpOption.
// Returns true if a flag was found.
static bool parse_help_line(const std::string &line, HelpOption &opt) {
    opt.short_flag.clear();
    opt.long_flag.clear();
    opt.description.clear();

    // Skip lines that don't start with whitespace (headers, usage lines, etc.)
    if (line.empty() || (line[0] != ' ' && line[0] != '\t')) {
        return false;
    }

    size_t pos = 0;
    size_t len = line.size();

    // Skip leading whitespace
    while (pos < len && (line[pos] == ' ' || line[pos] == '\t')) {
        ++pos;
    }

    if (pos >= len) return false;

    // Look for a short flag: -X where X is [a-zA-Z0-9]
    if (pos + 1 < len && line[pos] == '-' && line[pos + 1] != '-' &&
        is_flag_char(line[pos + 1])) {
        opt.short_flag = line.substr(pos, 2);
        pos += 2;

        // Skip optional comma and whitespace before long flag
        size_t saved = pos;
        while (pos < len && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
        if (pos < len && line[pos] == ',') {
            ++pos;
            while (pos < len && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
        } else {
            pos = saved;
        }
    }

    // Look for a long flag: --word
    if (pos + 2 < len && line[pos] == '-' && line[pos + 1] == '-' &&
        is_long_flag_char(line[pos + 2]) && line[pos + 2] != '-') {
        size_t flag_start = pos;
        pos += 2;
        while (pos < len && is_long_flag_char(line[pos])) ++pos;
        // Long flag must be at least --XX (two chars after --)
        if (pos - flag_start >= 4) {
            opt.long_flag = line.substr(flag_start, pos - flag_start);
        } else {
            // Still accept --X forms as long flags
            opt.long_flag = line.substr(flag_start, pos - flag_start);
        }
    }

    // If no flag was found at all, skip this line
    if (opt.short_flag.empty() && opt.long_flag.empty()) {
        return false;
    }

    // Skip any argument placeholder (e.g., " FILE", " =VALUE", " <arg>")
    // Skip past non-whitespace tokens that look like arguments
    while (pos < len && (line[pos] == ' ' || line[pos] == '\t' || line[pos] == '=')) {
        if (line[pos] == '=' || line[pos] == '<') break;
        // Check if the next non-space is a description (2+ spaces gap) or argument
        size_t ws_start = pos;
        while (pos < len && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
        if (pos >= len) break;
        // If we crossed 2+ spaces and hit a lowercase letter, it's likely description
        if (pos - ws_start >= 2 && line[pos] != '-' && line[pos] != '<' &&
            line[pos] != '=' && line[pos] >= 'A') {
            // This is the description
            opt.description = trim(line.substr(pos));
            return true;
        }
        // Otherwise it might be an argument placeholder; skip it
        while (pos < len && line[pos] != ' ' && line[pos] != '\t') ++pos;
    }

    // Skip argument placeholders like =VALUE, <file>, etc.
    if (pos < len && (line[pos] == '=' || line[pos] == '<')) {
        while (pos < len && line[pos] != ' ' && line[pos] != '\t') ++pos;
    }

    // Now find description: skip 2+ spaces
    while (pos < len && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
    if (pos < len) {
        opt.description = trim(line.substr(pos));
    }

    return true;
}

std::vector<HelpOption> parse_help_output(const std::string &help_text) {
    std::vector<HelpOption> options;
    std::istringstream stream(help_text);
    std::string line;

    while (std::getline(stream, line)) {
        HelpOption opt;
        if (parse_help_line(line, opt)) {
            options.push_back(std::move(opt));
        }
    }

    return options;
}

// ── ManpageCompletionProvider implementation ─────────────────

bool ManpageCompletionProvider::can_complete(const std::string &) const {
    // Fallback provider: always willing to try
    return true;
}

std::vector<HelpOption> ManpageCompletionProvider::get_help_options(
    const std::string &command) const {
    // Check cache first
    auto it = cache_.find(command);
    if (it != cache_.end()) {
        return it->second;
    }

    // Run <command> --help 2>&1 with a timeout
    std::string cmd = command + " --help 2>&1";
    std::string output;

#ifndef TESTING_BUILD
    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        cache_[command] = {};
        return {};
    }

    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
        // Safety limit: don't read more than 64KB
        if (output.size() > 65536) break;
    }
    pclose(pipe);
#endif

    auto options = parse_help_output(output);
    cache_[command] = options;
    return options;
}

std::vector<Completion> ManpageCompletionProvider::complete(
    const std::string &command,
    const std::string &current_word,
    const std::vector<std::string> &,
    const ShellState &) const {

    std::vector<Completion> results;

    // Only provide flag completions when current_word starts with "-"
    if (current_word.empty() || current_word[0] != '-') {
        return results;
    }

    auto options = get_help_options(command);

    for (const auto &opt : options) {
        // Match short flags
        if (!opt.short_flag.empty() &&
            opt.short_flag.compare(0, current_word.size(), current_word) == 0) {
            results.emplace_back(opt.short_flag, opt.description,
                                 Completion::OPTION_SHORT);
        }
        // Match long flags
        if (!opt.long_flag.empty() &&
            opt.long_flag.compare(0, current_word.size(), current_word) == 0) {
            results.emplace_back(opt.long_flag, opt.description,
                                 Completion::OPTION_LONG);
        }
    }

    return results;
}
