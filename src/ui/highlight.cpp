#include "tash/core.h"
#include "tash/ui.h"
#include "theme.h"
#include <algorithm>

using namespace std;
using namespace replxx;

// Convert an RGB (0-255 per channel) to a replxx rgb666 Color.
static Replxx::Color to_replxx(const RGB &c) {
    int r = (c.r * 5 + 127) / 255;
    int g = (c.g * 5 + 127) / 255;
    int b = (c.b * 5 + 127) / 255;
    return replxx::color::rgb666(r, g, b);
}

static Replxx::Color cat_green()   { return to_replxx(g_current_theme.command_valid); }
static Replxx::Color cat_teal()    { return to_replxx(g_current_theme.command_builtin); }
static Replxx::Color cat_red()     { return to_replxx(g_current_theme.command_invalid); }
static Replxx::Color cat_yellow()  { return to_replxx(g_current_theme.string_color); }
static Replxx::Color cat_sky()     { return to_replxx(g_current_theme.variable); }
static Replxx::Color cat_mauve()   { return to_replxx(g_current_theme.op); }
static Replxx::Color cat_peach()   { return to_replxx(g_current_theme.redirect); }
static Replxx::Color cat_overlay() { return to_replxx(g_current_theme.comment); }

// ── Syntax highlighting callback ───────────────────────────────

void syntax_highlighter(const string &input, Replxx::colors_t &colors) {
    bool in_single_quote = false;
    bool in_double_quote = false;
    bool first_word = true;
    bool at_word_start = true;

    for (size_t i = 0; i < input.size() && i < colors.size(); i++) {
        char c = input[i];

        // Comments
        if (c == '#' && !in_single_quote && !in_double_quote) {
            for (size_t j = i; j < colors.size(); j++) {
                colors[j] = cat_overlay();
            }
            break;
        }

        // Quoted strings
        if (c == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
            colors[i] = cat_yellow();
            continue;
        }
        if (c == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
            colors[i] = cat_yellow();
            continue;
        }
        if (in_single_quote || in_double_quote) {
            colors[i] = cat_yellow();
            if (in_double_quote && c == '$') {
                colors[i] = cat_sky();
            }
            continue;
        }

        // Variables
        if (c == '$') {
            colors[i] = cat_sky();
            size_t j = i + 1;
            if (j < input.size() && input[j] == '{') {
                while (j < input.size() && j < colors.size()) {
                    colors[j] = cat_sky();
                    if (input[j] == '}') { j++; break; }
                    j++;
                }
            } else {
                while (j < input.size() && j < colors.size() &&
                       (isalnum(input[j]) || input[j] == '_' || input[j] == '?' || input[j] == '$')) {
                    colors[j] = cat_sky();
                    j++;
                }
            }
            i = j - 1;
            continue;
        }

        // Operators
        if (c == '|') {
            colors[i] = cat_mauve();
            if (i + 1 < input.size() && i + 1 < colors.size() && input[i + 1] == '|') {
                colors[i + 1] = cat_mauve();
                i++;
            }
            first_word = true;
            at_word_start = true;
            continue;
        }
        if (c == '&' && i + 1 < input.size() && input[i + 1] == '&') {
            colors[i] = cat_mauve();
            colors[i + 1] = cat_mauve();
            i++;
            first_word = true;
            at_word_start = true;
            continue;
        }
        if (c == ';') {
            colors[i] = cat_mauve();
            first_word = true;
            at_word_start = true;
            continue;
        }

        // Redirections
        if (c == '>' || c == '<') {
            colors[i] = cat_peach();
            if (c == '>' && i + 1 < input.size() && input[i + 1] == '>') {
                colors[i + 1] = cat_peach();
                i++;
            }
            continue;
        }
        if (c == '2' && i + 1 < input.size() && input[i + 1] == '>') {
            colors[i] = cat_peach();
            colors[i + 1] = cat_peach();
            i++;
            if (i + 1 < input.size() && input[i + 1] == '&') {
                colors[i + 1] = cat_peach();
                i++;
                if (i + 1 < input.size() && input[i + 1] == '1') {
                    colors[i + 1] = cat_peach();
                    i++;
                }
            }
            continue;
        }

        // Spaces
        if (c == ' ' || c == '\t') {
            at_word_start = true;
            continue;
        }

        // Command name (first word after operator or start)
        if (first_word && at_word_start) {
            size_t j = i;
            while (j < input.size() && input[j] != ' ' && input[j] != '\t' &&
                   input[j] != '|' && input[j] != '&' && input[j] != ';' &&
                   input[j] != '>' && input[j] != '<') {
                j++;
            }
            string word = input.substr(i, j - i);

            Replxx::Color cmd_color;
            if (is_builtin(word) || word == "bg" || word == "z") {
                cmd_color = cat_teal();
            } else if (command_exists_on_path(word)) {
                cmd_color = cat_green();
            } else {
                cmd_color = cat_red();
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

// ── Hint callback stub ─────────────────────────────────────────

Replxx::hints_t hint_callback(const string &input, int &context_len, Replxx::Color &color) {
    Replxx::hints_t hints;
    if (input.size() < 2) return hints;
    color = Replxx::Color::GRAY;
    context_len = (int)input.size();
    return hints;
}
