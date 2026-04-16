#include "tash/core.h"
#include "tash/ui.h"
#include "theme.h"
#include <sys/ioctl.h>

using namespace std;

string get_git_branch() {
    FILE *pipe = popen("git rev-parse --abbrev-ref HEAD 2>/dev/null", "r");
    if (!pipe) return "";
    char buffer[256];
    string branch;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        branch += buffer;
    }
    int status = pclose(pipe);
    if (status != 0) return "";
    while (!branch.empty() && (branch.back() == '\n' || branch.back() == '\r')) {
        branch.pop_back();
    }
    return branch;
}

string get_git_status_indicators() {
    FILE *pipe = popen("git status --porcelain 2>/dev/null", "r");
    if (!pipe) return "";
    char buffer[512];
    bool has_staged = false, has_unstaged = false, has_untracked = false;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        if (buffer[0] == '?') has_untracked = true;
        else if (buffer[0] != ' ' && buffer[0] != '?') has_staged = true;
        if (buffer[1] == 'M' || buffer[1] == 'D') has_unstaged = true;
    }
    pclose(pipe);

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
