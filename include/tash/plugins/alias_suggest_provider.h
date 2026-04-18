#ifndef TASH_PLUGINS_ALIAS_SUGGEST_PROVIDER_H
#define TASH_PLUGINS_ALIAS_SUGGEST_PROVIDER_H

#include "tash/plugin.h"
#include <string>
#include <unordered_map>
#include <unordered_set>

// ── Helper functions (visible for testing) ───────────────────

// Find the alias whose value best matches the beginning of |command|.
// Prefers the longest matching alias value (most specific).
// Returns the alias name, or empty string if no match.
std::string find_matching_alias(
    const std::string &command,
    const std::unordered_map<std::string, std::string> &aliases);

// Return the part of |command| after |alias_value|.
// If the command equals the alias value exactly, returns "".
std::string get_remaining_args(const std::string &command,
                                const std::string &alias_value);

// ── AliasSuggestProvider ─────────────────────────────────────

class AliasSuggestProvider : public IHookProvider {
public:
    std::string name() const override;

    // Checks whether the user typed a command that could be replaced by
    // an existing alias.  If so, prints a one-time-per-session reminder.
    void on_before_command(const std::string &command,
                           ShellState &state) override;

    // No-op.
    void on_after_command(const std::string &command,
                          int exit_code,
                          const std::string &stderr_output,
                          ShellState &state) override;

    // Expose reminded set for testing.
    const std::unordered_set<std::string> &reminded_aliases() const;

    // Clear the reminded set (e.g. for a new session).
    void reset_reminders();

    // No lifecycle state to manage.
    void on_startup(ShellState &)       override {}
    void on_exit(ShellState &)          override {}
    void on_config_reload(ShellState &) override {}

private:
    std::unordered_set<std::string> reminded_this_session_;
};

#endif // TASH_PLUGINS_ALIAS_SUGGEST_PROVIDER_H
