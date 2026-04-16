#ifndef TASH_MANPAGE_COMPLETION_PROVIDER_H
#define TASH_MANPAGE_COMPLETION_PROVIDER_H

#include "tash/plugin.h"
#include <string>
#include <vector>
#include <unordered_map>

// ── Help option parsed from --help output ────────────────────

struct HelpOption {
    std::string short_flag;   // e.g., "-v"
    std::string long_flag;    // e.g., "--verbose"
    std::string description;  // e.g., "Increase verbosity"
};

// ── Parse --help output into structured options ──────────────

std::vector<HelpOption> parse_help_output(const std::string &help_text);

// ── Completion provider using --help output ──────────────────

class ManpageCompletionProvider : public ICompletionProvider {
public:
    std::string name() const override { return "manpage"; }
    int priority() const override { return 5; }
    bool can_complete(const std::string &command) const override;
    std::vector<Completion> complete(
        const std::string &command,
        const std::string &current_word,
        const std::vector<std::string> &args,
        const ShellState &state) const override;

    // Expose cache for testing
    const std::unordered_map<std::string, std::vector<HelpOption>> &cache() const {
        return cache_;
    }

private:
    mutable std::unordered_map<std::string, std::vector<HelpOption>> cache_;

    std::vector<HelpOption> get_help_options(const std::string &command) const;
};

#endif // TASH_MANPAGE_COMPLETION_PROVIDER_H
