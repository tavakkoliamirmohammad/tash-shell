#ifndef TASH_FIG_COMPLETION_PROVIDER_H
#define TASH_FIG_COMPLETION_PROVIDER_H

#include "tash/plugin.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

// ── Fig/Amazon Q Completion Provider ─────────────────────────

class FigCompletionProvider : public ICompletionProvider {
public:
    // Construct with default spec directory (~/.tash/completions/fig/)
    FigCompletionProvider();

    // Construct with explicit directory (for testing)
    explicit FigCompletionProvider(const std::string &spec_dir);

    std::string name() const override;
    int priority() const override;
    bool can_complete(const std::string &command) const override;
    std::vector<Completion> complete(
        const std::string &command,
        const std::string &current_word,
        const std::vector<std::string> &args,
        const ShellState &state) const override;

    // Introspection for testing
    size_t loaded_spec_count() const;
    bool has_spec(const std::string &command) const;

    // Load a spec from a JSON string (for testing)
    bool load_spec_from_string(const std::string &command,
                               const std::string &json_str);

private:
    bool load_spec(const std::string &command) const;

    // Given a parsed spec JSON and the args typed so far,
    // traverse the subcommand tree and return matching completions
    std::vector<Completion> traverse_and_complete(
        const nlohmann::json &spec,
        const std::string &current_word,
        const std::vector<std::string> &args) const;

    // Extract options from a JSON node
    std::vector<Completion> extract_options(
        const nlohmann::json &node,
        const std::string &prefix) const;

    // Extract subcommands from a JSON node
    std::vector<Completion> extract_subcommands(
        const nlohmann::json &node,
        const std::string &prefix) const;

    // Extract arguments from a JSON node
    std::vector<Completion> extract_arguments(
        const nlohmann::json &node,
        const std::string &prefix) const;

    // Find the deepest matching subcommand node
    const nlohmann::json *find_subcommand(
        const nlohmann::json &node,
        const std::vector<std::string> &args,
        size_t &consumed) const;

    std::string spec_dir_;

    // Maps command name -> JSON spec file path
    std::unordered_map<std::string, std::string> spec_index_;

    // Loaded specs cache
    mutable std::unordered_map<std::string, nlohmann::json> specs_;
    mutable std::mutex spec_mutex_;
};

#endif // TASH_FIG_COMPLETION_PROVIDER_H
