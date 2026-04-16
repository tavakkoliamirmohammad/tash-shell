#include "tash/plugins/fish_completion_provider.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <dirent.h>

// ── Utility: strip quotes from a string ──────────────────────

static std::string strip_quotes(const std::string &s) {
    if (s.size() >= 2) {
        if ((s.front() == '\'' && s.back() == '\'') ||
            (s.front() == '"' && s.back() == '"')) {
            return s.substr(1, s.size() - 2);
        }
    }
    return s;
}

// ── Utility: tokenize a line respecting quotes ───────────────

static std::vector<std::string> tokenize_line(const std::string &line) {
    std::vector<std::string> tokens;
    std::string current;
    bool in_single = false;
    bool in_double = false;
    bool escaped = false;

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];

        if (escaped) {
            current += c;
            escaped = false;
            continue;
        }

        if (c == '\\' && !in_single) {
            escaped = true;
            current += c;
            continue;
        }

        if (c == '\'' && !in_double) {
            in_single = !in_single;
            current += c;
            continue;
        }

        if (c == '"' && !in_single) {
            in_double = !in_double;
            current += c;
            continue;
        }

        if ((c == ' ' || c == '\t') && !in_single && !in_double) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            continue;
        }

        current += c;
    }

    if (!current.empty()) {
        tokens.push_back(current);
    }

    return tokens;
}

// ── Parse a single fish complete line ────────────────────────

bool parse_fish_complete_line(const std::string &line,
                              FishCompletionEntry &entry) {
    // Skip empty lines, comments, and lines that don't start with "complete"
    if (line.empty()) return false;

    // Trim leading whitespace
    size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos) return false;

    std::string trimmed = line.substr(start);
    if (trimmed.empty() || trimmed[0] == '#') return false;

    // Must start with "complete"
    if (trimmed.compare(0, 8, "complete") != 0) return false;
    if (trimmed.size() > 8 && trimmed[8] != ' ' && trimmed[8] != '\t')
        return false;

    std::vector<std::string> tokens = tokenize_line(trimmed);
    if (tokens.empty() || tokens[0] != "complete") return false;

    entry = FishCompletionEntry();

    for (size_t i = 1; i < tokens.size(); ++i) {
        const std::string &tok = tokens[i];

        if (tok == "-c" || tok == "--command") {
            if (i + 1 < tokens.size()) {
                entry.command = strip_quotes(tokens[++i]);
            }
        } else if (tok == "-s" || tok == "--short-option") {
            if (i + 1 < tokens.size()) {
                entry.short_opt = strip_quotes(tokens[++i]);
            }
        } else if (tok == "-l" || tok == "--long-option") {
            if (i + 1 < tokens.size()) {
                entry.long_opt = strip_quotes(tokens[++i]);
            }
        } else if (tok == "-d" || tok == "--description") {
            if (i + 1 < tokens.size()) {
                entry.description = strip_quotes(tokens[++i]);
            }
        } else if (tok == "-a" || tok == "--arguments") {
            if (i + 1 < tokens.size()) {
                entry.arguments = strip_quotes(tokens[++i]);
            }
        } else if (tok == "-f" || tok == "--no-files") {
            entry.no_files = true;
        } else if (tok == "-r" || tok == "--require-parameter") {
            entry.requires_arg = true;
        } else if (tok == "-x" || tok == "--exclusive") {
            // -x is shorthand for -f -r
            entry.no_files = true;
            entry.requires_arg = true;
        } else if (tok == "-n" || tok == "--condition") {
            // Skip condition and its argument
            if (i + 1 < tokens.size()) {
                ++i;
            }
        } else if (tok == "-w" || tok == "--wraps") {
            // Skip wraps and its argument
            if (i + 1 < tokens.size()) {
                ++i;
            }
        }
    }

    // Must have a command name to be useful
    return !entry.command.empty();
}

// ── Convert fish entries to Completion objects ───────────────

std::vector<Completion> fish_entries_to_completions(
    const std::vector<FishCompletionEntry> &entries,
    const std::string &prefix) {

    std::vector<Completion> completions;

    for (const auto &e : entries) {
        // Short option: -X
        if (!e.short_opt.empty()) {
            std::string opt = "-" + e.short_opt;
            if (prefix.empty() ||
                opt.compare(0, prefix.size(), prefix) == 0) {
                completions.emplace_back(
                    opt, e.description, Completion::OPTION_SHORT);
            }
        }

        // Long option: --foo
        if (!e.long_opt.empty()) {
            std::string opt = "--" + e.long_opt;
            if (prefix.empty() ||
                opt.compare(0, prefix.size(), prefix) == 0) {
                completions.emplace_back(
                    opt, e.description, Completion::OPTION_LONG);
            }
        }

        // Arguments: space-separated words in -a value
        if (!e.arguments.empty() && e.short_opt.empty() &&
            e.long_opt.empty()) {
            std::istringstream iss(e.arguments);
            std::string arg;
            while (iss >> arg) {
                if (prefix.empty() ||
                    arg.compare(0, prefix.size(), prefix) == 0) {
                    completions.emplace_back(
                        arg, e.description, Completion::SUBCOMMAND);
                }
            }
        }
    }

    return completions;
}

// ── Expand ~ in paths ────────────────────────────────────────

static std::string expand_home(const std::string &path) {
    if (!path.empty() && path[0] == '~') {
        const char *home = std::getenv("HOME");
        if (home) {
            return std::string(home) + path.substr(1);
        }
    }
    return path;
}

// ── Default search directories ───────────────────────────────

static std::vector<std::string> default_search_dirs() {
    return {
        "/usr/share/fish/completions/",
        "/usr/local/share/fish/completions/",
        "/opt/homebrew/share/fish/completions/",
        "~/.config/fish/completions/"
    };
}

// ── Constructor (default dirs) ───────────────────────────────

FishCompletionProvider::FishCompletionProvider()
    : search_dirs_(default_search_dirs()) {
    build_index();
}

// ── Constructor (explicit dirs) ──────────────────────────────

FishCompletionProvider::FishCompletionProvider(
    const std::vector<std::string> &search_dirs)
    : search_dirs_(search_dirs) {
    build_index();
}

// ── Build filename index ─────────────────────────────────────

void FishCompletionProvider::build_index() {
    for (const auto &dir_raw : search_dirs_) {
        std::string dir = expand_home(dir_raw);

        DIR *dp = opendir(dir.c_str());
        if (!dp) continue;

        struct dirent *entry;
        while ((entry = readdir(dp)) != nullptr) {
            std::string filename = entry->d_name;

            // Must end with .fish
            if (filename.size() < 6) continue;
            if (filename.compare(filename.size() - 5, 5, ".fish") != 0)
                continue;

            // Command name is filename without .fish extension
            std::string cmd = filename.substr(0, filename.size() - 5);

            // Only index if not already indexed (first dir wins)
            if (command_index_.find(cmd) == command_index_.end()) {
                command_index_[cmd] = dir + filename;
            }
        }
        closedir(dp);
    }
}

// ── ICompletionProvider interface ────────────────────────────

std::string FishCompletionProvider::name() const {
    return "fish";
}

int FishCompletionProvider::priority() const {
    return 10;
}

bool FishCompletionProvider::can_complete(const std::string &command) const {
    return command_index_.find(command) != command_index_.end();
}

std::vector<Completion> FishCompletionProvider::complete(
    const std::string &command,
    const std::string &current_word,
    const std::vector<std::string> & /*args*/,
    const ShellState & /*state*/) const {

    // Lazy-load the file on first access for this command
    load_command(command);

    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto it = cache_.find(command);
    if (it == cache_.end()) {
        return {};
    }

    return fish_entries_to_completions(it->second, current_word);
}

// ── Introspection ────────────────────────────────────────────

size_t FishCompletionProvider::indexed_command_count() const {
    return command_index_.size();
}

bool FishCompletionProvider::is_command_loaded(
    const std::string &command) const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    return cache_.find(command) != cache_.end();
}

// ── Lazy loading ─────────────────────────────────────────────

void FishCompletionProvider::load_command(
    const std::string &command) const {
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        if (cache_.find(command) != cache_.end()) {
            return;  // Already loaded
        }
    }

    auto idx_it = command_index_.find(command);
    if (idx_it == command_index_.end()) {
        return;
    }

    auto entries = parse_file(idx_it->second);

    std::lock_guard<std::mutex> lock(cache_mutex_);
    cache_[command] = std::move(entries);
}

// ── File parsing ─────────────────────────────────────────────

std::vector<FishCompletionEntry> FishCompletionProvider::parse_file(
    const std::string &path) const {

    std::vector<FishCompletionEntry> entries;
    std::ifstream file(path);
    if (!file.is_open()) {
        return entries;
    }

    std::string line;
    while (std::getline(file, line)) {
        FishCompletionEntry entry;
        if (parse_fish_complete_line(line, entry)) {
            entries.push_back(std::move(entry));
        }
    }

    return entries;
}
