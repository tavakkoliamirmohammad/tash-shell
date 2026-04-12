#include "shell.h"

using namespace std;

static vector<string> builtin_name_list;

static char *builtin_generator(const char *text, int state) {
    static size_t list_index;
    static size_t len;

    if (!state) {
        // Rebuild the name list from the dispatch table + aliases
        builtin_name_list.clear();
        const auto &builtins = get_builtins();
        for (const auto &pair : builtins) {
            builtin_name_list.push_back(pair.first);
        }
        // "bg" is handled specially in main.cpp, add it too
        builtin_name_list.push_back("bg");

        list_index = 0;
        len = strlen(text);
    }

    // Search builtins
    while (list_index < builtin_name_list.size()) {
        const string &name = builtin_name_list[list_index];
        list_index++;
        if (name.compare(0, len, text) == 0) {
            return strdup(name.c_str());
        }
    }

    return nullptr;
}

char **tash_completion(const char *text, int start, int end) {
    (void)end;
    char **matches = nullptr;

    if (start == 0) {
        matches = rl_completion_matches(text, builtin_generator);
    }

    return matches;
}
