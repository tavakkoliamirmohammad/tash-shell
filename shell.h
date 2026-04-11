#ifndef SHELL_H
#define SHELL_H

#include <string>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <unordered_set>

using namespace std;

// ── Operator types for command separators ────────────────────

enum OperatorType {
    OP_NONE,
    OP_AND,
    OP_OR,
    OP_SEMICOLON
};

struct CommandSegment {
    string command;
    OperatorType op;
};

// ── Testable pure functions ──────────────────────────────────

string &rtrim(string &s, const char *t = " \t\n\r\f\v");
string &ltrim(string &s, const char *t = " \t\n\r\f\v");
string &trim(string &s, const char *t = " \t\n\r\f\v");
vector<string> tokenize_string(string line, const string &delimiter);
string expand_variables(const string &input);
string expand_command_substitution(const string &input);
string strip_quotes(const string &s);
vector<CommandSegment> parse_command_line(const string &line);

#endif // SHELL_H
