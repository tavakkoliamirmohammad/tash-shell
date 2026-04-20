#include "tash/plugin.h"
#include "tash/util/io.h"
#include <algorithm>
#include <unordered_map>

// ── Registration ──────────────────────────────────────────────

void PluginRegistry::register_completion_provider(
    std::unique_ptr<ICompletionProvider> provider) {
    completion_providers_.push_back(std::move(provider));
}

void PluginRegistry::register_prompt_provider(
    std::unique_ptr<IPromptProvider> provider) {
    prompt_providers_.push_back(std::move(provider));
}

void PluginRegistry::register_history_provider(
    std::unique_ptr<IHistoryProvider> provider) {
    history_providers_.push_back(std::move(provider));
}

void PluginRegistry::register_hook_provider(
    std::unique_ptr<IHookProvider> provider) {
    hook_providers_.push_back(std::move(provider));
}

// ── Completion dispatch ───────────────────────────────────────

std::vector<Completion> PluginRegistry::complete(
    const std::string &command,
    const std::string &current_word,
    const std::vector<std::string> &args,
    const ShellState &state) const {

    // Collect results from all providers that can handle this command,
    // ordered by priority (highest first)
    struct ProviderResult {
        int priority;
        std::vector<Completion> completions;
    };
    std::vector<ProviderResult> results;

    for (const auto &provider : completion_providers_) {
        if (provider->can_complete(command)) {
            auto comps = provider->complete(command, current_word, args, state);
            if (!comps.empty()) {
                results.push_back({provider->priority(), std::move(comps)});
            }
        }
    }

    // Sort by priority descending
    std::sort(results.begin(), results.end(),
        [](const ProviderResult &a, const ProviderResult &b) {
            return a.priority > b.priority;
        });

    // Merge: higher-priority completions first, dedup by text
    std::vector<Completion> merged;
    std::unordered_map<std::string, bool> seen;

    for (const auto &pr : results) {
        for (const auto &c : pr.completions) {
            if (seen.find(c.text) == seen.end()) {
                seen[c.text] = true;
                merged.push_back(c);
            }
        }
    }

    return merged;
}

// ── Prompt dispatch ───────────────────────────────────────────

std::string PluginRegistry::render_prompt(const ShellState &state) {
    // Empty list or all providers returning "" means "no plugin wants
    // to override the prompt" — callers fall through to their builtin.
    if (prompt_providers_.empty()) return "";

    // Sort by priority (highest first) so registration order doesn't
    // accidentally hide a high-priority provider.
    std::vector<IPromptProvider *> ordered;
    ordered.reserve(prompt_providers_.size());
    for (const auto &p : prompt_providers_) ordered.push_back(p.get());
    std::sort(ordered.begin(), ordered.end(),
              [](IPromptProvider *a, IPromptProvider *b) {
                  return a->priority() > b->priority();
              });

    for (IPromptProvider *p : ordered) {
        std::string out = p->render(state);
        if (!out.empty()) return out;
    }
    return "";
}

// ── History dispatch ──────────────────────────────────────────

void PluginRegistry::record_history(const HistoryEntry &entry) {
    for (const auto &provider : history_providers_) {
        provider->record(entry);
    }
}

std::vector<HistoryEntry> PluginRegistry::search_history(
    const std::string &query,
    const SearchFilter &filter) const {
    if (history_providers_.empty()) {
        return {};
    }
    // Search primary (first) provider
    return history_providers_.front()->search(query, filter);
}

std::vector<HistoryEntry> PluginRegistry::recent_history(int count) const {
    if (history_providers_.empty()) {
        return {};
    }
    return history_providers_.front()->recent(count);
}

HistoryStats PluginRegistry::history_stats() const {
    if (history_providers_.empty()) {
        return {};
    }
    return history_providers_.front()->stats();
}

// ── Hook dispatch ─────────────────────────────────────────────

void PluginRegistry::fire_before_command(
    const std::string &command, ShellState &state) {
    for (const auto &provider : hook_providers_) {
        provider->on_before_command(command, state);
    }
}

void PluginRegistry::fire_after_command(
    const std::string &command, int exit_code,
    const std::string &stderr_output, ShellState &state) {
    for (const auto &provider : hook_providers_) {
        provider->on_after_command(command, exit_code, stderr_output, state);
    }
}

// ── Lifecycle hook dispatch ───────────────────────────────────

void PluginRegistry::fire_startup(ShellState &state) {
    for (const auto &provider : hook_providers_) {
        provider->on_startup(state);
        tash::io::debug("plugin: " + provider->name() + ".on_startup ok");
    }
}

void PluginRegistry::fire_exit(ShellState &state) {
    for (const auto &provider : hook_providers_) {
        provider->on_exit(state);
    }
}

void PluginRegistry::fire_config_reload(ShellState &state) {
    for (const auto &provider : hook_providers_) {
        provider->on_config_reload(state);
    }
}

// ── Introspection ─────────────────────────────────────────────

size_t PluginRegistry::completion_provider_count() const {
    return completion_providers_.size();
}

size_t PluginRegistry::prompt_provider_count() const {
    return prompt_providers_.size();
}

size_t PluginRegistry::history_provider_count() const {
    return history_providers_.size();
}

size_t PluginRegistry::hook_provider_count() const {
    return hook_providers_.size();
}

// ── Process-wide singleton registry ───────────────────────────

PluginRegistry& global_plugin_registry() {
    static PluginRegistry instance;
    return instance;
}
