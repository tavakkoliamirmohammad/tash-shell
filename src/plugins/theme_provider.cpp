#include "tash/plugins/theme_provider.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

// ── Helpers ───────────────────────────────────────────────────

static std::string trim_whitespace(const std::string &s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        start++;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        end--;
    }
    return s.substr(start, end - start);
}

static std::string strip_quotes(const std::string &s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// ── parse_hex ─────────────────────────────────────────────────

RGB Theme::parse_hex(const std::string &hex) {
    if (hex.empty() || hex[0] != '#') {
        return RGB(0, 0, 0);
    }

    std::string digits = hex.substr(1);

    // "#RGB" short form -> expand to "#RRGGBB"
    if (digits.size() == 3) {
        int r = hex_digit(digits[0]);
        int g = hex_digit(digits[1]);
        int b = hex_digit(digits[2]);
        if (r < 0 || g < 0 || b < 0) return RGB(0, 0, 0);
        return RGB(
            static_cast<uint8_t>(r * 17),
            static_cast<uint8_t>(g * 17),
            static_cast<uint8_t>(b * 17)
        );
    }

    // "#RRGGBB" full form
    if (digits.size() == 6) {
        int r1 = hex_digit(digits[0]);
        int r2 = hex_digit(digits[1]);
        int g1 = hex_digit(digits[2]);
        int g2 = hex_digit(digits[3]);
        int b1 = hex_digit(digits[4]);
        int b2 = hex_digit(digits[5]);
        if (r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 || b1 < 0 || b2 < 0) {
            return RGB(0, 0, 0);
        }
        return RGB(
            static_cast<uint8_t>(r1 * 16 + r2),
            static_cast<uint8_t>(g1 * 16 + g2),
            static_cast<uint8_t>(b1 * 16 + b2)
        );
    }

    return RGB(0, 0, 0);
}

// ── default_theme (Catppuccin Mocha) ──────────────────────────

Theme Theme::default_theme() {
    Theme t;
    t.name = "Catppuccin Mocha";
    t.variant = "dark";

    // Syntax — from src/util/theme.h macros
    t.command_valid   = RGB(166, 227, 161); // #a6e3a1  CAT_GREEN
    t.command_builtin = RGB(148, 226, 213); // #94e2d5  CAT_TEAL
    t.command_invalid = RGB(243, 139, 168); // #f38ba8  CAT_RED
    t.string_color    = RGB(249, 226, 175); // #f9e2af  CAT_YELLOW
    t.variable        = RGB(137, 220, 235); // #89dceb  CAT_SKY
    t.op              = RGB(203, 166, 247); // #cba6f7  CAT_MAUVE
    t.redirect        = RGB(250, 179, 135); // #fab387  CAT_PEACH
    t.comment         = RGB(108, 112, 134); // #6c7086  CAT_OVERLAY0

    // Prompt
    t.prompt_success   = RGB(166, 227, 161); // #a6e3a1  CAT_GREEN
    t.prompt_error     = RGB(243, 139, 168); // #f38ba8  CAT_RED
    t.prompt_path      = RGB(137, 180, 250); // #89b4fa  CAT_BLUE
    t.prompt_git       = RGB(203, 166, 247); // #cba6f7  CAT_MAUVE
    t.prompt_duration  = RGB(250, 179, 135); // #fab387  CAT_PEACH
    t.prompt_user      = RGB(166, 227, 161); // #a6e3a1  CAT_GREEN
    t.prompt_separator = RGB(127, 132, 156); // #7f849c  CAT_OVERLAY1

    // Completion
    t.comp_builtin     = RGB(148, 226, 213); // #94e2d5  CAT_TEAL
    t.comp_command     = RGB(166, 227, 161); // #a6e3a1  CAT_GREEN
    t.comp_file        = RGB(249, 226, 175); // #f9e2af  CAT_YELLOW
    t.comp_directory   = RGB(137, 180, 250); // #89b4fa  CAT_BLUE
    t.comp_option      = RGB(250, 179, 135); // #fab387  CAT_PEACH
    t.comp_description = RGB(186, 194, 222); // #bac2de  CAT_SUBTEXT1

    return t;
}

// ── INI-style TOML loader ─────────────────────────────────────

// Applies a key=value pair from the TOML file to the theme struct
// based on the current section.
static void apply_field(Theme &t, const std::string &section,
                        const std::string &key, const std::string &value) {
    RGB color = Theme::parse_hex(value);

    if (section == "meta") {
        if (key == "name")    t.name = value;
        if (key == "variant") t.variant = value;
        return;
    }

    if (section == "syntax") {
        if (key == "command_valid")   t.command_valid   = color;
        if (key == "command_builtin") t.command_builtin = color;
        if (key == "command_invalid") t.command_invalid = color;
        if (key == "string_color")    t.string_color    = color;
        if (key == "variable")        t.variable        = color;
        if (key == "operator")        t.op              = color;
        if (key == "redirect")        t.redirect        = color;
        if (key == "comment")         t.comment         = color;
        return;
    }

    if (section == "prompt") {
        if (key == "success")   t.prompt_success   = color;
        if (key == "error")     t.prompt_error     = color;
        if (key == "path")      t.prompt_path      = color;
        if (key == "git")       t.prompt_git       = color;
        if (key == "duration")  t.prompt_duration  = color;
        if (key == "user")      t.prompt_user      = color;
        if (key == "separator") t.prompt_separator = color;
        return;
    }

    if (section == "completion") {
        if (key == "builtin")     t.comp_builtin     = color;
        if (key == "command")     t.comp_command     = color;
        if (key == "file")        t.comp_file        = color;
        if (key == "directory")   t.comp_directory   = color;
        if (key == "option")      t.comp_option      = color;
        if (key == "description") t.comp_description = color;
        return;
    }
}

Theme Theme::load_from_file(const std::string &path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return default_theme();
    }

    Theme t = default_theme(); // start from defaults so missing keys are fine
    std::string section;
    std::string line;

    while (std::getline(file, line)) {
        line = trim_whitespace(line);

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }

        // Section header: [section]
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            section = trim_whitespace(section);
            continue;
        }

        // Key = value
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }

        std::string key = trim_whitespace(line.substr(0, eq));
        std::string value = trim_whitespace(line.substr(eq + 1));
        value = strip_quotes(value);

        apply_field(t, section, key, value);
    }

    return t;
}
