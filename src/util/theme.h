#ifndef THEME_H
#define THEME_H

#include <string>
#include <vector>
#include "tash/plugins/theme_provider.h"

// Theme-independent text modifiers (safe as string literals).
#define CAT_BOLD   "\033[1m"
#define CAT_DIM    "\033[2m"
#define CAT_ITALIC "\033[3m"
#define CAT_RESET  "\033[0m"

// ── Global runtime theme ──────────────────────────────────────
extern Theme g_current_theme;
extern std::string g_current_theme_name;

// Build an ANSI 24-bit SGR escape from an RGB triplet.
std::string ansi_fg(const RGB &c);
std::string ansi_bg(const RGB &c);

// Rebuild the semantic-role strings below from the given theme.
void apply_theme(const Theme &t, const std::string &name = "");

// Load ~/.config/tash/theme.toml (if present) and apply it. Falls back to default.
void load_user_theme();

// Persist the named bundled theme as the active theme (copies the TOML file to
// ~/.config/tash/theme.toml and applies it in-process). Returns true on success.
bool set_active_theme(const std::string &name, std::string &error_out);

// Resolved directories containing *.toml theme files. Order: user dir first.
std::vector<std::string> theme_search_dirs();

// Enumerate all available theme basenames (without .toml), deduplicated.
std::vector<std::string> list_available_themes();

// Find the TOML file path for a theme name, or empty string if not found.
std::string find_theme_file(const std::string &name);

// ── Semantic roles (rebuilt by apply_theme) ────────────────────
// These contain ANSI escape sequences; concatenate with + when mixing with
// adjacent string literals (e.g., PROMPT_USER + "user" + CAT_RESET).

// Prompt
extern std::string PROMPT_USER;
extern std::string PROMPT_PATH;
extern std::string PROMPT_BRANCH;
extern std::string PROMPT_GIT_DIRTY;
extern std::string PROMPT_SEPARATOR;
extern std::string PROMPT_TEXT;
extern std::string PROMPT_ARROW_OK;
extern std::string PROMPT_ARROW_ERR;
extern std::string PROMPT_DURATION;

// Syntax highlighting
extern std::string SYN_CMD_VALID;
extern std::string SYN_CMD_BUILTIN;
extern std::string SYN_CMD_INVALID;
extern std::string SYN_STRING;
extern std::string SYN_VARIABLE;
extern std::string SYN_OPERATOR;
extern std::string SYN_REDIRECT;
extern std::string SYN_COMMENT;

// Banner
extern std::string BANNER_FRAME;
extern std::string BANNER_LOGO;
extern std::string BANNER_TITLE;
extern std::string BANNER_VERSION;
extern std::string BANNER_HINT;
extern std::string BANNER_TEXT;
extern std::string BANNER_FEATURE;

// Suggestions
extern std::string SUGGEST_TEXT;
extern std::string SUGGEST_CMD;

// AI output
extern std::string AI_LABEL;
extern std::string AI_SEPARATOR;
extern std::string AI_CMD;
extern std::string AI_ERROR;
extern std::string AI_PROMPT;
extern std::string AI_STEP_NUM;
extern std::string AI_FLAG;

// Direct Catppuccin color aliases (mapped to semantic theme fields).
extern std::string CAT_GREEN;
extern std::string CAT_YELLOW;
extern std::string CAT_RED;

#endif // THEME_H
