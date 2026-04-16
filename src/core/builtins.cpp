#include "tash/core.h"
#include "tash/history.h"
#include "tash/ui/clipboard.h"
#include "tash/ui/inline_docs.h"
#include "theme.h"
#include <cstring>

using namespace std;

extern char **environ;

// ── Individual builtin implementations ─────────────────────────

static int builtin_cd(const vector<string> &argv, ShellState &state) {
    char cwd[MAX_SIZE];
    const char *target = nullptr;

    if (argv.size() > 1) {
        if (argv[1] == "-") {
            if (state.previous_directory.empty()) {
                write_stderr("cd: OLDPWD not set\n");
                return 1;
            }
            target = state.previous_directory.c_str();
        } else {
            target = argv[1].c_str();
        }
    } else {
        target = getenv("HOME");
        if (!target) {
            write_stderr("cd: HOME not set\n");
            return 1;
        }
    }

    if (getcwd(cwd, MAX_SIZE) == nullptr) {
        write_stderr("cd: " + string(strerror(errno)) + "\n");
        return 1;
    }

    if (chdir(target) == -1) {
        write_stderr("cd: " + string(target) + ": " + strerror(errno) + "\n");
        return 1;
    }

    state.previous_directory = string(cwd);

    // Record directory for frecency tracking
    char new_cwd[MAX_SIZE];
    if (getcwd(new_cwd, MAX_SIZE) != nullptr) {
        z_record_directory(string(new_cwd));
        if (argv.size() > 1 && argv[1] == "-") {
            write_stdout(string(new_cwd) + "\n");
        }
    }
    return 0;
}

static int builtin_pwd(const vector<string> &, ShellState &) {
    char temp[MAX_SIZE];
    if (getcwd(temp, MAX_SIZE) != nullptr) {
        write_stdout(string(temp) + "\n");
        return 0;
    }
    write_stderr("pwd: " + string(strerror(errno)) + "\n");
    return 1;
}

static int builtin_exit(const vector<string> &, ShellState &) {
    write_stdout("GoodBye! See you soon!\n");
    exit(0);
    return 0;
}

static int builtin_export(const vector<string> &argv, ShellState &) {
    if (argv.size() < 2) {
        for (char **env = environ; *env != nullptr; env++)
            write_stdout(string(*env) + "\n");
    } else {
        string arg = argv[1];
        size_t eq_pos = arg.find('=');
        if (eq_pos != string::npos) {
            setenv(arg.substr(0, eq_pos).c_str(), arg.substr(eq_pos + 1).c_str(), 1);
        } else {
            write_stderr("export: invalid format. Usage: export VAR=value\n");
            return 1;
        }
    }
    return 0;
}

static int builtin_unset(const vector<string> &argv, ShellState &) {
    if (argv.size() >= 2) {
        unsetenv(argv[1].c_str());
    } else {
        write_stderr("unset: missing variable name\n");
        return 1;
    }
    return 0;
}

static int builtin_alias(const vector<string> &argv, ShellState &state) {
    if (argv.size() < 2) {
        for (auto &pair : state.aliases) {
            write_stdout("alias " + pair.first + "='" + pair.second + "'\n");
        }
    } else {
        string arg = argv[1];
        size_t eq_pos = arg.find('=');
        if (eq_pos != string::npos) {
            string name = arg.substr(0, eq_pos);
            string value = arg.substr(eq_pos + 1);
            if (value.size() >= 2 &&
                ((value.front() == '\'' && value.back() == '\'') ||
                 (value.front() == '"' && value.back() == '"'))) {
                value = value.substr(1, value.size() - 2);
            }
            state.aliases[name] = value;
        } else {
            if (state.aliases.count(arg)) {
                write_stdout("alias " + arg + "='" + state.aliases[arg] + "'\n");
            } else {
                write_stderr("alias: " + arg + ": not found\n");
                return 1;
            }
        }
    }
    return 0;
}

static int builtin_unalias(const vector<string> &argv, ShellState &state) {
    if (argv.size() >= 2) {
        if (state.aliases.count(argv[1])) {
            state.aliases.erase(argv[1]);
        } else {
            write_stderr("unalias: " + argv[1] + ": not found\n");
            return 1;
        }
    } else {
        write_stderr("unalias: missing alias name\n");
        return 1;
    }
    return 0;
}

static int builtin_clear(const vector<string> &, ShellState &) {
    write_stdout("\033[2J\033[H");
    return 0;
}

static int builtin_which(const vector<string> &argv, ShellState &state) {
    if (argv.size() < 2) {
        write_stderr(argv[0] + ": missing argument\n");
        return 1;
    }
    string name = argv[1];
    if (is_builtin(name)) {
        write_stdout(name + ": shell builtin\n");
        return 0;
    }
    if (state.aliases.count(name)) {
        write_stdout(name + " is aliased to '" + state.aliases[name] + "'\n");
        return 0;
    }
    const char *path_env = getenv("PATH");
    if (path_env) {
        string path_str = path_env;
        size_t start = 0;
        while (start < path_str.size()) {
            size_t end = path_str.find(':', start);
            if (end == string::npos) end = path_str.size();
            string dir = path_str.substr(start, end - start);
            string full_path = dir + "/" + name;
            if (access(full_path.c_str(), X_OK) == 0) {
                write_stdout(full_path + "\n");
                return 0;
            }
            start = end + 1;
        }
    }
    write_stderr(name + " not found\n");
    return 1;
}

static int builtin_pushd(const vector<string> &argv, ShellState &state) {
    if (argv.size() < 2) {
        write_stderr("pushd: no directory specified\n");
        return 1;
    }
    char cwd[MAX_SIZE];
    if (getcwd(cwd, MAX_SIZE) == nullptr) {
        write_stderr("pushd: cannot get current directory\n");
        return 1;
    }
    if (chdir(argv[1].c_str()) == -1) {
        write_stderr("pushd: " + argv[1] + ": " + strerror(errno) + "\n");
        return 1;
    }
    state.dir_stack.push_back(string(cwd));
    char new_cwd[MAX_SIZE];
    if (getcwd(new_cwd, MAX_SIZE) != nullptr) {
        string stack_str = string(new_cwd);
        for (int si = (int)state.dir_stack.size() - 1; si >= 0; si--)
            stack_str += " " + state.dir_stack[si];
        write_stdout(stack_str + "\n");
    }
    return 0;
}

static int builtin_popd(const vector<string> &, ShellState &state) {
    if (state.dir_stack.empty()) {
        write_stderr("popd: directory stack empty\n");
        return 1;
    }
    string target = state.dir_stack.back();
    state.dir_stack.pop_back();
    if (chdir(target.c_str()) == -1) {
        write_stderr("popd: " + target + ": " + string(strerror(errno)) + "\n");
        return 1;
    }
    char new_cwd[MAX_SIZE];
    if (getcwd(new_cwd, MAX_SIZE) != nullptr) {
        string stack_str = string(new_cwd);
        for (int si = (int)state.dir_stack.size() - 1; si >= 0; si--)
            stack_str += " " + state.dir_stack[si];
        write_stdout(stack_str + "\n");
    }
    return 0;
}

static int builtin_dirs(const vector<string> &, ShellState &state) {
    char cwd[MAX_SIZE];
    if (getcwd(cwd, MAX_SIZE) != nullptr) {
        string stack_str = string(cwd);
        for (int si = (int)state.dir_stack.size() - 1; si >= 0; si--)
            stack_str += " " + state.dir_stack[si];
        write_stdout(stack_str + "\n");
    }
    return 0;
}

static int builtin_bglist(const vector<string> &, ShellState &state) {
    int i = 0;
    for (auto &process : state.background_processes) {
        ++i;
        stringstream ss;
        ss << "(" << i << ")" << " " << process.second << endl;
        write_stdout(ss.str());
    }
    stringstream ss;
    ss << "Total Background Jobs: " << i << endl;
    write_stdout(ss.str());
    return 0;
}

static pid_t get_nth_background_process(unordered_map<pid_t, string> &background_processes, int n) {
    int i = 1;
    for (auto &process : background_processes) {
        if (i == n) return process.first;
        ++i;
    }
    return -1;
}

static int parse_job_number(const vector<string> &argv, const string &cmd_name) {
    if (argv.size() < 2) {
        write_stderr(cmd_name + ": missing process number\n");
        return -1;
    }
    try {
        return stoi(argv[1]);
    } catch (const invalid_argument&) {
        write_stderr(cmd_name + ": invalid process number\n");
        return -1;
    } catch (const out_of_range&) {
        write_stderr(cmd_name + ": process number out of range\n");
        return -1;
    }
}

static int builtin_bgkill(const vector<string> &argv, ShellState &state) {
    int n = parse_job_number(argv, "bgkill");
    if (n < 0) return 1;
    pid_t pid = get_nth_background_process(state.background_processes, n);
    if (pid == -1) { write_stderr("bgkill: invalid job number\n"); return 1; }
    if (kill(pid, SIGTERM) == -1) { write_stderr(string(strerror(errno)) + "\n"); return 1; }
    return 0;
}

static int builtin_bgstop(const vector<string> &argv, ShellState &state) {
    int n = parse_job_number(argv, "bgstop");
    if (n < 0) return 1;
    pid_t pid = get_nth_background_process(state.background_processes, n);
    if (pid == -1) { write_stderr("bgstop: invalid job number\n"); return 1; }
    if (kill(pid, SIGSTOP) == -1) { write_stderr(string(strerror(errno)) + "\n"); return 1; }
    return 0;
}

static int builtin_bgstart(const vector<string> &argv, ShellState &state) {
    int n = parse_job_number(argv, "bgstart");
    if (n < 0) return 1;
    pid_t pid = get_nth_background_process(state.background_processes, n);
    if (pid == -1) { write_stderr("bgstart: invalid job number\n"); return 1; }
    if (kill(pid, SIGCONT) == -1) { write_stderr(string(strerror(errno)) + "\n"); return 1; }
    return 0;
}

static int builtin_fg(const vector<string> &argv, ShellState &state) {
    if (state.background_processes.empty()) {
        write_stderr("fg: no background jobs\n");
        return 1;
    }
    pid_t pid;
    if (argv.size() < 2) {
        pid = -1;
        for (auto &p : state.background_processes) {
            if (p.first > pid) pid = p.first;
        }
    } else {
        int n;
        try { n = stoi(argv[1]); }
        catch (const invalid_argument&) { write_stderr("fg: invalid job number\n"); return 1; }
        catch (const out_of_range&) { write_stderr("fg: job number out of range\n"); return 1; }
        pid = get_nth_background_process(state.background_processes, n);
        if (pid == -1) { write_stderr("fg: invalid job number\n"); return 1; }
    }
    kill(pid, SIGCONT);
    fg_child_pid = pid;
    int status;
    waitpid(pid, &status, WUNTRACED);
    fg_child_pid = 0;
    state.background_processes.erase(pid);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

static int builtin_history(const vector<string> &, ShellState &) {
    string path = history_file_path();
    if (path.empty()) {
        write_stderr("history: no history file\n");
        return 1;
    }
    ifstream file(path);
    if (!file.is_open()) {
        write_stderr("history: cannot read history\n");
        return 1;
    }
    string line;
    int n = 1;
    while (getline(file, line)) {
        if (!line.empty()) {
            stringstream ss;
            ss << "  " << n << "  " << line << endl;
            write_stdout(ss.str());
            n++;
        }
    }
    return 0;
}

static int builtin_source(const vector<string> &argv, ShellState &state) {
    if (argv.size() < 2) {
        write_stderr("source: missing file argument\n");
        return 1;
    }
    return execute_script_file(argv[1], state);
}

static int builtin_z(const vector<string> &argv, ShellState &state) {
    if (argv.size() < 2) {
        write_stderr("z: missing directory pattern\n");
        return 1;
    }
    string query;
    for (size_t i = 1; i < argv.size(); i++) {
        if (i > 1) query += " ";
        query += argv[i];
    }
    string target = z_find_directory(query);
    if (target.empty()) {
        write_stderr("z: no match for '" + query + "'\n");
        return 1;
    }

    char cwd[MAX_SIZE];
    if (getcwd(cwd, MAX_SIZE) == nullptr) {
        write_stderr("z: cannot get current directory\n");
        return 1;
    }
    if (chdir(target.c_str()) == -1) {
        write_stderr("z: " + target + ": " + strerror(errno) + "\n");
        return 1;
    }
    state.previous_directory = string(cwd);
    z_record_directory(target);
    write_stdout(target + "\n");
    return 0;
}

static int builtin_copy(const vector<string> &argv, ShellState &) {
    string text;
    if (argv.size() > 1) {
        for (size_t i = 1; i < argv.size(); i++) {
            if (i > 1) text += " ";
            text += argv[i];
        }
    } else {
        // Read from stdin until EOF (allows `echo foo | copy`).
        char buf[4096];
        while (true) {
            ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
            if (n <= 0) break;
            text.append(buf, n);
        }
    }
    if (!copy_to_clipboard(text)) {
        write_stderr("copy: failed to write to clipboard\n");
        return 1;
    }
    return 0;
}

static int builtin_paste(const vector<string> &, ShellState &) {
    string text = paste_from_clipboard();
    if (text.empty()) {
        write_stderr("paste: clipboard is empty or unavailable\n");
        return 1;
    }
    if (!text.empty() && text.back() != '\n') text += '\n';
    write_stdout(text);
    return 0;
}

static int builtin_explain(const vector<string> &argv, ShellState &) {
    if (argv.size() < 2) {
        write_stderr("explain: usage: explain <command> [args...]\n");
        return 1;
    }
    const string &cmd = argv[1];
    vector<string> rest(argv.begin() + 2, argv.end());

    string hint = get_command_hint(cmd);
    if (hint.empty()) {
        write_stderr("explain: no entry for '" + cmd + "'\n");
        return 1;
    }
    write_stdout("  " + cmd + string(cmd.size() < 6 ? 6 - cmd.size() : 0, ' ') +
                 "  " + hint + "\n");

    auto flags = explain_command(cmd, rest);
    for (const auto &f : flags) {
        if (f.description.empty()) continue;
        string pad(f.flag.size() < 6 ? 6 - f.flag.size() : 0, ' ');
        write_stdout("  " + f.flag + pad + "  " + f.description + "\n");
    }
    return 0;
}

static string hex6(const RGB &c) {
    char buf[8];
    snprintf(buf, sizeof(buf), "#%02x%02x%02x", c.r, c.g, c.b);
    return buf;
}

static void theme_print_swatch(const string &label, const RGB &c) {
    write_stdout("  " + ansi_fg(c) + "██████" + CAT_RESET + "  " +
                 label + "  " + hex6(c) + "\n");
}

static int builtin_theme(const vector<string> &argv, ShellState &) {
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

// ── Dispatch table ─────────────────────────────────────────────

const unordered_map<string, BuiltinFn>& get_builtins() {
    static const unordered_map<string, BuiltinFn> builtins = {
        {"cd",       builtin_cd},
        {"pwd",      builtin_pwd},
        {"exit",     builtin_exit},
        {"export",   builtin_export},
        {"unset",    builtin_unset},
        {"alias",    builtin_alias},
        {"unalias",  builtin_unalias},
        {"clear",    builtin_clear},
        {"which",    builtin_which},
        {"type",     builtin_which},
        {"pushd",    builtin_pushd},
        {"popd",     builtin_popd},
        {"dirs",     builtin_dirs},
        {"bglist",   builtin_bglist},
        {"bgkill",   builtin_bgkill},
        {"bgstop",   builtin_bgstop},
        {"bgstart",  builtin_bgstart},
        {"fg",       builtin_fg},
        {"history",  builtin_history},
        {"source",   builtin_source},
        {".",        builtin_source},
        {"z",        builtin_z},
        {"theme",    builtin_theme},
        {"explain",  builtin_explain},
        {"copy",     builtin_copy},
        {"paste",    builtin_paste},
    };
    return builtins;
}

bool is_builtin(const string &name) {
    return get_builtins().count(name) > 0;
}
