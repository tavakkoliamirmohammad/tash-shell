#include "tash/core.h"
#include "tash/ui.h"
#include "theme.h"

using namespace std;
using namespace replxx;

static const vector<string> git_subcommands = {
    "add", "bisect", "branch", "checkout", "cherry-pick", "clone",
    "commit", "diff", "fetch", "grep", "init", "log", "merge",
    "mv", "pull", "push", "rebase", "remote", "reset", "restore",
    "revert", "rm", "show", "stash", "status", "switch", "tag"
};

static const vector<string> docker_subcommands = {
    "build", "compose", "container", "exec", "image", "images",
    "logs", "network", "ps", "pull", "push", "rm", "rmi",
    "run", "start", "stop", "volume"
};

// Catppuccin colors for different completion types
static Replxx::Color comp_builtin() { return replxx::color::rgb666(2, 5, 4); }  // teal
static Replxx::Color comp_command() { return replxx::color::rgb666(3, 5, 3); }  // green
static Replxx::Color comp_subcmd()  { return replxx::color::rgb666(4, 3, 5); }  // mauve
static Replxx::Color comp_envvar()  { return replxx::color::rgb666(2, 4, 5); }  // sky

Replxx::completions_t completion_callback(const string &input, int &context_len) {
    Replxx::completions_t completions;

    size_t last_space = input.find_last_of(" \t");
    string prefix;
    string cmd;

    if (last_space == string::npos) {
        // Command position — complete builtins + PATH executables
        prefix = input;
        context_len = (int)prefix.size();

        // Builtins (colored teal)
        const auto &builtins = get_builtins();
        for (const auto &pair : builtins) {
            if (pair.first.compare(0, prefix.size(), prefix) == 0) {
                completions.emplace_back(pair.first, comp_builtin());
            }
        }
        if (string("bg").compare(0, prefix.size(), prefix) == 0)
            completions.emplace_back("bg", comp_builtin());
        if (string("z").compare(0, prefix.size(), prefix) == 0)
            completions.emplace_back("z", comp_builtin());

        // PATH executables (colored green)
        build_command_cache();
        for (const string &cmd_name : get_path_commands()) {
            if (cmd_name.compare(0, prefix.size(), prefix) == 0) {
                completions.emplace_back(cmd_name, comp_command());
            }
        }

        return completions;
    }

    // Argument position
    prefix = input.substr(last_space + 1);
    context_len = (int)prefix.size();

    // Extract command name
    size_t cmd_start = 0;
    while (cmd_start < input.size() && (input[cmd_start] == ' ' || input[cmd_start] == '\t')) cmd_start++;
    size_t cmd_end = cmd_start;
    while (cmd_end < input.size() && input[cmd_end] != ' ' && input[cmd_end] != '\t') cmd_end++;
    cmd = input.substr(cmd_start, cmd_end - cmd_start);

    // Variable completion (colored sky)
    if (!prefix.empty() && prefix[0] == '$') {
        string var_prefix = prefix.substr(1);
        extern char **environ;
        for (char **env = environ; *env; env++) {
            string entry = *env;
            size_t eq = entry.find('=');
            if (eq == string::npos) continue;
            string name = entry.substr(0, eq);
            if (name.compare(0, var_prefix.size(), var_prefix) == 0) {
                completions.emplace_back("$" + name, comp_envvar());
            }
        }
        return completions;
    }

    // Subcommand completion (colored mauve)
    const vector<string> *subcmds = nullptr;
    if (cmd == "git") subcmds = &git_subcommands;
    else if (cmd == "docker") subcmds = &docker_subcommands;

    if (subcmds) {
        for (const string &sub : *subcmds) {
            if (sub.compare(0, prefix.size(), prefix) == 0) {
                completions.emplace_back(sub, comp_subcmd());
            }
        }
    }

    return completions;
}
