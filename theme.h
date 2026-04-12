#ifndef THEME_H
#define THEME_H

// ── Catppuccin Mocha Color Palette ─────────────────────────────
// A warm, classy dark theme. Uses 24-bit true color (RGB).
// https://catppuccin.com
//
// Format: \033[38;2;R;G;Bm  (foreground)
//         \033[48;2;R;G;Bm  (background)
//         \033[1m            (bold)
//         \033[2m            (dim)
//         \033[3m            (italic)
//         \033[0m            (reset)

// ── Accent colors ──────────────────────────────────────────────
#define CAT_ROSEWATER "\033[38;2;245;224;220m"
#define CAT_FLAMINGO  "\033[38;2;242;205;205m"
#define CAT_PINK      "\033[38;2;245;194;231m"
#define CAT_MAUVE     "\033[38;2;203;166;247m"
#define CAT_RED       "\033[38;2;243;139;168m"
#define CAT_MAROON    "\033[38;2;235;160;172m"
#define CAT_PEACH     "\033[38;2;250;179;135m"
#define CAT_YELLOW    "\033[38;2;249;226;175m"
#define CAT_GREEN     "\033[38;2;166;227;161m"
#define CAT_TEAL      "\033[38;2;148;226;213m"
#define CAT_SKY       "\033[38;2;137;220;235m"
#define CAT_SAPPHIRE  "\033[38;2;116;199;236m"
#define CAT_BLUE      "\033[38;2;137;180;250m"
#define CAT_LAVENDER  "\033[38;2;180;190;254m"

// ── Text colors ────────────────────────────────────────────────
#define CAT_TEXT      "\033[38;2;205;214;244m"
#define CAT_SUBTEXT1  "\033[38;2;186;194;222m"
#define CAT_SUBTEXT0  "\033[38;2;166;173;200m"
#define CAT_OVERLAY2  "\033[38;2;147;153;178m"
#define CAT_OVERLAY1  "\033[38;2;127;132;156m"
#define CAT_OVERLAY0  "\033[38;2;108;112;134m"
#define CAT_SURFACE2  "\033[38;2;88;91;112m"
#define CAT_SURFACE1  "\033[38;2;69;71;90m"
#define CAT_SURFACE0  "\033[38;2;49;50;68m"

// ── Base colors ────────────────────────────────────────────────
#define CAT_BASE      "\033[38;2;30;30;46m"
#define CAT_MANTLE    "\033[38;2;24;24;37m"
#define CAT_CRUST     "\033[38;2;17;17;27m"

// ── Bold variants ──────────────────────────────────────────────
#define CAT_BOLD      "\033[1m"
#define CAT_DIM       "\033[2m"
#define CAT_ITALIC    "\033[3m"
#define CAT_RESET     "\033[0m"

// ── Semantic color roles for the shell ─────────────────────────
// Prompt
#define PROMPT_USER       CAT_BOLD CAT_GREEN       // username
#define PROMPT_PATH       CAT_BOLD CAT_BLUE        // current directory
#define PROMPT_BRANCH     CAT_BOLD CAT_MAUVE       // git branch
#define PROMPT_GIT_DIRTY  CAT_YELLOW               // git status indicators
#define PROMPT_SEPARATOR  CAT_OVERLAY1              // box-drawing chars
#define PROMPT_TEXT       CAT_SUBTEXT1              // "in", "on" text
#define PROMPT_ARROW_OK   CAT_GREEN                 // ❯ on success
#define PROMPT_ARROW_ERR  CAT_RED                   // ❯ on failure
#define PROMPT_DURATION   CAT_DIM CAT_PEACH         // "took 3.2s"

// Syntax highlighting
#define SYN_CMD_VALID     CAT_GREEN                 // valid command
#define SYN_CMD_BUILTIN   CAT_BOLD CAT_TEAL         // builtin command
#define SYN_CMD_INVALID   CAT_RED                   // unknown command
#define SYN_STRING        CAT_YELLOW                // quoted strings
#define SYN_VARIABLE      CAT_SKY                   // $VAR, ${VAR}
#define SYN_OPERATOR      CAT_MAUVE                 // |, &&, ||, ;
#define SYN_REDIRECT      CAT_PEACH                 // >, >>, <, 2>
#define SYN_COMMENT       CAT_OVERLAY0              // # comments

// Banner
#define BANNER_LOGO       CAT_BOLD CAT_LAVENDER     // TASH ascii art
#define BANNER_TITLE      CAT_BOLD CAT_TEXT          // "Tavakkoli's Shell"
#define BANNER_VERSION    CAT_PEACH                  // "v1.0.0"
#define BANNER_HINT       CAT_GREEN                  // "exit", "history"
#define BANNER_TEXT       CAT_SUBTEXT0               // hint text

// Suggestions
#define SUGGEST_TEXT      CAT_DIM CAT_YELLOW         // "did you mean"
#define SUGGEST_CMD       CAT_BOLD CAT_PEACH         // suggested command

#endif // THEME_H
