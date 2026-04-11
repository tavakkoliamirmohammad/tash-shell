#include "shell.h"

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
    ss << bold(red("\u21aa ")) << bold(green(user)) << bold(cyan("@")) << bold(green(hostname)) << " "
       << bold(cyan(cwd_display))
       << bold(yellow(" shell> "));
    return ss.str();
}
