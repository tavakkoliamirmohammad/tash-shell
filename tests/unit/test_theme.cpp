#include <gtest/gtest.h>
#include "tash/plugins/theme_provider.h"
#include <cstdlib>
#include <fstream>

// ── Helper: path to bundled themes (relative to project root) ─

static std::string themes_dir() {
    // CMAKE_SOURCE_DIR is passed as a define; fall back to relative path
#ifdef TASH_THEMES_DIR
    return TASH_THEMES_DIR;
#else
    return "data/themes";
#endif
}

// ── parse_hex tests ───────────────────────────────────────────

TEST(ThemeParseHex, ParseHexColor) {
    RGB c = Theme::parse_hex("#a6e3a1");
    EXPECT_EQ(c.r, 0xa6);
    EXPECT_EQ(c.g, 0xe3);
    EXPECT_EQ(c.b, 0xa1);
}

TEST(ThemeParseHex, ParseHexUppercase) {
    RGB c = Theme::parse_hex("#FF8800");
    EXPECT_EQ(c.r, 0xFF);
    EXPECT_EQ(c.g, 0x88);
    EXPECT_EQ(c.b, 0x00);
}

TEST(ThemeParseHex, ParseInvalidHexEmpty) {
    RGB c = Theme::parse_hex("");
    EXPECT_EQ(c.r, 0);
    EXPECT_EQ(c.g, 0);
    EXPECT_EQ(c.b, 0);
}

TEST(ThemeParseHex, ParseInvalidHexNoHash) {
    RGB c = Theme::parse_hex("a6e3a1");
    EXPECT_EQ(c.r, 0);
    EXPECT_EQ(c.g, 0);
    EXPECT_EQ(c.b, 0);
}

TEST(ThemeParseHex, ParseInvalidHexWrongLength) {
    RGB c = Theme::parse_hex("#a6e3");
    EXPECT_EQ(c.r, 0);
    EXPECT_EQ(c.g, 0);
    EXPECT_EQ(c.b, 0);
}

TEST(ThemeParseHex, ParseInvalidHexBadChars) {
    RGB c = Theme::parse_hex("#gggggg");
    EXPECT_EQ(c.r, 0);
    EXPECT_EQ(c.g, 0);
    EXPECT_EQ(c.b, 0);
}

TEST(ThemeParseHex, ParseShortHex) {
    RGB c = Theme::parse_hex("#F80");
    // #F80 -> #FF8800
    EXPECT_EQ(c.r, 0xFF);
    EXPECT_EQ(c.g, 0x88);
    EXPECT_EQ(c.b, 0x00);
}

TEST(ThemeParseHex, ParseShortHexLowercase) {
    RGB c = Theme::parse_hex("#abc");
    // #abc -> #aabbcc
    EXPECT_EQ(c.r, 0xAA);
    EXPECT_EQ(c.g, 0xBB);
    EXPECT_EQ(c.b, 0xCC);
}

// ── default_theme tests ───────────────────────────────────────

TEST(ThemeDefault, DefaultThemeHasCorrectName) {
    Theme t = Theme::default_theme();
    EXPECT_EQ(t.name, "Catppuccin Mocha");
    EXPECT_EQ(t.variant, "dark");
}

TEST(ThemeDefault, DefaultThemeHasCorrectColors) {
    Theme t = Theme::default_theme();

    // Spot-check Catppuccin Mocha values from theme.h
    EXPECT_EQ(t.command_valid,   RGB(166, 227, 161)); // #a6e3a1 green
    EXPECT_EQ(t.command_builtin, RGB(148, 226, 213)); // #94e2d5 teal
    EXPECT_EQ(t.command_invalid, RGB(243, 139, 168)); // #f38ba8 red
    EXPECT_EQ(t.string_color,    RGB(249, 226, 175)); // #f9e2af yellow
    EXPECT_EQ(t.prompt_path,     RGB(137, 180, 250)); // #89b4fa blue
    EXPECT_EQ(t.comp_builtin,    RGB(148, 226, 213)); // #94e2d5 teal
}

// ── load_from_file tests ──────────────────────────────────────

TEST(ThemeLoad, LoadMissingFileReturnsFallback) {
    Theme t = Theme::load_from_file("/nonexistent/path/theme.toml");
    Theme d = Theme::default_theme();

    EXPECT_EQ(t.name, d.name);
    EXPECT_EQ(t.variant, d.variant);
    EXPECT_EQ(t.command_valid, d.command_valid);
    EXPECT_EQ(t.prompt_success, d.prompt_success);
}

// ── Bundled theme file loading ────────────────────────────────

TEST(ThemeBundled, CatppuccinMochaLoads) {
    Theme t = Theme::load_from_file(themes_dir() + "/catppuccin-mocha.toml");
    EXPECT_EQ(t.name, "Catppuccin Mocha");
    EXPECT_EQ(t.variant, "dark");
    EXPECT_EQ(t.command_valid, RGB(166, 227, 161));
}

TEST(ThemeBundled, CatppuccinLatteLoads) {
    Theme t = Theme::load_from_file(themes_dir() + "/catppuccin-latte.toml");
    EXPECT_EQ(t.name, "Catppuccin Latte");
    EXPECT_EQ(t.variant, "light");
    EXPECT_EQ(t.command_valid, RGB(0x40, 0xa0, 0x2b));
}

TEST(ThemeBundled, TokyoNightLoads) {
    Theme t = Theme::load_from_file(themes_dir() + "/tokyo-night.toml");
    EXPECT_EQ(t.name, "Tokyo Night");
    EXPECT_EQ(t.variant, "dark");
    EXPECT_EQ(t.command_valid, RGB(0x9e, 0xce, 0x6a));
}

TEST(ThemeBundled, DraculaLoads) {
    Theme t = Theme::load_from_file(themes_dir() + "/dracula.toml");
    EXPECT_EQ(t.name, "Dracula");
    EXPECT_EQ(t.variant, "dark");
    EXPECT_EQ(t.command_valid, RGB(0x50, 0xfa, 0x7b));
}

TEST(ThemeBundled, NordLoads) {
    Theme t = Theme::load_from_file(themes_dir() + "/nord.toml");
    EXPECT_EQ(t.name, "Nord");
    EXPECT_EQ(t.variant, "dark");
    EXPECT_EQ(t.command_valid, RGB(0xa3, 0xbe, 0x8c));
}

TEST(ThemeBundled, MochaFileMatchesDefault) {
    Theme file_theme = Theme::load_from_file(themes_dir() + "/catppuccin-mocha.toml");
    Theme def = Theme::default_theme();

    // All syntax colors should match between file and hardcoded default
    EXPECT_EQ(file_theme.command_valid,   def.command_valid);
    EXPECT_EQ(file_theme.command_builtin, def.command_builtin);
    EXPECT_EQ(file_theme.command_invalid, def.command_invalid);
    EXPECT_EQ(file_theme.string_color,    def.string_color);
    EXPECT_EQ(file_theme.variable,        def.variable);
    EXPECT_EQ(file_theme.op,              def.op);
    EXPECT_EQ(file_theme.redirect,        def.redirect);
    EXPECT_EQ(file_theme.comment,         def.comment);

    // Prompt colors
    EXPECT_EQ(file_theme.prompt_success,   def.prompt_success);
    EXPECT_EQ(file_theme.prompt_error,     def.prompt_error);
    EXPECT_EQ(file_theme.prompt_path,      def.prompt_path);

    // Completion colors
    EXPECT_EQ(file_theme.comp_builtin,     def.comp_builtin);
    EXPECT_EQ(file_theme.comp_command,     def.comp_command);
    EXPECT_EQ(file_theme.comp_directory,   def.comp_directory);
}
