#include "tash/core.h"
#include "tash/plugin.h"
#include "tash/ui.h"
#include "tash/util/safe_exec.h"
#include "theme.h"

#include <sstream>
#include <sys/ioctl.h>

using namespace std;

string get_git_branch() {
    // 500ms timeout is plenty for a local repo and short enough not to
    // stall the prompt when cwd sits on a network mount that hangs.
    // suppress_stderr=true: in a non-repo cwd git writes
    // "fatal: not a git repository" to stderr — we must not leak that
    // to the terminal on every prompt render.
    auto r = tash::util::safe_exec(
        {"git", "rev-parse", "--abbrev-ref", "HEAD"}, 500,
        /*suppress_stderr=*/true);
    if (r.exit_code != 0) return "";
    string branch = r.stdout_text;
    while (!branch.empty() && (branch.back() == '\n' || branch.back() == '\r')) {
        branch.pop_back();
    }
    return branch;
}

string get_git_status_indicators() {
    auto r = tash::util::safe_exec(
        {"git", "status", "--porcelain"}, 500,
        /*suppress_stderr=*/true);
    if (r.exit_code < 0) return "";
    bool has_staged = false, has_unstaged = false, has_untracked = false;
    const string &out = r.stdout_text;
    size_t pos = 0;
    while (pos < out.size()) {
        size_t nl = out.find('\n', pos);
        size_t len = (nl == string::npos ? out.size() : nl) - pos;
        if (len >= 1) {
            char c0 = out[pos];
            if (c0 == '?') has_untracked = true;
            else if (c0 != ' ' && c0 != '?') has_staged = true;
        }
        if (len >= 2) {
            char c1 = out[pos + 1];
            if (c1 == 'M' || c1 == 'D') has_unstaged = true;
        }
        if (nl == string::npos) break;
        pos = nl + 1;
    }

    string indicators;
    if (has_staged) indicators += "+";
    if (has_unstaged) indicators += "*";
    if (has_untracked) indicators += "?";
    return indicators;
}

void set_terminal_title(const string &title) {
    if (isatty(STDOUT_FILENO)) {
        string seq = "\033]0;" + title + "\007";
        if (write(STDOUT_FILENO, seq.c_str(), seq.size())) {}
    }
}

static string short_name(const string &full) {
    for (size_t i = 0; i < full.size(); i++) {
        if (full[i] == '.' || full[i] == '_' || full[i] == '-') {
            if (i > 0) return full.substr(0, i);
        }
    }
    if (full.size() > 12) return full.substr(0, 8);
    return full;
}

static string format_duration(double seconds) {
    if (seconds < 60) {
        stringstream ss;
        ss << fixed;
        ss.precision(1);
        ss << seconds << "s";
        return ss.str();
    } else if (seconds < 3600) {
        int mins = (int)(seconds / 60);
        int secs = (int)seconds % 60;
        return to_string(mins) + "m" + to_string(secs) + "s";
    } else {
        int hrs = (int)(seconds / 3600);
        int mins = ((int)seconds % 3600) / 60;
        return to_string(hrs) + "h" + to_string(mins) + "m";
    }
}

string write_shell_prefix(const ShellState &state) {
    // Let a registered prompt provider (e.g. Starship) override the builtin
    // prompt. Empty result means "fall through to builtin".
    {
        std::string custom = global_plugin_registry().render_prompt(state);
        if (!custom.empty()) {
            set_terminal_title("tash");
            return custom;
        }
    }

    char cwd[MAX_SIZE];
    if (!getcwd(cwd, MAX_SIZE)) cwd[0] = '\0';
    const char *login = getlogin();
    const char *home = getenv("HOME");
    string user = login ? short_name(string(login)) : "user";
    string cwd_display = string(cwd);
    if (home) {
        string home_str(home);
        size_t pos = cwd_display.find(home_str);
        if (pos == 0) {
            cwd_display = "~" + cwd_display.substr(home_str.size());
        }
    }
    string branch = get_git_branch();

    set_terminal_title("tash: " + cwd_display);

    if (isatty(STDOUT_FILENO)) {
        // Catppuccin Mocha colored prompt — first line written to stdout
        string line1;
        line1 += PROMPT_SEPARATOR + "\u256d\u2500 " CAT_RESET;
        line1 += PROMPT_USER + user + CAT_RESET;
        line1 += PROMPT_TEXT + " in " CAT_RESET;
        line1 += PROMPT_PATH + cwd_display + CAT_RESET;
        if (!branch.empty()) {
            line1 += PROMPT_TEXT + " on " CAT_RESET;
            line1 += PROMPT_BRANCH + "\ue0a0 " + branch + CAT_RESET;

            string git_indicators = get_git_status_indicators();
            if (!git_indicators.empty()) {
                line1 += PROMPT_GIT_DIRTY + " [" + git_indicators + "]" CAT_RESET;
            }
        }

        if (state.last_cmd_duration >= 0.5) {
            line1 += PROMPT_DURATION + " took " + format_duration(state.last_cmd_duration) + CAT_RESET;
        }

        line1 += "\n";
        write_stdout(line1);

        // Second line — the actual prompt passed to replxx
        string arrow_color = (state.last_exit_status == 0) ? PROMPT_ARROW_OK : PROMPT_ARROW_ERR;
        return PROMPT_SEPARATOR + "\u2570\u2500" CAT_RESET
               + arrow_color + "\u276f " CAT_RESET;
    } else {
        stringstream ss;
        ss << user << " " << cwd_display;
        if (!branch.empty()) {
            ss << " (" << branch << ")";
        }
        ss << " > ";
        return ss.str();
    }
}
