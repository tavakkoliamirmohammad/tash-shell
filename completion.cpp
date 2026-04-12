#include "shell.h"
#include <dirent.h>

using namespace std;

// ── Git subcommand completions ─────────────────────────────────

static const char *git_subcommands[] = {
    "add", "bisect", "branch", "checkout", "cherry-pick", "clone",
    "commit", "diff", "fetch", "grep", "init", "log", "merge",
    "mv", "pull", "push", "rebase", "remote", "reset", "restore",
    "revert", "rm", "show", "stash", "status", "switch", "tag",
    nullptr
};

static const char *docker_subcommands[] = {
    "build", "compose", "container", "exec", "image", "images",
    "logs", "network", "ps", "pull", "push", "rm", "rmi",
    "run", "start", "stop", "volume",
    nullptr
};

// ── Builtin name generator ─────────────────────────────────────

static vector<string> builtin_name_list;

static char *builtin_generator(const char *text, int state) {
    static size_t list_index;
    static size_t len;

    if (!state) {
        builtin_name_list.clear();
        const auto &builtins = get_builtins();
        for (const auto &pair : builtins) {
            builtin_name_list.push_back(pair.first);
        }
        builtin_name_list.push_back("bg");
        builtin_name_list.push_back("z");

        list_index = 0;
        len = strlen(text);
    }

    while (list_index < builtin_name_list.size()) {
        const string &name = builtin_name_list[list_index];
        list_index++;
        if (name.compare(0, len, text) == 0) {
            return strdup(name.c_str());
        }
    }

    return nullptr;
}

// ── Subcommand generator ───────────────────────────────────────

static const char **current_subcommands = nullptr;

static char *subcommand_generator(const char *text, int state) {
    static int list_index;
    static size_t len;

    if (!state) {
        list_index = 0;
        len = strlen(text);
    }

    if (!current_subcommands) return nullptr;

    while (current_subcommands[list_index]) {
        const char *name = current_subcommands[list_index];
        list_index++;
        if (strncmp(name, text, len) == 0) {
            return strdup(name);
        }
    }

    return nullptr;
}

// ── Environment variable generator ─────────────────────────────

extern char **environ;

static char *env_var_generator(const char *text, int state) {
    static int env_index;
    static size_t len;
    // text includes the leading $
    const char *var_prefix = text + 1;  // skip $

    if (!state) {
        env_index = 0;
        len = strlen(var_prefix);
    }

    while (environ[env_index]) {
        char *entry = environ[env_index];
        env_index++;
        // Each entry is "NAME=value"
        char *eq = strchr(entry, '=');
        if (!eq) continue;
        size_t name_len = eq - entry;
        if (name_len >= len && strncmp(entry, var_prefix, len) == 0) {
            // Return $NAME
            string result = "$" + string(entry, name_len);
            return strdup(result.c_str());
        }
    }

    return nullptr;
}

// ── Main completion function ───────────────────────────────────

char **tash_completion(const char *text, int start, int end) {
    (void)end;
    char **matches = nullptr;

    // Variable completion: if text starts with $
    if (text[0] == '$') {
        matches = rl_completion_matches(text, env_var_generator);
        if (matches) return matches;
    }

    if (start == 0) {
        // Command position: complete builtins + PATH commands
        matches = rl_completion_matches(text, builtin_generator);
    } else {
        // Argument position: check what command we're completing for
        // Extract the command name from the line buffer
        string line(rl_line_buffer, start);
        string cmd;
        // Find first word
        size_t i = 0;
        while (i < line.size() && line[i] == ' ') i++;
        while (i < line.size() && line[i] != ' ') {
            cmd += line[i];
            i++;
        }

        if (cmd == "git") {
            current_subcommands = git_subcommands;
            matches = rl_completion_matches(text, subcommand_generator);
        } else if (cmd == "docker") {
            current_subcommands = docker_subcommands;
            matches = rl_completion_matches(text, subcommand_generator);
        }
        // Fall through to default filename completion if no matches
    }

    return matches;
}
