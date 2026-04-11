#include "shell.h"

string &rtrim(string &s, const char *t) {
    return s.erase(s.find_last_not_of(t) + 1);
}

string &ltrim(string &s, const char *t) {
    return s.erase(0, s.find_first_not_of(t));
}

string &trim(string &s, const char *t) {
    return ltrim(rtrim(s, t), t);
}

vector<string> tokenize_string(string line, const string &delimiter) {
    string regularExpression = delimiter + R"((?=(?:[^\"]*\"[^\"]*\")*[^\"]*$))";
    regex e(regularExpression);
    regex_token_iterator<string::iterator> it(line.begin(), line.end(), e, -1);
    vector<string> commands{it, {}};
    commands.erase(
            remove_if(
                    commands.begin(), commands.end(),
                    [](const string& c){ return c.empty();}),
            commands.end());;
    for (string &command : commands) {
        command = trim(command);
        if (command[0] == '~') {
            command = regex_replace(command, regex("~"), string(getenv("HOME")));
        }
    }
    return commands;
}
