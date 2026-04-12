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
        // Use combined ANSI codes in single \001...\002 blocks
        // so readline/libedit can correctly calculate prompt width.
        // Nesting bold(cyan(...)) produces adjacent \002\001 pairs
        // that confuse macOS libedit.
        #define P_RST   "\001\e[0m\002"
        #define P_BCYAN "\001\e[1;36m\002"
        #define P_BGRN  "\001\e[1;32m\002"
        #define P_BWHT  "\001\e[1;37m\002"
        #define P_BMAG  "\001\e[1;35m\002"
        #define P_BYEL  "\001\e[1;33m\002"

        ss << P_RST;
        ss << P_BCYAN "\u256d\u2500 " P_RST;
        ss << P_BGRN << user << P_RST;
        ss << P_BWHT " in " P_RST;
        ss << P_BCYAN << cwd_display << P_RST;
        if (!branch.empty()) {
            ss << P_BWHT " on " P_RST;
            ss << P_BMAG "\ue0a0 " << branch << P_RST;
        }
        ss << "\n";
        ss << P_BCYAN "\u2570\u2500" P_RST P_BYEL "\u276f " P_RST;

        #undef P_RST
        #undef P_BCYAN
        #undef P_BGRN
        #undef P_BWHT
        #undef P_BMAG
        #undef P_BYEL
    } else {
        ss << user << " " << cwd_display;
        if (!branch.empty()) {
            ss << " (" << branch << ")";
        }
        ss << " > ";
    }
    return ss.str();
}
