#include "shell.h"

string write_shell_prefix() {
    stringstream ss;
    gethostname(hostname, MAX_SIZE);
    char cwd[MAX_SIZE];
    getcwd(cwd, MAX_SIZE);
    ss << bold(red("\u21aa ")) << bold(green(getlogin())) << bold(cyan("@")) << bold(green(hostname)) << " "
       << bold(cyan(regex_replace(string(cwd), regex(string(getenv("HOME"))), "~")))
       << bold(yellow(" shell> "));
    return ss.str();
}
