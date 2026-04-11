#include "shell.h"

string get_git_branch() {
    FILE *fp = popen("git rev-parse --abbrev-ref HEAD 2>/dev/null", "r");
    if (!fp) return "";
    char buf[256];
    string branch;
    if (fgets(buf, sizeof(buf), fp)) {
        branch = buf;
        // strip trailing newline
        if (!branch.empty() && branch.back() == '\n') branch.pop_back();
    }
    pclose(fp);
    return branch;
}

string write_shell_prefix() {
    stringstream ss;
    gethostname(hostname, MAX_SIZE);
    char cwd[MAX_SIZE];
    getcwd(cwd, MAX_SIZE);
    const char *login = getlogin();
    const char *home = getenv("HOME");
    string user = login ? login : "user";
    string cwd_display = string(cwd);
    if (home) {
        cwd_display = regex_replace(cwd_display, regex(string(home)), "~");
    }
    string branch = get_git_branch();
    // Only use colored prompt when stdout is a terminal.
    // Plain prompt avoids a known heap-buffer-overflow in GNU readline's
    // rl_redisplay when processing ANSI escape sequences non-interactively.
    if (isatty(STDOUT_FILENO)) {
        ss << bold(red("\u21aa ")) << bold(green(user)) << bold(cyan("@")) << bold(green(hostname)) << " "
           << bold(cyan(cwd_display));
        if (!branch.empty()) {
            ss << " " << bold(magenta("(" + branch + ")"));
        }
        ss << bold(yellow(" shell> "));
    } else {
        ss << user << "@" << hostname << " " << cwd_display;
        if (!branch.empty()) {
            ss << " (" << branch << ")";
        }
        ss << " shell> ";
    }
    return ss.str();
}
