#include "tash/core.h"
#include "tash/plugin.h"
#include "tash/ui.h"
#include "tash/ui/fuzzy_finder.h"
#include "tash/util/safe_exec.h"
#include "theme.h"

#include <algorithm>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>
#include <unordered_set>

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

static Replxx::Color to_replxx(const RGB &c) {
    int r = (c.r * 5 + 127) / 255;
    int g = (c.g * 5 + 127) / 255;
    int b = (c.b * 5 + 127) / 255;
    return replxx::color::rgb666(r, g, b);
}

static Replxx::Color comp_builtin() { return to_replxx(g_current_theme.comp_builtin); }
static Replxx::Color comp_command() { return to_replxx(g_current_theme.comp_command); }
static Replxx::Color comp_subcmd()  { return to_replxx(g_current_theme.op); }
static Replxx::Color comp_envvar()  { return to_replxx(g_current_theme.variable); }
static Replxx::Color comp_file()    { return to_replxx(g_current_theme.comp_file); }
static Replxx::Color comp_dir()     { return to_replxx(g_current_theme.comp_directory); }

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
        vector<string> builtin_names;
        for (const auto &pair : builtins) builtin_names.push_back(pair.first);
        builtin_names.push_back("bg");
        builtin_names.push_back("z");

        for (const string &name : builtin_names) {
            if (name.compare(0, prefix.size(), prefix) == 0) {
                completions.emplace_back(name, comp_builtin());
            }
        }

        // PATH executables (colored green)
        build_command_cache();
        const auto &path_cmds = get_path_commands();
        for (const string &cmd_name : path_cmds) {
            if (cmd_name.compare(0, prefix.size(), prefix) == 0) {
                completions.emplace_back(cmd_name, comp_command());
            }
        }

        // Fuzzy fallback: if no prefix matches and the user typed ≥2 chars,
        // rank builtins + PATH commands by fuzzy score.
        if (completions.empty() && prefix.size() >= 2) {
            vector<string> candidates = builtin_names;
            candidates.insert(candidates.end(), path_cmds.begin(), path_cmds.end());
            auto ranked = tash::fuzzy_filter(prefix, candidates, 20);
            for (const auto &r : ranked) {
                bool is_builtin_name =
                    std::find(builtin_names.begin(), builtin_names.end(), r.text) !=
                    builtin_names.end();
                completions.emplace_back(r.text,
                    is_builtin_name ? comp_builtin() : comp_command());
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

    // Flag completion via plugin registry (manpage provider, etc.)
    if (!prefix.empty() && prefix[0] == '-') {
        ShellState dummy;
        vector<string> args;
        auto plugin_comps = global_plugin_registry().complete(
            cmd, prefix, args, dummy);
        for (const auto &c : plugin_comps) {
            if (c.text.compare(0, prefix.size(), prefix) == 0) {
                completions.emplace_back(c.text, comp_subcmd());
            }
        }
        if (!completions.empty()) return completions;
    }

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

    // PID completion for kill-family builtins. Try `ps` first for the
    // command name in the output; fall back to /proc scan (Linux) when
    // ps is missing — minimal container images often have neither ps
    // nor procps installed.
    static const std::unordered_set<std::string> kill_cmds = {
        "kill", "bgkill", "bgstop", "bgstart"
    };
    if (kill_cmds.count(cmd)) {
        // 500ms timeout matches the prompt's git queries -- on a
        // process-table lookup this is ample.
        auto ps = tash::util::safe_exec({"ps", "-eo", "pid,comm="}, 500);
        const std::string &out = ps.stdout_text;
        size_t line_start = 0;
        while (line_start < out.size()) {
            size_t line_end = out.find('\n', line_start);
            size_t line_len = (line_end == std::string::npos ? out.size() : line_end) - line_start;
            const char *s = out.c_str() + line_start;
            const char *e = s + line_len;
            while (s < e && (*s == ' ' || *s == '\t')) s++;
            const char *space = s;
            while (space < e && *space != ' ' && *space != '\t') space++;
            if (space != s) {
                std::string pid_str(s, space - s);
                if (pid_str.compare(0, prefix.size(), prefix) == 0) {
                    completions.emplace_back(pid_str, comp_subcmd());
                }
            }
            if (line_end == std::string::npos) break;
            line_start = line_end + 1;
        }
        // /proc fallback — if ps produced nothing (e.g. minimal Linux
        // container), list numeric directories under /proc.
        if (completions.empty()) {
            DIR *proc = opendir("/proc");
            if (proc) {
                struct dirent *entry;
                while ((entry = readdir(proc)) != nullptr) {
                    const char *n = entry->d_name;
                    bool all_digits = *n != '\0';
                    for (const char *c = n; *c; ++c) {
                        if (*c < '0' || *c > '9') { all_digits = false; break; }
                    }
                    if (!all_digits) continue;
                    std::string pid_str(n);
                    if (pid_str.compare(0, prefix.size(), prefix) != 0) continue;
                    completions.emplace_back(pid_str, comp_subcmd());
                }
                closedir(proc);
            }
        }
        return completions;
    }

    // cd/pushd: directories only.
    static const std::unordered_set<std::string> dir_only_cmds = {
        "cd", "pushd"
    };
    bool dirs_only = dir_only_cmds.count(cmd) > 0;

    // File/directory completion
    string dir_part;
    string name_prefix;
    size_t last_slash = prefix.find_last_of('/');
    if (last_slash != string::npos) {
        dir_part = prefix.substr(0, last_slash + 1);
        name_prefix = prefix.substr(last_slash + 1);
    } else {
        dir_part = "";
        name_prefix = prefix;
    }

    string search_dir = dir_part.empty() ? "." : dir_part;
    // Expand ~ to HOME
    if (!search_dir.empty() && search_dir[0] == '~') {
        const char *home = getenv("HOME");
        if (home) search_dir = string(home) + search_dir.substr(1);
    }

    DIR *dp = opendir(search_dir.c_str());
    if (dp) {
        // Only replace the name portion, not the directory path
        context_len = (int)name_prefix.size();
        struct dirent *entry;
        while ((entry = readdir(dp)) != nullptr) {
            string name = entry->d_name;
            if (name == "." || name == "..") continue;
            // Skip hidden files unless the user typed a dot
            if (name[0] == '.' && (name_prefix.empty() || name_prefix[0] != '.')) continue;
            if (name.compare(0, name_prefix.size(), name_prefix) == 0) {
                string full_path = search_dir + "/" + name;
                struct stat st;
                bool is_dir = stat(full_path.c_str(), &st) == 0 &&
                              S_ISDIR(st.st_mode);
                if (is_dir) {
                    completions.emplace_back(name + "/", comp_dir());
                } else if (!dirs_only) {
                    completions.emplace_back(name, comp_file());
                }
            }
        }
        closedir(dp);
    }

    return completions;
}
