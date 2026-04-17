#include "theme.h"
#include "tash/util/config_resolver.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

// ── Globals ───────────────────────────────────────────────────

Theme g_current_theme = Theme::default_theme();
std::string g_current_theme_name = "catppuccin-mocha";

std::string PROMPT_USER, PROMPT_PATH, PROMPT_BRANCH, PROMPT_GIT_DIRTY;
std::string PROMPT_SEPARATOR, PROMPT_TEXT, PROMPT_ARROW_OK, PROMPT_ARROW_ERR;
std::string PROMPT_DURATION;

std::string SYN_CMD_VALID, SYN_CMD_BUILTIN, SYN_CMD_INVALID, SYN_STRING;
std::string SYN_VARIABLE, SYN_OPERATOR, SYN_REDIRECT, SYN_COMMENT;

std::string BANNER_FRAME, BANNER_LOGO, BANNER_TITLE, BANNER_VERSION;
std::string BANNER_HINT, BANNER_TEXT, BANNER_FEATURE;

std::string SUGGEST_TEXT, SUGGEST_CMD;

std::string AI_LABEL, AI_SEPARATOR, AI_CMD, AI_ERROR, AI_PROMPT;
std::string AI_STEP_NUM, AI_FLAG;

std::string CAT_GREEN, CAT_YELLOW, CAT_RED;

// ── ANSI helpers ──────────────────────────────────────────────

std::string ansi_fg(const RGB &c) {
    return "\033[38;2;" + std::to_string(c.r) + ";" +
           std::to_string(c.g) + ";" + std::to_string(c.b) + "m";
}

std::string ansi_bg(const RGB &c) {
    return "\033[48;2;" + std::to_string(c.r) + ";" +
           std::to_string(c.g) + ";" + std::to_string(c.b) + "m";
}

// ── Apply theme ───────────────────────────────────────────────

void apply_theme(const Theme &t, const std::string &name) {
    g_current_theme = t;
    if (!name.empty()) g_current_theme_name = name;

    const std::string B = CAT_BOLD;
    const std::string D = CAT_DIM;

    // Prompt
    PROMPT_USER       = B + ansi_fg(t.prompt_user);
    PROMPT_PATH       = B + ansi_fg(t.prompt_path);
    PROMPT_BRANCH     = B + ansi_fg(t.prompt_git);
    PROMPT_GIT_DIRTY  = ansi_fg(t.string_color);
    PROMPT_SEPARATOR  = ansi_fg(t.prompt_separator);
    PROMPT_TEXT       = ansi_fg(t.comp_description);
    PROMPT_ARROW_OK   = ansi_fg(t.prompt_success);
    PROMPT_ARROW_ERR  = ansi_fg(t.prompt_error);
    PROMPT_DURATION   = D + ansi_fg(t.prompt_duration);

    // Syntax
    SYN_CMD_VALID    = ansi_fg(t.command_valid);
    SYN_CMD_BUILTIN  = B + ansi_fg(t.command_builtin);
    SYN_CMD_INVALID  = ansi_fg(t.command_invalid);
    SYN_STRING       = ansi_fg(t.string_color);
    SYN_VARIABLE     = ansi_fg(t.variable);
    SYN_OPERATOR     = ansi_fg(t.op);
    SYN_REDIRECT     = ansi_fg(t.redirect);
    SYN_COMMENT      = ansi_fg(t.comment);

    // Banner
    BANNER_FRAME    = B + ansi_fg(t.prompt_path);
    BANNER_LOGO     = B + ansi_fg(t.prompt_git);
    BANNER_TITLE    = B + ansi_fg(t.prompt_user);
    BANNER_VERSION  = ansi_fg(t.prompt_duration);
    BANNER_HINT     = ansi_fg(t.prompt_success);
    BANNER_TEXT     = ansi_fg(t.comp_description);
    BANNER_FEATURE  = ansi_fg(t.prompt_success);

    // Suggestions
    SUGGEST_TEXT = D + ansi_fg(t.string_color);
    SUGGEST_CMD  = B + ansi_fg(t.prompt_duration);

    // AI
    AI_LABEL     = B + ansi_fg(t.command_builtin);
    AI_SEPARATOR = D;
    AI_CMD       = ansi_fg(t.command_valid);
    AI_ERROR     = ansi_fg(t.prompt_error);
    AI_PROMPT    = ansi_fg(t.op);
    AI_STEP_NUM  = ansi_fg(t.op);
    AI_FLAG      = ansi_fg(t.string_color);

    // Direct aliases
    CAT_GREEN  = ansi_fg(t.prompt_success);
    CAT_YELLOW = ansi_fg(t.string_color);
    CAT_RED    = ansi_fg(t.prompt_error);
}

// ── Theme discovery ───────────────────────────────────────────

static std::string home_theme_dir() {
    return tash::config::get_user_themes_dir();
}

static std::string user_theme_file() {
    return tash::config::get_theme_toml_path();
}

std::vector<std::string> theme_search_dirs() {
    std::vector<std::string> dirs;
    if (const char *env = getenv("TASH_THEMES_DIR")) {
        if (*env) dirs.push_back(env);
    }
    std::string user_dir = home_theme_dir();
    if (!user_dir.empty()) dirs.push_back(user_dir);
#ifdef TASH_THEMES_DIR
    dirs.push_back(TASH_THEMES_DIR);
#endif
    return dirs;
}

static bool has_suffix(const std::string &s, const std::string &suffix) {
    if (s.size() < suffix.size()) return false;
    return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<std::string> list_available_themes() {
    std::vector<std::string> names;
    for (const auto &dir : theme_search_dirs()) {
        DIR *dp = opendir(dir.c_str());
        if (!dp) continue;
        struct dirent *entry;
        while ((entry = readdir(dp)) != nullptr) {
            std::string name = entry->d_name;
            if (has_suffix(name, ".toml")) {
                std::string base = name.substr(0, name.size() - 5);
                if (std::find(names.begin(), names.end(), base) == names.end()) {
                    names.push_back(base);
                }
            }
        }
        closedir(dp);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::string find_theme_file(const std::string &name) {
    for (const auto &dir : theme_search_dirs()) {
        std::string path = dir + "/" + name + ".toml";
        struct stat st;
        if (stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            return path;
        }
    }
    return "";
}

static bool mkdir_p(const std::string &path) {
    if (path.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (!ec) return true;
    return std::filesystem::is_directory(path, ec);
}

static bool copy_file(const std::string &src, const std::string &dst) {
    std::ifstream in(src, std::ios::binary);
    if (!in) return false;
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << in.rdbuf();
    return out.good();
}

static std::string name_marker_file() {
    return tash::config::get_theme_name_path();
}

void load_user_theme() {
    std::string path = user_theme_file();
    struct stat st;
    if (!path.empty() && stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
        Theme t = Theme::load_from_file(path);
        std::string name;
        std::ifstream marker(name_marker_file());
        if (marker) std::getline(marker, name);
        if (name.empty()) name = t.name.empty() ? std::string("custom") : t.name;
        apply_theme(t, name);
        return;
    }
    apply_theme(Theme::default_theme(), "catppuccin-mocha");
}

bool set_active_theme(const std::string &name, std::string &error_out) {
    std::string src = find_theme_file(name);
    if (src.empty()) {
        error_out = "theme not found: " + name;
        return false;
    }
    std::string dst = user_theme_file();
    if (dst.empty()) {
        error_out = "HOME is not set";
        return false;
    }
    // Ensure ~/.config/tash exists.
    size_t slash = dst.find_last_of('/');
    if (slash != std::string::npos) {
        if (!mkdir_p(dst.substr(0, slash))) {
            error_out = "cannot create config directory";
            return false;
        }
    }
    if (!copy_file(src, dst)) {
        error_out = "failed to write " + dst;
        return false;
    }
    // Persist the basename so subsequent sessions know which bundled theme is active.
    std::ofstream marker(name_marker_file(), std::ios::trunc);
    if (marker) marker << name;
    Theme t = Theme::load_from_file(dst);
    apply_theme(t, name);
    return true;
}
