#include "tash/core/executor.h"
#include "tash/core/parser.h"
#include "tash/util/io.h"
#include "tash/util/limits.h"
#include "tash/util/parse_error.h"
#include "tash/util/quote_state.h"

#include <cstdlib>
#include <cstring>
#include <glob.h>
#include <regex>
#include <sstream>

using namespace std;

// Emit a cap-exceeded diagnostic exactly once per invocation path.
// Keeping the message consistent makes the test assertions easy.
static void expansion_cap_error(const char *what) {
    std::string msg = what;
    msg += " exceeds maximum size; aborting expansion";
    tash::io::error(msg);
}

// ── Parse-error helpers ───────────────────────────────────────

namespace tash::parse {

void offset_to_line_col(std::string_view input, size_t offset,
                        size_t &line_out, size_t &column_out) {
    if (offset > input.size()) offset = input.size();
    size_t line = 1;
    size_t col = 1;
    for (size_t i = 0; i < offset; ++i) {
        if (input[i] == '\n') {
            ++line;
            col = 1;
        } else {
            ++col;
        }
    }
    line_out = line;
    column_out = col;
}

void emit_parse_error(const ParseError &err) {
    // io::error already prefixes "tash: error: "; we append the compiler-
    // style position suffix "LINE:COL: MSG" so the final line reads like
    //   tash: error: 1:7: unmatched '(' in subshell
    // which is close enough to the bash/zsh convention for editor tools
    // to parse. Column 0 is elided when we don't have a precise offset.
    std::string body;
    body.reserve(err.message.size() + 32);
    body += std::to_string(err.line);
    body += ':';
    if (err.column > 0) {
        body += std::to_string(err.column);
        body += ": ";
    } else {
        body += ' ';
    }
    body += err.message;
    tash::io::error(body);
}

} // namespace tash::parse

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
    tash::util::QuoteState qs;
    size_t i = 0;
    size_t len = line.size();
    size_t dlen = delimiter.size();

    while (i < len) {
        if (i + 1 < len && line[i] == '\\' && (line[i + 1] == '"' || line[i + 1] == '\'' || line[i + 1] == '\\')) {
            current += line[i + 1];
            i += 2;
            continue;
        }
        if (qs.consume(line[i])) {
            current += line[i];
            ++i;
            continue;
        }
        if (!qs.any_active() &&
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
    // Cap check helper: if appending `add` would push us past the
    // limit, emit the diagnostic and bail out with an empty string.
    auto cap_exceeded = [&](size_t add) {
        if (result.size() + add > tash::util::TASH_MAX_EXPANSION_BYTES) {
            expansion_cap_error("variable expansion");
            result.clear();
            return true;
        }
        return false;
    };
    while (i < input.size()) {
        if (input[i] == '\'' && !in_single_quotes) {
            in_single_quotes = true;
            if (cap_exceeded(1)) return result;
            result += input[i];
            i++;
            continue;
        }
        if (input[i] == '\'' && in_single_quotes) {
            in_single_quotes = false;
            if (cap_exceeded(1)) return result;
            result += input[i];
            i++;
            continue;
        }
        if (in_single_quotes) {
            if (cap_exceeded(1)) return result;
            result += input[i];
            i++;
            continue;
        }
        if (input[i] == '$') {
            i++;
            if (i < input.size() && input[i] == '?') {
                string s = to_string(last_exit_status);
                if (cap_exceeded(s.size())) return result;
                result += s;
                i++;
                continue;
            }
            if (i < input.size() && input[i] == '$') {
                string s = to_string(getpid());
                if (cap_exceeded(s.size())) return result;
                result += s;
                i++;
                continue;
            }
            if (i < input.size() && input[i] == '{') {
                // `i` currently points at '{'; remember its offset so the
                // diagnostic can point back at the unterminated `${`.
                size_t brace_off = i - 1; // offset of the leading '$'
                i++; // skip '{'
                string var_name;
                while (i < input.size() && input[i] != '}') {
                    var_name += input[i];
                    i++;
                }
                if (i < input.size()) {
                    i++; // skip '}'
                } else {
                    // Reached end of input without seeing '}': classic
                    // `${FOO` with no close. Emit a parse error pointing
                    // at the opening `$`.
                    size_t ln = 1, col = 1;
                    tash::parse::offset_to_line_col(input, brace_off, ln, col);
                    tash::parse::emit_parse_error(
                        {"unmatched '${' in variable expansion", ln, col});
                }
                const char *val = getenv(var_name.c_str());
                if (val) {
                    size_t n = std::strlen(val);
                    if (cap_exceeded(n)) return result;
                    result.append(val, n);
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
                        size_t n = std::strlen(val);
                        if (cap_exceeded(n)) return result;
                        result.append(val, n);
                    }
                } else {
                    if (cap_exceeded(1)) return result;
                    result += '$';
                }
            }
        } else {
            if (cap_exceeded(1)) return result;
            result += input[i];
            i++;
        }
    }
    return result;
}

string expand_command_substitution(const string &input, ShellState &state) {
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
                // Recurse first so any nested $(...) inside this body is
                // expanded by tash (firing the safety hook at every level)
                // before the resulting text is handed to /bin/sh. Without
                // this, `ls $(echo $(rm -rf .))` would let /bin/sh evaluate
                // the inner $(rm -rf .) unseen by our classifier.
                cmd = expand_command_substitution(cmd, state);
                // Route through the hook-aware helper so the safety plugin
                // can inspect (and potentially skip) the inner command
                // before it runs.
                auto hooked = run_command_with_hooks_capture(cmd, state);
                string output = hooked.captured_stdout;
                while (!output.empty() && output.back() == '\n') {
                    output.pop_back();
                }
                if (!hooked.skipped) {
                    if (result.size() + output.size() > tash::util::TASH_MAX_EXPANSION_BYTES) {
                        expansion_cap_error("command substitution");
                        return std::string();
                    }
                    result += output;
                }
                // If skipped, append nothing — the $(...) expands to
                // empty, which is the same behaviour as `$()` with no
                // inner command.
                i = j + 1;
            } else {
                // depth > 0 here means the input ran out before we saw
                // the closing ')'. Emit a position-tagged diagnostic
                // pointing at the `$` that opened the substitution, then
                // fall back to the prior behaviour (copy the `$` through
                // and keep scanning) so downstream callers still get a
                // usable string.
                size_t ln = 1, col = 1;
                tash::parse::offset_to_line_col(input, i, ln, col);
                tash::parse::emit_parse_error(
                    {"unmatched '$(' in command substitution", ln, col});
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
                    if (expanded.size() >= tash::util::TASH_MAX_GLOB_RESULTS) {
                        expansion_cap_error("glob expansion");
                        globfree(&glob_result);
                        return std::vector<std::string>();
                    }
                    expanded.push_back(glob_result.gl_pathv[i]);
                }
            } else {
                if (expanded.size() >= tash::util::TASH_MAX_GLOB_RESULTS) {
                    expansion_cap_error("glob expansion");
                    globfree(&glob_result);
                    return std::vector<std::string>();
                }
                expanded.push_back(arg);
            }
            globfree(&glob_result);
        } else {
            if (expanded.size() >= tash::util::TASH_MAX_GLOB_RESULTS) {
                expansion_cap_error("glob expansion");
                return std::vector<std::string>();
            }
            expanded.push_back(arg);
        }
    }
    return expanded;
}

string strip_quotes(string_view s) {
    if (s.size() >= 2) {
        if ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')) {
            return string(s.substr(1, s.size() - 2));
        }
    }
    return string(s);
}

// Build a plain file-backed redirection. Avoids aggregate-init braces
// now that Redirection has default-initialized heredoc fields — without
// this the compiler warns about each missing initializer.
static Redirection make_redir(int fd, const string &filename,
                              bool append, bool dup_to_stdout) {
    Redirection r;
    r.fd = fd;
    r.filename = filename;
    r.append = append;
    r.dup_to_stdout = dup_to_stdout;
    return r;
}

// Same reasoning for CommandSegment now that it carries a heredocs
// vector.
static CommandSegment make_segment(const string &command, OperatorType op) {
    CommandSegment s;
    s.command = command;
    s.op = op;
    return s;
}

Command parse_redirections(const string &command_str) {
    return parse_redirections(command_str, nullptr);
}

Command parse_redirections(const string &command_str,
                           vector<PendingHeredoc> *bodies) {
    Command cmd;
    string remaining;
    tash::util::QuoteState qs;
    size_t i = 0;
    size_t body_index = 0;

    while (i < command_str.size()) {
        char c = command_str[i];

        if (qs.consume(c)) {
            remaining += c;
            ++i;
        } else if (!qs.any_active()) {
            // Check for 2>&1
            if (i + 4 <= command_str.size() && command_str.compare(i, 4, "2>&1") == 0) {
                cmd.redirections.push_back(make_redir(2, "", false, true));
                i += 4;
            }
            // Check for 2>
            else if (i + 2 <= command_str.size() && command_str.compare(i, 2, "2>") == 0) {
                size_t op_off = i;
                i += 2;
                while (i < command_str.size() && command_str[i] == ' ') ++i;
                string fname;
                while (i < command_str.size() && command_str[i] != ' ' && command_str[i] != '\t') {
                    fname += command_str[i];
                    ++i;
                }
                fname = trim(fname);
                if (!fname.empty()) {
                    cmd.redirections.push_back(make_redir(2, fname, false, false));
                } else {
                    size_t ln = 1, col = 1;
                    tash::parse::offset_to_line_col(command_str, op_off, ln, col);
                    tash::parse::emit_parse_error(
                        {"missing filename for '2>'", ln, col});
                }
            }
            // Check for >>
            else if (i + 2 <= command_str.size() && command_str.compare(i, 2, ">>") == 0) {
                size_t op_off = i;
                i += 2;
                while (i < command_str.size() && command_str[i] == ' ') ++i;
                string fname;
                while (i < command_str.size() && command_str[i] != ' ' && command_str[i] != '\t') {
                    fname += command_str[i];
                    ++i;
                }
                fname = trim(fname);
                if (!fname.empty()) {
                    cmd.redirections.push_back(make_redir(1, fname, true, false));
                } else {
                    size_t ln = 1, col = 1;
                    tash::parse::offset_to_line_col(command_str, op_off, ln, col);
                    tash::parse::emit_parse_error(
                        {"missing filename for '>>'", ln, col});
                }
            }
            // Check for > (must be after >>)
            else if (c == '>') {
                size_t op_off = i;
                i += 1;
                while (i < command_str.size() && command_str[i] == ' ') ++i;
                string fname;
                while (i < command_str.size() && command_str[i] != ' ' && command_str[i] != '\t') {
                    fname += command_str[i];
                    ++i;
                }
                fname = trim(fname);
                if (!fname.empty()) {
                    cmd.redirections.push_back(make_redir(1, fname, false, false));
                } else {
                    size_t ln = 1, col = 1;
                    tash::parse::offset_to_line_col(command_str, op_off, ln, col);
                    tash::parse::emit_parse_error(
                        {"missing filename for '>'", ln, col});
                }
            }
            // Check for << (heredoc). Must precede the plain `<` branch.
            else if (i + 2 <= command_str.size() && command_str.compare(i, 2, "<<") == 0) {
                size_t op_off = i;
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

                if (delim.empty()) {
                    size_t ln = 1, col = 1;
                    tash::parse::offset_to_line_col(command_str, op_off, ln, col);
                    tash::parse::emit_parse_error(
                        {"missing delimiter for '<<' heredoc", ln, col});
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
                size_t op_off = i;
                i += 1;
                while (i < command_str.size() && command_str[i] == ' ') ++i;
                string fname;
                while (i < command_str.size() && command_str[i] != ' ' && command_str[i] != '\t') {
                    fname += command_str[i];
                    ++i;
                }
                fname = trim(fname);
                if (!fname.empty()) {
                    cmd.redirections.push_back(make_redir(0, fname, false, false));
                } else {
                    size_t ln = 1, col = 1;
                    tash::parse::offset_to_line_col(command_str, op_off, ln, col);
                    tash::parse::emit_parse_error(
                        {"missing filename for '<'", ln, col});
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
    // Offsets of the most recent unmatched opener, for diagnostics.
    size_t single_quote_open = 0;
    size_t double_quote_open = 0;
    size_t last_paren_open = 0;
    int paren_depth = 0;       // track `(...)` subshells so `;`, `&&`,
                               // `||` inside them don't split segments
    int cmdsub_depth = 0;      // separate depth for `$(...)` so its
                               // parens don't confuse the subshell count
    size_t i = 0;
    OperatorType next_op = OP_NONE;

    while (i < stripped.size()) {
        char c = stripped[i];
        if (c == '"' && !in_single_quotes) {
            if (!in_double_quotes) double_quote_open = i;
            in_double_quotes = !in_double_quotes;
            current += c;
            ++i;
        } else if (c == '\'' && !in_double_quotes) {
            if (!in_single_quotes) single_quote_open = i;
            in_single_quotes = !in_single_quotes;
            current += c;
            ++i;
        } else if (!in_double_quotes && !in_single_quotes && c == '(') {
            // `$(...)` is command substitution, not a subshell — the
            // expansion pass has its own close-tracking and error path.
            // Track cmdsub opens in a separate depth so we can match the
            // corresponding `)` without miscounting the subshell parens.
            bool is_cmdsub_open = (i > 0 && stripped[i - 1] == '$');
            if (is_cmdsub_open) {
                ++cmdsub_depth;
            } else {
                if (paren_depth == 0) last_paren_open = i;
                ++paren_depth;
            }
            current += c;
            ++i;
        } else if (!in_double_quotes && !in_single_quotes && c == ')') {
            // Match cmdsub closes against cmdsub opens before popping a
            // subshell depth — otherwise `$(foo)` inside a subshell would
            // close the subshell one level early.
            if (cmdsub_depth > 0) {
                --cmdsub_depth;
            } else if (paren_depth > 0) {
                --paren_depth;
            }
            current += c;
            ++i;
        } else if (!in_double_quotes && !in_single_quotes && paren_depth == 0 && c == '&' && i + 1 < stripped.size() && stripped[i + 1] == '&') {
            string cmd = current;
            cmd = trim(cmd);
            if (!cmd.empty()) segments.push_back(make_segment(cmd, next_op));
            next_op = OP_AND;
            current.clear();
            i += 2;
        } else if (!in_double_quotes && !in_single_quotes && paren_depth == 0 && c == '|' && i + 1 < stripped.size() && stripped[i + 1] == '|') {
            string cmd = current;
            cmd = trim(cmd);
            if (!cmd.empty()) segments.push_back(make_segment(cmd, next_op));
            next_op = OP_OR;
            current.clear();
            i += 2;
        } else if (!in_double_quotes && !in_single_quotes && paren_depth == 0 && c == ';') {
            string cmd = current;
            cmd = trim(cmd);
            if (!cmd.empty()) segments.push_back(make_segment(cmd, next_op));
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
    if (!cmd.empty()) {
        segments.push_back(make_segment(cmd, next_op));
    } else if (next_op == OP_AND || next_op == OP_OR) {
        // Hit EOL with a pending `&&` / `||` from the previous segment
        // but no command to apply it to. `;` is a terminator, not an
        // operator, so `ls;` stays silent. The REPL catches trailing
        // `&&` / `||` before we get here (is_input_complete returns
        // false), but scripts and `-c` strings can land in this branch.
        size_t ln = 1, col = 1;
        tash::parse::offset_to_line_col(stripped, stripped.size(), ln, col);
        const char *what = (next_op == OP_AND)
            ? "empty command after '&&'"
            : "empty command after '||'";
        tash::parse::emit_parse_error({what, ln, col});
    }

    // Unmatched quotes / parens: flag at EOL. These guards fire after
    // segment collection so the partial parse doesn't get lost — the
    // caller still sees whatever segments we managed to build.
    if (in_single_quotes) {
        size_t ln = 1, col = 1;
        tash::parse::offset_to_line_col(stripped, single_quote_open, ln, col);
        tash::parse::emit_parse_error(
            {"unmatched '\\''", ln, col});
    }
    if (in_double_quotes) {
        size_t ln = 1, col = 1;
        tash::parse::offset_to_line_col(stripped, double_quote_open, ln, col);
        tash::parse::emit_parse_error(
            {"unmatched '\"'", ln, col});
    }
    // Note: we deliberately do NOT emit for unmatched `(` here — the
    // executor's subshell + pipeline-subshell branches already report
    // that with more context ("unmatched '(' in subshell" / "unmatched
    // '(' in pipeline subshell"), and emitting twice would produce
    // duplicate diagnostics for the same token. paren_depth tracking
    // stays because it correctly keeps `;` / `&&` / `||` inside a
    // subshell from splitting the segment.
    (void)last_paren_open;
    (void)paren_depth;
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
    // NOTE: This helper stays silent on malformed markers. Callers invoke
    // it several times per line (REPL prefetch, executor per-segment,
    // parse_redirections), so emitting here would produce duplicate
    // diagnostics. The empty-delimiter check lives in parse_redirections
    // where it only runs once per Command.
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
