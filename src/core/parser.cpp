#include "tash/core.h"

using namespace std;

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

    return tokens;
}

string expand_tilde(const string &token) {
    if (!token.empty() && token[0] == '~') {
        const char *home = getenv("HOME");
        if (home) {
            return string(home) + token.substr(1);
        }
    }
    return token;
}

string expand_variables(const string &input, int last_exit_status) {
    string result;
    bool in_single_quotes = false;
    size_t i = 0;
    while (i < input.size()) {
        if (input[i] == '\'' && !in_single_quotes) {
            in_single_quotes = true;
            result += input[i];
            i++;
            continue;
        }
        if (input[i] == '\'' && in_single_quotes) {
            in_single_quotes = false;
            result += input[i];
            i++;
            continue;
        }
        if (in_single_quotes) {
            result += input[i];
            i++;
            continue;
        }
        if (input[i] == '$') {
            i++;
            if (i < input.size() && input[i] == '?') {
                result += to_string(last_exit_status);
                i++;
                continue;
            }
            if (i < input.size() && input[i] == '$') {
                result += to_string(getpid());
                i++;
                continue;
            }
            if (i < input.size() && input[i] == '{') {
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

string expand_command_substitution(const string &input) {
    string result;
    size_t i = 0;
    while (i < input.size()) {
        if (i + 1 < input.size() && input[i] == '$' && input[i + 1] == '(') {
            size_t start = i + 2;
            int depth = 1;
            size_t j = start;
            while (j < input.size() && depth > 0) {
                if (input[j] == '(') {
                    depth++;
                } else if (input[j] == ')') {
                    depth--;
                }
                if (depth > 0) {
                    j++;
                }
            }
            if (depth == 0) {
                string cmd = input.substr(start, j - start);
                string output;
                FILE *pipe = popen(cmd.c_str(), "r");
                if (pipe) {
                    char buffer[256];
                    while (fgets(buffer, sizeof(buffer), pipe)) {
                        output += buffer;
                    }
                    pclose(pipe);
                }
                while (!output.empty() && output.back() == '\n') {
                    output.pop_back();
                }
                result += output;
                i = j + 1;
            } else {
                result += input[i];
                i++;
            }
        } else {
            result += input[i];
            i++;
        }
    }
    return result;
}

vector<string> expand_globs(const vector<string> &args,
                             const vector<bool> &quoted) {
    vector<string> expanded;
    for (size_t idx = 0; idx < args.size(); ++idx) {
        const string &arg = args[idx];
        bool is_quoted = (idx < quoted.size()) ? quoted[idx] : false;
        if (!is_quoted && arg.find_first_of("*?[") != string::npos) {
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

vector<string> expand_globs(const vector<string> &args) {
    // Back-compat overload for callers that have no quote information.
    return expand_globs(args, vector<bool>{});
}

string strip_quotes(const string &s) {
    if (s.size() >= 2) {
        if ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')) {
            return s.substr(1, s.size() - 2);
        }
    }
    return s;
}

string expand_history_bang(const string &line, replxx::Replxx &rx) {
    string trimmed = line;
    trimmed = trim(trimmed);

    // Collect history entries into a vector for indexed access
    vector<string> hist_entries;
    {
        replxx::Replxx::HistoryScan hs(rx.history_scan());
        while (hs.next()) {
            replxx::Replxx::HistoryEntry he(hs.get());
            hist_entries.push_back(he.text());
        }
    }

    // hist_entries is oldest-first (history_scan iterates chronologically)
    if (trimmed == "!!") {
        if (hist_entries.empty()) {
            write_stderr("tash: !!: event not found\n");
            return "";
        }
        return hist_entries.back();  // most recent = last element
    }

    if (trimmed.size() >= 2 && trimmed[0] == '!') {
        string num_str = trimmed.substr(1);
        bool all_digits = true;
        for (size_t i = 0; i < num_str.size(); i++) {
            if (!isdigit(num_str[i])) { all_digits = false; break; }
        }
        if (all_digits && !num_str.empty()) {
            int n = stoi(num_str);
            // !1 = first command = hist_entries[0] (1-based)
            int idx = n - 1;
            if (idx < 0 || idx >= (int)hist_entries.size()) {
                write_stderr("tash: !" + num_str + ": event not found\n");
                return "";
            }
            return hist_entries[idx];
        }
    }

    return line;
}

Command parse_redirections(const string &command_str) {
    return parse_redirections(command_str, nullptr);
}

Command parse_redirections(const string &command_str,
                           vector<PendingHeredoc> *bodies) {
    Command cmd;
    string remaining;
    bool in_double_quotes = false;
    bool in_single_quotes = false;
    size_t i = 0;
    size_t body_index = 0;

    while (i < command_str.size()) {
        char c = command_str[i];

        if (c == '"' && !in_single_quotes) {
            in_double_quotes = !in_double_quotes;
            remaining += c;
            ++i;
        } else if (c == '\'' && !in_double_quotes) {
            in_single_quotes = !in_single_quotes;
            remaining += c;
            ++i;
        } else if (!in_double_quotes && !in_single_quotes) {
            // Check for 2>&1
            if (i + 4 <= command_str.size() && command_str.compare(i, 4, "2>&1") == 0) {
                cmd.redirections.push_back({2, "", false, true});
                i += 4;
            }
            // Check for 2>
            else if (i + 2 <= command_str.size() && command_str.compare(i, 2, "2>") == 0) {
                i += 2;
                while (i < command_str.size() && command_str[i] == ' ') ++i;
                string fname;
                while (i < command_str.size() && command_str[i] != ' ' && command_str[i] != '\t') {
                    fname += command_str[i];
                    ++i;
                }
                fname = trim(fname);
                if (!fname.empty()) {
                    cmd.redirections.push_back({2, fname, false, false});
                }
            }
            // Check for >>
            else if (i + 2 <= command_str.size() && command_str.compare(i, 2, ">>") == 0) {
                i += 2;
                while (i < command_str.size() && command_str[i] == ' ') ++i;
                string fname;
                while (i < command_str.size() && command_str[i] != ' ' && command_str[i] != '\t') {
                    fname += command_str[i];
                    ++i;
                }
                fname = trim(fname);
                if (!fname.empty()) {
                    cmd.redirections.push_back({1, fname, true, false});
                }
            }
            // Check for > (must be after >>)
            else if (c == '>') {
                i += 1;
                while (i < command_str.size() && command_str[i] == ' ') ++i;
                string fname;
                while (i < command_str.size() && command_str[i] != ' ' && command_str[i] != '\t') {
                    fname += command_str[i];
                    ++i;
                }
                fname = trim(fname);
                if (!fname.empty()) {
                    cmd.redirections.push_back({1, fname, false, false});
                }
            }
            // Check for << (heredoc). Must precede the plain `<` branch.
            else if (i + 2 <= command_str.size() && command_str.compare(i, 2, "<<") == 0) {
                i += 2;
                bool strip_tabs = false;
                if (i < command_str.size() && command_str[i] == '-') {
                    strip_tabs = true;
                    ++i;
                }
                while (i < command_str.size() &&
                       (command_str[i] == ' ' || command_str[i] == '\t')) ++i;

                // Capture the delimiter token. Quoted delim (single or
                // double) disables variable/command expansion in the body.
                bool expand = true;
                std::string delim;
                if (i < command_str.size() &&
                    (command_str[i] == '\'' || command_str[i] == '"')) {
                    char q = command_str[i++];
                    expand = false;
                    while (i < command_str.size() && command_str[i] != q) {
                        delim += command_str[i++];
                    }
                    if (i < command_str.size()) ++i; // skip closing quote
                } else {
                    while (i < command_str.size() &&
                           command_str[i] != ' ' && command_str[i] != '\t' &&
                           command_str[i] != '|' && command_str[i] != '>' &&
                           command_str[i] != '<') {
                        delim += command_str[i++];
                    }
                }

                Redirection r;
                r.fd = 0;
                r.append = false;
                r.dup_to_stdout = false;
                r.is_heredoc = true;
                r.heredoc_delim = delim;
                r.heredoc_strip_tabs = strip_tabs;
                r.heredoc_expand = expand;

                // Pull the already-collected body (filled by the REPL /
                // script reader). Matched by appearance order.
                if (bodies && body_index < bodies->size()) {
                    r.heredoc_body = (*bodies)[body_index].body;
                    ++body_index;
                }
                cmd.redirections.push_back(r);
            }
            // Check for <
            else if (c == '<') {
                i += 1;
                while (i < command_str.size() && command_str[i] == ' ') ++i;
                string fname;
                while (i < command_str.size() && command_str[i] != ' ' && command_str[i] != '\t') {
                    fname += command_str[i];
                    ++i;
                }
                fname = trim(fname);
                if (!fname.empty()) {
                    cmd.redirections.push_back({0, fname, false, false});
                }
            }
            else {
                remaining += c;
                ++i;
            }
        } else {
            remaining += c;
            ++i;
        }
    }

    // Tokenize the command body, then record whether each token was
    // originally enclosed in quotes so expand_globs can skip it. Also
    // un-escape any backslash-escaped glob metacharacters and mark the
    // token quoted (literal) when such escapes are present.
    vector<string> tokens = tokenize_string(remaining, " ");
    cmd.argv.reserve(tokens.size());
    cmd.argv_quoted.reserve(tokens.size());
    for (string &t : tokens) {
        bool was_quoted = false;
        if (t.size() >= 2 &&
            ((t.front() == '"' && t.back() == '"') ||
             (t.front() == '\'' && t.back() == '\''))) {
            was_quoted = true;
        }
        t = expand_tilde(t);
        t = strip_quotes(t);
        // Un-escape `\*` / `\?` / `\[` so the character stays literal
        // through expand_globs.
        string unescaped;
        unescaped.reserve(t.size());
        for (size_t k = 0; k < t.size(); ++k) {
            if (t[k] == '\\' && k + 1 < t.size() &&
                (t[k + 1] == '*' || t[k + 1] == '?' ||
                 t[k + 1] == '[' || t[k + 1] == '\\')) {
                unescaped += t[k + 1];
                was_quoted = true;   // treat escaped-metachar as literal
                ++k;
            } else {
                unescaped += t[k];
            }
        }
        cmd.argv.push_back(unescaped);
        cmd.argv_quoted.push_back(was_quoted);
    }

    return cmd;
}

vector<CommandSegment> parse_command_line(const string &line) {
    // Strip comments: everything after '#' outside quotes is ignored
    string stripped = line;
    {
        bool in_single = false;
        bool in_double = false;
        for (size_t j = 0; j < stripped.size(); j++) {
            if (stripped[j] == '\'' && !in_double) {
                in_single = !in_single;
            } else if (stripped[j] == '"' && !in_single) {
                in_double = !in_double;
            } else if (stripped[j] == '#' && !in_single && !in_double) {
                stripped = stripped.substr(0, j);
                break;
            }
        }
    }

    vector<CommandSegment> segments;
    string current;
    bool in_double_quotes = false;
    bool in_single_quotes = false;
    size_t i = 0;
    OperatorType next_op = OP_NONE;

    while (i < stripped.size()) {
        char c = stripped[i];
        if (c == '"' && !in_single_quotes) {
            in_double_quotes = !in_double_quotes;
            current += c;
            ++i;
        } else if (c == '\'' && !in_double_quotes) {
            in_single_quotes = !in_single_quotes;
            current += c;
            ++i;
        } else if (!in_double_quotes && !in_single_quotes && c == '&' && i + 1 < stripped.size() && stripped[i + 1] == '&') {
            string cmd = current;
            cmd = trim(cmd);
            if (!cmd.empty()) segments.push_back({cmd, next_op});
            next_op = OP_AND;
            current.clear();
            i += 2;
        } else if (!in_double_quotes && !in_single_quotes && c == '|' && i + 1 < stripped.size() && stripped[i + 1] == '|') {
            string cmd = current;
            cmd = trim(cmd);
            if (!cmd.empty()) segments.push_back({cmd, next_op});
            next_op = OP_OR;
            current.clear();
            i += 2;
        } else if (!in_double_quotes && !in_single_quotes && c == ';') {
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

bool is_input_complete(const string &input) {
    bool in_single = false;
    bool in_double = false;

    for (size_t i = 0; i < input.size(); i++) {
        char c = input[i];
        if (c == '\\' && i + 1 < input.size()) {
            i++; // skip escaped char
            continue;
        }
        if (c == '\'' && !in_double) in_single = !in_single;
        else if (c == '"' && !in_single) in_double = !in_double;
    }

    // Unclosed quotes
    if (in_single || in_double) return false;

    // Trailing pipe or operator
    string trimmed = input;
    trimmed = trim(trimmed);
    if (!trimmed.empty()) {
        char last = trimmed.back();
        if (last == '|' || last == '\\') return false;
        if (trimmed.size() >= 2) {
            string last2 = trimmed.substr(trimmed.size() - 2);
            if (last2 == "&&" || last2 == "||") return false;
        }
    }

    return true;
}

// ── Heredoc helpers ────────────────────────────────────────────
//
// Scan a command line for `<<` / `<<-` markers at top level (outside
// quotes) and return one PendingHeredoc entry per marker, in order.
// Bodies stay empty; the caller fills them from the input stream.
vector<PendingHeredoc> scan_pending_heredocs(const string &line) {
    vector<PendingHeredoc> pending;
    bool in_double = false, in_single = false;
    size_t i = 0;
    while (i < line.size()) {
        char c = line[i];
        if (c == '"' && !in_single) { in_double = !in_double; ++i; continue; }
        if (c == '\'' && !in_double) { in_single = !in_single; ++i; continue; }
        if (!in_single && !in_double &&
            i + 2 <= line.size() && line.compare(i, 2, "<<") == 0) {
            i += 2;
            PendingHeredoc h;
            if (i < line.size() && line[i] == '-') {
                h.strip_tabs = true;
                ++i;
            }
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
            if (i < line.size() && (line[i] == '\'' || line[i] == '"')) {
                char q = line[i++];
                h.expand = false;
                while (i < line.size() && line[i] != q) h.delim += line[i++];
                if (i < line.size()) ++i;
            } else {
                while (i < line.size() &&
                       line[i] != ' ' && line[i] != '\t' &&
                       line[i] != '|' && line[i] != '>' && line[i] != '<') {
                    h.delim += line[i++];
                }
            }
            if (!h.delim.empty()) pending.push_back(h);
            continue;
        }
        ++i;
    }
    return pending;
}

bool collect_heredoc_bodies(vector<PendingHeredoc> &pending,
                            const std::function<bool(string&)> &read_line) {
    for (PendingHeredoc &h : pending) {
        string body;
        string raw;
        while (true) {
            if (!read_line(raw)) return false;
            // Strip leading tabs when `<<-` form. Applies to both the
            // body lines and the terminating-delimiter line.
            string test = raw;
            if (h.strip_tabs) {
                size_t k = 0;
                while (k < test.size() && test[k] == '\t') ++k;
                test = test.substr(k);
            }
            if (test == h.delim) break;
            body += test;
            body += '\n';
        }
        h.body = body;
    }
    return true;
}
