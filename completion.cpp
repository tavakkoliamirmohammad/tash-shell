#include "shell.h"

static const char *builtin_names[] = {
    "cd", "pwd", "exit", "export", "unset", "alias", "unalias",
    "clear", "bglist", "bgkill", "bgstop", "bgstart", "fg",
    "history", "source", "bg", "which", "type",
    "pushd", "popd", "dirs",
    nullptr
};

static char *builtin_generator(const char *text, int state) {
    static int list_index;
    static size_t len;

    if (!state) {
        list_index = 0;
        len = strlen(text);
    }

    while (builtin_names[list_index]) {
        const char *name = builtin_names[list_index];
        list_index++;
        if (strncmp(name, text, len) == 0) {
            return strdup(name);
        }
    }

    // Also check aliases
    static unordered_map<string, string>::iterator alias_it;
    if (!state) {
        alias_it = aliases.begin();
    }
    while (alias_it != aliases.end()) {
        string name = alias_it->first;
        ++alias_it;
        if (name.compare(0, len, text) == 0) {
            return strdup(name.c_str());
        }
    }

    return nullptr;
}

char **tash_completion(const char *text, int start, int end) {
    (void)end;
    char **matches = nullptr;

    // If this is the first word (command position), complete with builtins + aliases
    if (start == 0) {
        matches = rl_completion_matches(text, builtin_generator);
    }

    // Return NULL to fall through to default filename completion for arguments
    return matches;
}
