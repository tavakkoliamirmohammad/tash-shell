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
    stringstream ss;
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
        ss << "\001\e[0m\002";  // reset
        ss << bold(cyan("\u256d\u2500 "));
        ss << bold(green(user));
        ss << bold(white(" in "));
        ss << bold(cyan(cwd_display));
        if (!branch.empty()) {
            ss << bold(white(" on "));
            ss << bold(magenta("\ue0a0 " + branch));
        }
        ss << "\n";
        ss << bold(cyan("\u2570\u2500")) << bold(yellow("\u276f "));
    } else {
        ss << user << " " << cwd_display;
        if (!branch.empty()) {
            ss << " (" << branch << ")";
        }
        ss << " > ";
    }
    return ss.str();
}
