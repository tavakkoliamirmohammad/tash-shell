#ifndef SHELL_H
#define SHELL_H

#include <string>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <unordered_set>

using namespace std;

// ── Testable pure functions ──────────────────────────────────

string &rtrim(string &s, const char *t = " \t\n\r\f\v");
string &ltrim(string &s, const char *t = " \t\n\r\f\v");
string &trim(string &s, const char *t = " \t\n\r\f\v");
vector<string> tokenize_string(string line, const string &delimiter);

#endif // SHELL_H
