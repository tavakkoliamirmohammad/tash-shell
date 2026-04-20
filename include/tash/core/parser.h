#ifndef TASH_CORE_PARSER_H
#define TASH_CORE_PARSER_H

// Parser surface: tokenization, variable/glob/tilde expansion,
// command-line parsing, redirection parsing, heredoc scanning.
//
// Split out of the old mega-header tash/core.h. `tash/core.h` still
// includes this so existing #include paths keep working.

#include "tash/shell.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "replxx.hxx"

std::string &rtrim(std::string &s, const char *t = " \t\n\r\f\v");
std::string &ltrim(std::string &s, const char *t = " \t\n\r\f\v");
std::string &trim(std::string &s, const char *t = " \t\n\r\f\v");
std::vector<std::string> tokenize_string(std::string line, const std::string &delimiter);
std::string expand_variables(const std::string &input, int last_exit_status);
std::string expand_command_substitution(const std::string &input, ShellState &state);
std::vector<std::string> expand_globs(const std::vector<std::string> &args,
                                       const std::vector<bool> &quoted);
std::string expand_tilde(const std::string &token);
std::string strip_quotes(std::string_view s);
std::vector<CommandSegment> parse_command_line(const std::string &line);
std::string expand_history_bang(const std::string &line, replxx::Replxx &rx);
Command parse_redirections(const std::string &command_str);
Command parse_redirections(const std::string &command_str,
                           std::vector<PendingHeredoc> *bodies);
bool is_input_complete(const std::string &input);

// Walk a full command line (post-replxx input, pre-segment-split) and
// return an ordered list of heredoc markers it declares. Caller uses
// this to know whether more input lines must be read to form the body.
std::vector<PendingHeredoc> scan_pending_heredocs(const std::string &line);

// Read body lines from `read_line` until each pending heredoc's
// delimiter appears alone on a line. Returns true on success, false if
// the stream ran out before all delimiters were satisfied. Performs
// leading-tab stripping for `<<-` forms.
bool collect_heredoc_bodies(std::vector<PendingHeredoc> &pending,
                            const std::function<bool(std::string&)> &read_line);

#endif // TASH_CORE_PARSER_H
