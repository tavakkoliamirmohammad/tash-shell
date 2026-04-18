// History builtin.

#include "tash/builtins.h"
#include "tash/core/signals.h"
#include "tash/history.h"

#include <fstream>
#include <sstream>

using namespace std;

int builtin_history(const vector<string> &, ShellState &) {
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
