// tash-specific config builtins: config (sync), session, theme.
//
// Split out of the old src/builtins/shell.cpp blob so POSIX meta
// builtins (meta.cpp) and signal/trap logic (trap.cpp) stand on their
// own.

#include "tash/builtins.h"
#include "tash/core/signals.h"
#include "tash/core/config_sync.h"
#include "tash/core/session.h"
#include "theme.h"

#include <cstdio>

using namespace std;

int builtin_config(const vector<string> &argv, ShellState &) {
    if (argv.size() < 2) {
        write_stderr(
            "config: usage:\n"
            "  config sync init                   initialize ~/.tash as a git repo\n"
            "  config sync remote <url>           set the git remote URL\n"
            "  config sync push                   commit and push changes\n"
            "  config sync pull                   pull latest changes\n"
            "  config sync diff                   show pending changes\n"
            "  config sync status                 is the repo initialized?\n");
        return 1;
    }
    if (argv[1] != "sync") {
        write_stderr("config: unknown subcommand: " + argv[1] + "\n");
        return 1;
    }
    if (argv.size() < 3) {
        write_stderr("config: sync requires an action\n");
        return 1;
    }
    std::string dir = tash::config_sync::get_tash_config_dir();
    const string &action = argv[2];

    if (action == "init") {
        if (!tash::config_sync::sync_init(dir)) {
            write_stderr("config: sync init failed\n");
            return 1;
        }
        write_stdout("config: initialized " + dir + "\n");
        return 0;
    }
    if (action == "remote") {
        if (argv.size() < 4) {
            write_stderr("config: remote requires a URL\n");
            return 1;
        }
        if (!tash::config_sync::sync_set_remote(dir, argv[3])) {
            write_stderr("config: setting remote failed\n");
            return 1;
        }
        write_stdout("config: remote set to " + argv[3] + "\n");
        return 0;
    }
    if (action == "push") {
        if (!tash::config_sync::sync_push(dir)) {
            write_stderr("config: push failed\n");
            return 1;
        }
        write_stdout("config: push ok\n");
        return 0;
    }
    if (action == "pull") {
        if (!tash::config_sync::sync_pull(dir)) {
            write_stderr("config: pull failed\n");
            return 1;
        }
        write_stdout("config: pull ok\n");
        return 0;
    }
    if (action == "diff") {
        write_stdout(tash::config_sync::sync_diff(dir));
        return 0;
    }
    if (action == "status") {
        if (tash::config_sync::sync_is_initialized(dir)) {
            write_stdout("config: initialized at " + dir + "\n");
            return 0;
        }
        write_stdout("config: not initialized (run 'config sync init')\n");
        return 1;
    }

    write_stderr("config: unknown sync action: " + action + "\n");
    return 1;
}

int builtin_session(const vector<string> &argv, ShellState &state) {
    if (argv.size() < 2) {
        write_stderr(
            "session: usage:\n"
            "  session list             list saved sessions\n"
            "  session save <name>      capture current state as <name>\n"
            "  session load <name>      restore state from <name>\n"
            "  session rm <name>        delete saved <name>\n");
        return 1;
    }

    const string &sub = argv[1];

    if (sub == "list") {
        auto sessions = list_sessions();
        if (sessions.empty()) {
            write_stdout("(no saved sessions)\n");
            return 0;
        }
        for (const auto &s : sessions) {
            write_stdout("  " + s.name + "  " +
                         s.working_directory + "\n");
        }
        return 0;
    }

    if (sub == "save") {
        if (argv.size() < 3) {
            write_stderr("session: save requires a name\n");
            return 1;
        }
        const string &name = argv[2];
        SessionInfo info = capture_current_state(name, state);
        string dir = get_sessions_dir();
        string path = dir + "/" + name + ".json";
        if (!save_session(path, info)) {
            write_stderr("session: failed to write " + path + "\n");
            return 1;
        }
        write_stdout("session: saved '" + name + "' to " + path + "\n");
        return 0;
    }

    if (sub == "load") {
        if (argv.size() < 3) {
            write_stderr("session: load requires a name\n");
            return 1;
        }
        const string &name = argv[2];
        if (!session_exists(name)) {
            write_stderr("session: no such session: " + name + "\n");
            return 1;
        }
        string path = get_sessions_dir() + "/" + name + ".json";
        SessionInfo info = load_session(path);
        restore_session(info, state);
        write_stdout("session: loaded '" + name + "'\n");
        return 0;
    }

    if (sub == "rm") {
        if (argv.size() < 3) {
            write_stderr("session: rm requires a name\n");
            return 1;
        }
        const string &name = argv[2];
        if (!delete_session(name)) {
            write_stderr("session: no such session: " + name + "\n");
            return 1;
        }
        write_stdout("session: removed '" + name + "'\n");
        return 0;
    }

    write_stderr("session: unknown subcommand: " + sub + "\n");
    return 1;
}

namespace {

string hex6(const RGB &c) {
    char buf[8];
    snprintf(buf, sizeof(buf), "#%02x%02x%02x", c.r, c.g, c.b);
    return buf;
}

void theme_print_swatch(const string &label, const RGB &c) {
    write_stdout("  " + ansi_fg(c) + "██████" + CAT_RESET + "  " +
                 label + "  " + hex6(c) + "\n");
}

} // anonymous namespace

int builtin_theme(const vector<string> &argv, ShellState &) {
    if (argv.size() < 2) {
        write_stderr(
            "theme: usage:\n"
            "  theme list          list available themes\n"
            "  theme current       print the active theme name\n"
            "  theme set <name>    switch to a theme (persists)\n"
            "  theme preview [<name>]  show color swatches\n");
        return 1;
    }
    const string &sub = argv[1];

    if (sub == "list") {
        auto names = list_available_themes();
        if (names.empty()) {
            write_stdout("(no themes found)\n");
            return 0;
        }
        for (const auto &n : names) {
            bool active = (n == g_current_theme_name);
            write_stdout((active ? string("* ") : string("  ")) + n + "\n");
        }
        return 0;
    }

    if (sub == "current") {
        write_stdout(g_current_theme_name + "\n");
        return 0;
    }

    if (sub == "set") {
        if (argv.size() < 3) {
            write_stderr("theme: set requires a theme name\n");
            return 1;
        }
        string err;
        if (!set_active_theme(argv[2], err)) {
            write_stderr("theme: " + err + "\n");
            return 1;
        }
        write_stdout("theme: switched to " + argv[2] + "\n");
        return 0;
    }

    if (sub == "preview") {
        Theme t = g_current_theme;
        if (argv.size() >= 3) {
            string path = find_theme_file(argv[2]);
            if (path.empty()) {
                write_stderr("theme: not found: " + argv[2] + "\n");
                return 1;
            }
            t = Theme::load_from_file(path);
        }
        write_stdout("\n" + string(CAT_BOLD) + t.name + CAT_RESET +
                     " (" + t.variant + ")\n\n");
        write_stdout(string(CAT_BOLD) + "syntax" + CAT_RESET + "\n");
        theme_print_swatch("command_valid  ", t.command_valid);
        theme_print_swatch("command_builtin", t.command_builtin);
        theme_print_swatch("command_invalid", t.command_invalid);
        theme_print_swatch("string         ", t.string_color);
        theme_print_swatch("variable       ", t.variable);
        theme_print_swatch("operator       ", t.op);
        theme_print_swatch("redirect       ", t.redirect);
        theme_print_swatch("comment        ", t.comment);
        write_stdout("\n" + string(CAT_BOLD) + "prompt" + CAT_RESET + "\n");
        theme_print_swatch("success        ", t.prompt_success);
        theme_print_swatch("error          ", t.prompt_error);
        theme_print_swatch("path           ", t.prompt_path);
        theme_print_swatch("git            ", t.prompt_git);
        theme_print_swatch("duration       ", t.prompt_duration);
        theme_print_swatch("user           ", t.prompt_user);
        theme_print_swatch("separator      ", t.prompt_separator);
        write_stdout("\n" + string(CAT_BOLD) + "completion" + CAT_RESET + "\n");
        theme_print_swatch("builtin        ", t.comp_builtin);
        theme_print_swatch("command        ", t.comp_command);
        theme_print_swatch("file           ", t.comp_file);
        theme_print_swatch("directory      ", t.comp_directory);
        theme_print_swatch("option         ", t.comp_option);
        theme_print_swatch("description    ", t.comp_description);
        write_stdout("\n");
        return 0;
    }

    write_stderr("theme: unknown subcommand: " + sub + "\n");
    return 1;
}
