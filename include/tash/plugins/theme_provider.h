#ifndef TASH_THEME_PROVIDER_H
#define TASH_THEME_PROVIDER_H

#include <string>
#include <cstdint>

// ── RGB color ─────────────────────────────────────────────────

struct RGB {
    uint8_t r, g, b;

    RGB() : r(0), g(0), b(0) {}
    RGB(uint8_t r, uint8_t g, uint8_t b) : r(r), g(g), b(b) {}

    bool operator==(const RGB &other) const {
        return r == other.r && g == other.g && b == other.b;
    }
    bool operator!=(const RGB &other) const {
        return !(*this == other);
    }
};

// ── Theme struct ──────────────────────────────────────────────

struct Theme {
    std::string name;
    std::string variant; // "dark" or "light"

    // Syntax
    RGB command_valid;
    RGB command_builtin;
    RGB command_invalid;
    RGB string_color;
    RGB variable;
    RGB op;
    RGB redirect;
    RGB comment;

    // Prompt
    RGB prompt_success;
    RGB prompt_error;
    RGB prompt_path;
    RGB prompt_git;
    RGB prompt_duration;
    RGB prompt_user;
    RGB prompt_separator;

    // Completion
    RGB comp_builtin;
    RGB comp_command;
    RGB comp_file;
    RGB comp_directory;
    RGB comp_option;
    RGB comp_description;

    // Parse "#rrggbb" or "#RGB" to RGB struct. Returns {0,0,0} on error.
    static RGB parse_hex(const std::string &hex);

    // Returns the current Catppuccin Mocha palette as default.
    static Theme default_theme();

    // Load a theme from a .toml file. Falls back to default_theme() on error.
    static Theme load_from_file(const std::string &path);
};

#endif // TASH_THEME_PROVIDER_H
