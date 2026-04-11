#include "shell.h"

string &rtrim(std::string &s, const char *t) {
    return s.erase(s.find_last_not_of(t) + 1);
}

string &ltrim(std::string &s, const char *t) {
    return s.erase(0, s.find_first_not_of(t));
}

string &trim(std::string &s, const char *t) {
    return ltrim(rtrim(s, t), t);
}

vector<string> tokenize_string(string line, const string &delimiter) {
    vector<string> tokens;
    string current;
    bool in_double_quotes = false;
    bool in_single_quotes = false;
    size_t i = 0;
    size_t len = line.size();
    size_t dlen = delimiter.size();

    while (i < len) {
        if (i + 1 < len && line[i] == '\\' && (line[i + 1] == '"' || line[i + 1] == '\'' || line[i + 1] == '\\')) {
            current += line[i + 1];
            i += 2;
            continue;
        }
        if (line[i] == '"' && !in_single_quotes) {
            in_double_quotes = !in_double_quotes;
            current += line[i];
            ++i;
            continue;
        }
        if (line[i] == '\'' && !in_double_quotes) {
            in_single_quotes = !in_single_quotes;
            current += line[i];
            ++i;
            continue;
        }
        if (!in_double_quotes && !in_single_quotes &&
            i + dlen <= len && line.compare(i, dlen, delimiter) == 0) {
            string token = current;
            token = trim(token);
            if (!token.empty()) {
                tokens.push_back(token);
            }
            current.clear();
            i += dlen;
            continue;
        }
        current += line[i];
        ++i;
    }

    string token = current;
    token = trim(token);
    if (!token.empty()) {
        tokens.push_back(token);
    }

    const char *home = getenv("HOME");
    if (home) {
        for (string &t : tokens) {
            if (!t.empty() && t[0] == '~') {
                t = string(home) + t.substr(1);
            }
        }
    }

    return tokens;
}

string expand_variables(const string &input) {
    string result;
    size_t i = 0;
    while (i < input.size()) {
        if (input[i] == '$') {
            i++;
            if (i < input.size() && input[i] == '{') {
                // ${VAR} syntax
                i++; // skip '{'
                string var_name;
                while (i < input.size() && input[i] != '}') {
                    var_name += input[i];
                    i++;
                }
                if (i < input.size()) {
                    i++; // skip '}'
                }
                const char *val = getenv(var_name.c_str());
                if (val) {
                    result += val;
                }
            } else {
                // $VAR syntax: read alphanumeric + underscore
                string var_name;
                while (i < input.size() && (isalnum(input[i]) || input[i] == '_')) {
                    var_name += input[i];
                    i++;
                }
                if (!var_name.empty()) {
                    const char *val = getenv(var_name.c_str());
                    if (val) {
                        result += val;
                    }
                } else {
                    // lone '$' with no valid var name following
                    result += '$';
                }
            }
        } else {
            result += input[i];
            i++;
        }
    }
    return result;
}

vector<string> expand_globs(const vector<string> &args) {
    vector<string> expanded;
    for (const string &arg : args) {
        if (arg.find_first_of("*?[") != string::npos) {
            glob_t glob_result;
            int ret = glob(arg.c_str(), GLOB_NOCHECK | GLOB_TILDE, nullptr, &glob_result);
            if (ret == 0) {
                for (size_t i = 0; i < glob_result.gl_pathc; i++) {
                    expanded.push_back(glob_result.gl_pathv[i]);
                }
            } else {
                expanded.push_back(arg);
            }
            globfree(&glob_result);
        } else {
            expanded.push_back(arg);
        }
    }
    return expanded;
}

string strip_quotes(const string &s) {
    if (s.size() >= 2) {
        if ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')) {
            return s.substr(1, s.size() - 2);
        }
    }
    return s;
}

vector<CommandSegment> parse_command_line(const string &line) {
    vector<CommandSegment> segments;
    string current;
    bool in_quotes = false;
    size_t i = 0;
    OperatorType next_op = OP_NONE;

    while (i < line.size()) {
        char c = line[i];
        if (c == '"') {
            in_quotes = !in_quotes;
            current += c;
            ++i;
        } else if (!in_quotes && c == '&' && i + 1 < line.size() && line[i + 1] == '&') {
            string cmd = current;
            cmd = trim(cmd);
            if (!cmd.empty()) segments.push_back({cmd, next_op});
            next_op = OP_AND;
            current.clear();
            i += 2;
        } else if (!in_quotes && c == '|' && i + 1 < line.size() && line[i + 1] == '|') {
            string cmd = current;
            cmd = trim(cmd);
            if (!cmd.empty()) segments.push_back({cmd, next_op});
            next_op = OP_OR;
            current.clear();
            i += 2;
        } else if (!in_quotes && c == ';') {
            string cmd = current;
            cmd = trim(cmd);
            if (!cmd.empty()) segments.push_back({cmd, next_op});
            next_op = OP_SEMICOLON;
            current.clear();
            ++i;
        } else {
            current += c;
            ++i;
        }
    }
    string cmd = current;
    cmd = trim(cmd);
    if (!cmd.empty()) segments.push_back({cmd, next_op});
    return segments;
}
