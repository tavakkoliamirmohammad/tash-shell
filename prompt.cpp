#include "shell.h"

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

static string short_name(const string &full) {
    for (size_t i = 0; i < full.size(); i++) {
        if (full[i] == '.' || full[i] == '_' || full[i] == '-') {
            if (i > 0) return full.substr(0, i);
        }
    }
    if (full.size() > 12) return full.substr(0, 8);
    return full;
}

string write_shell_prefix() {
    char cwd[MAX_SIZE];
    getcwd(cwd, MAX_SIZE);
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

    if (isatty(STDOUT_FILENO)) {
        // Write the info line directly to stdout so colors work reliably
        // on both GNU readline and macOS libedit (which may not honor
        // \001/\002 non-printing markers in the prompt string).
        string line1;
        line1 += "\033[1;36m\u256d\u2500 \033[0m";
        line1 += "\033[1;32m" + user + "\033[0m";
        line1 += "\033[1;37m in \033[0m";
        line1 += "\033[1;36m" + cwd_display + "\033[0m";
        if (!branch.empty()) {
            line1 += "\033[1;37m on \033[0m";
            line1 += "\033[1;35m\ue0a0 " + branch + "\033[0m";
        }
        line1 += "\n";
        write_stdout(line1);

        // Only the second line goes to readline as the actual prompt.
        // Use \001/\002 markers here for readline's width calculation.
        return "\001\033[1;36m\002\u2570\u2500\001\033[0m\002"
               "\001\033[1;33m\002\u276f \001\033[0m\002";
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
