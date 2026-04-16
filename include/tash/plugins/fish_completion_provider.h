#ifndef TASH_FISH_COMPLETION_PROVIDER_H
#define TASH_FISH_COMPLETION_PROVIDER_H

#include "tash/plugin.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

// ── Fish completion file parser ──────────────────────────────

struct FishCompletionEntry {
    std::string command;
    std::string short_opt;
    std::string long_opt;
    std::string description;
    std::string arguments;
    bool no_files;
    bool requires_arg;

    FishCompletionEntry()
        : no_files(false), requires_arg(false) {}
};

// Parse a single `complete ...` line from a fish completion file.
// Returns true if parsing succeeded, false if the line should be skipped.
bool parse_fish_complete_line(const std::string &line,
                              FishCompletionEntry &entry);

// Convert parsed fish entries into Completion objects, optionally
// filtering by a prefix (current_word).
std::vector<Completion> fish_entries_to_completions(
    const std::vector<FishCompletionEntry> &entries,
    const std::string &prefix);

// ── Fish Completion Provider ─────────────────────────────────

class FishCompletionProvider : public ICompletionProvider {
public:
    // Construct with default system fish completion directories
    FishCompletionProvider();

    // Construct with explicit directories (for testing)
    explicit FishCompletionProvider(
        const std::vector<std::string> &search_dirs);

    std::string name() const override;
    int priority() const override;
    bool can_complete(const std::string &command) const override;
    std::vector<Completion> complete(
        const std::string &command,
        const std::string &current_word,
        const std::vector<std::string> &args,
        const ShellState &state) const override;

    // Introspection for testing
    size_t indexed_command_count() const;
    bool is_command_loaded(const std::string &command) const;

private:
    void build_index();
    void load_command(const std::string &command) const;
    std::vector<FishCompletionEntry> parse_file(
        const std::string &path) const;

    std::vector<std::string> search_dirs_;

    // Maps command name -> fish completion file path
    std::unordered_map<std::string, std::string> command_index_;

    // Lazy-loaded cache: command name -> parsed completions
    mutable std::unordered_map<std::string,
        std::vector<FishCompletionEntry>> cache_;
    mutable std::mutex cache_mutex_;
};

#endif // TASH_FISH_COMPLETION_PROVIDER_H
