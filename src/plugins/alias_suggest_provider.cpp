#include "tash/plugins/alias_suggest_provider.h"
#include "tash/core.h"
#include "tash/shell.h"

// ── Helper: find the alias whose value best matches the command ──

std::string find_matching_alias(
    const std::string &command,
    const std::unordered_map<std::string, std::string> &aliases) {

    if (command.empty()) {
        return "";
    }

    std::string best_alias;
    std::string::size_type best_length = 0;

    for (const auto &pair : aliases) {
        const std::string &alias_name  = pair.first;
        const std::string &alias_value = pair.second;

        if (alias_value.empty()) {
            continue;
        }

        bool match = false;
        if (command == alias_value) {
            // Exact match: the whole command IS the alias value
            match = true;
        } else if (command.size() > alias_value.size() &&
                   command.compare(0, alias_value.size(), alias_value) == 0 &&
                   command[alias_value.size()] == ' ') {
            // Prefix match: command starts with alias_value followed by a space
            match = true;
        }

        if (match && alias_value.size() > best_length) {
            best_length = alias_value.size();
            best_alias  = alias_name;
        }
    }

    return best_alias;
}

// ── Helper: extract the remaining arguments after the alias value ─

std::string get_remaining_args(const std::string &command,
                                const std::string &alias_value) {
    if (command.size() <= alias_value.size()) {
        return "";
    }
    return command.substr(alias_value.size());
}

// ── AliasSuggestProvider implementation ──────────────────────────

std::string AliasSuggestProvider::name() const {
    return "alias-suggest";
}

void AliasSuggestProvider::on_before_command(const std::string &command,
                                              ShellState &state) {
    std::string alias_name = find_matching_alias(command, state.aliases);

    if (alias_name.empty()) {
        return;
    }

    // Only remind once per session for each alias.
    if (reminded_this_session_.count(alias_name)) {
        return;
    }

    reminded_this_session_.insert(alias_name);

    const std::string &alias_value = state.aliases[alias_name];
    std::string remaining = get_remaining_args(command, alias_value);

    write_stderr("\xF0\x9F\x92\xA1 You have an alias for this: " +
                 alias_name + remaining + "\n");
}

void AliasSuggestProvider::on_after_command(const std::string & /*command*/,
                                             int /*exit_code*/,
                                             const std::string & /*stderr_output*/,
                                             ShellState & /*state*/) {
    // No-op.
}

const std::unordered_set<std::string> &AliasSuggestProvider::reminded_aliases() const {
    return reminded_this_session_;
}

void AliasSuggestProvider::reset_reminders() {
    reminded_this_session_.clear();
}
