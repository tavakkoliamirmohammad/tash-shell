#include "shell.h"
#include <algorithm>

using namespace std;
using namespace replxx;

// ── Syntax highlighting callback ───────────────────────────────
// Colors the input line as the user types:
// - Command name: green if exists, red if not
// - Quoted strings: yellow
// - Operators (|, &&, ||, ;): cyan
// - Redirections (>, >>, <, 2>): magenta
// - Comments (#...): gray
// - Variables ($VAR): blue

void syntax_highlighter(const string &input, Replxx::colors_t &colors) {
    bool in_single_quote = false;
    bool in_double_quote = false;
    bool first_word = true;
    bool at_word_start = true;

    for (size_t i = 0; i < input.size() && i < colors.size(); i++) {
        char c = input[i];

        // Comments: everything after unquoted #
        if (c == '#' && !in_single_quote && !in_double_quote) {
            for (size_t j = i; j < colors.size(); j++) {
                colors[j] = Replxx::Color::GRAY;
            }
            break;
        }

        // Quoted strings
        if (c == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
            colors[i] = Replxx::Color::YELLOW;
            continue;
        }
        if (c == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
            colors[i] = Replxx::Color::YELLOW;
            continue;
        }
        if (in_single_quote || in_double_quote) {
            colors[i] = Replxx::Color::YELLOW;
            // Variables inside double quotes
            if (in_double_quote && c == '$') {
                colors[i] = Replxx::Color::BLUE;
            }
            continue;
        }

        // Variables
        if (c == '$') {
            colors[i] = Replxx::Color::BLUE;
            // Color the rest of the variable name
            size_t j = i + 1;
            if (j < input.size() && input[j] == '{') {
                while (j < input.size() && j < colors.size()) {
                    colors[j] = Replxx::Color::BLUE;
                    if (input[j] == '}') { j++; break; }
                    j++;
                }
            } else {
                while (j < input.size() && j < colors.size() &&
                       (isalnum(input[j]) || input[j] == '_' || input[j] == '?' || input[j] == '$')) {
                    colors[j] = Replxx::Color::BLUE;
                    j++;
                }
            }
            i = j - 1;
            continue;
        }

        // Operators
        if (c == '|') {
            colors[i] = Replxx::Color::CYAN;
            if (i + 1 < input.size() && i + 1 < colors.size() && input[i + 1] == '|') {
                colors[i + 1] = Replxx::Color::CYAN;
                i++;
                first_word = true;
                at_word_start = true;
            }
            first_word = true;
            at_word_start = true;
            continue;
        }
        if (c == '&' && i + 1 < input.size() && input[i + 1] == '&') {
            colors[i] = Replxx::Color::CYAN;
            colors[i + 1] = Replxx::Color::CYAN;
            i++;
            first_word = true;
            at_word_start = true;
            continue;
        }
        if (c == ';') {
            colors[i] = Replxx::Color::CYAN;
            first_word = true;
            at_word_start = true;
            continue;
        }

        // Redirections
        if (c == '>' || c == '<') {
            colors[i] = Replxx::Color::MAGENTA;
            if (c == '>' && i + 1 < input.size() && input[i + 1] == '>') {
                colors[i + 1] = Replxx::Color::MAGENTA;
                i++;
            }
            continue;
        }
        if (c == '2' && i + 1 < input.size() && input[i + 1] == '>') {
            colors[i] = Replxx::Color::MAGENTA;
            colors[i + 1] = Replxx::Color::MAGENTA;
            i++;
            if (i + 1 < input.size() && input[i + 1] == '&') {
                colors[i + 1] = Replxx::Color::MAGENTA;
                i++;
                if (i + 1 < input.size() && input[i + 1] == '1') {
                    colors[i + 1] = Replxx::Color::MAGENTA;
                    i++;
                }
            }
            continue;
        }

        // Space handling
        if (c == ' ' || c == '\t') {
            at_word_start = true;
            continue;
        }

        // Command name (first word)
        if (first_word && at_word_start) {
            // Extract the full word
            size_t j = i;
            while (j < input.size() && input[j] != ' ' && input[j] != '\t' &&
                   input[j] != '|' && input[j] != '&' && input[j] != ';' &&
                   input[j] != '>' && input[j] != '<') {
                j++;
            }
            string word = input.substr(i, j - i);

            // Check if it's a valid command
            Replxx::Color cmd_color;
            if (is_builtin(word) || word == "bg" || word == "z") {
                cmd_color = Replxx::Color::BRIGHTGREEN;
            } else if (command_exists_on_path(word)) {
                cmd_color = Replxx::Color::GREEN;
            } else {
                cmd_color = Replxx::Color::RED;
            }

            for (size_t k = i; k < j && k < colors.size(); k++) {
                colors[k] = cmd_color;
            }
            i = j - 1;
            first_word = false;
            at_word_start = false;
            continue;
        }

        at_word_start = false;
    }
}

// ── Hint callback (autosuggestions / ghost text) ────────────────
// Shows the most recent history entry matching the current input
// as dimmed gray text after the cursor.

Replxx::hints_t hint_callback(const string &input, int &context_len, Replxx::Color &color) {
    Replxx::hints_t hints;

    // Don't show hints for very short input
    if (input.size() < 2) return hints;

    // Don't show hints if input contains a space (partial command args are noisy)
    // Only suggest for the command portion
    color = Replxx::Color::GRAY;
    context_len = (int)input.size();

    // Search history for prefix match (most recent first)
    // This is done via the suggest.cpp command cache + history
    // For now, return empty — the history-based hints are set up in main.cpp
    // where we have access to the Replxx instance's history

    return hints;
}
