#include "tash/util/io.h"

#include <atomic>
#include <cctype>
#include <string>
#include <unistd.h>

namespace tash::io {

// Global level, defaulted to Info so calls that land before
// set_log_level() runs at startup behave sensibly.
static std::atomic<Level> g_level{Level::Info};

// Cache TTY detection once at first use. STDERR_FILENO can't realistically
// be replaced mid-run (we don't ourselves dup2 over it from the parent
// path), and avoiding repeated isatty() calls in hot error paths is nice.
static bool stderr_is_tty() {
    static const bool tty = ::isatty(STDERR_FILENO) != 0;
    return tty;
}

// ANSI color escapes — kept inline (no dependency on theme.h) so any
// caller that links only this TU (e.g. narrow unit tests) still works.
static constexpr const char *ANSI_RED   = "\033[31m";
static constexpr const char *ANSI_YELLOW = "\033[33m";
static constexpr const char *ANSI_DIM   = "\033[2m";
static constexpr const char *ANSI_RESET = "\033[0m";

static void emit(Level lvl, std::string_view msg) {
    if (lvl < g_level.load(std::memory_order_relaxed)) return;

    // Build the line: "tash: <tag>: <msg>\n". Info has no tag per spec,
    // just "tash: <msg>\n".
    std::string line;
    line.reserve(msg.size() + 32);

    const bool tty = stderr_is_tty();
    const char *color = nullptr;
    const char *tag = nullptr;
    switch (lvl) {
        case Level::Error:   color = ANSI_RED;    tag = "error";   break;
        case Level::Warning: color = ANSI_YELLOW; tag = "warning"; break;
        case Level::Debug:   color = ANSI_DIM;    tag = nullptr;   break;
        case Level::Info:    color = nullptr;     tag = nullptr;   break;
    }

    line += "tash: ";
    if (tag) {
        if (tty && color) line += color;
        line += tag;
        if (tty && color) line += ANSI_RESET;
        line += ": ";
        line.append(msg.data(), msg.size());
    } else if (lvl == Level::Debug && tty && color) {
        line += color;
        line.append(msg.data(), msg.size());
        line += ANSI_RESET;
    } else {
        line.append(msg.data(), msg.size());
    }
    line += '\n';

    // write(2) directly — std::cerr can be redirected/buffered in odd ways,
    // and this mirrors the existing write_stderr() helper.
    ssize_t n = ::write(STDERR_FILENO, line.data(), line.size());
    (void)n;
}

void error(std::string_view msg)   { emit(Level::Error, msg); }
void warning(std::string_view msg) { emit(Level::Warning, msg); }
void info(std::string_view msg)    { emit(Level::Info, msg); }
void debug(std::string_view msg)   { emit(Level::Debug, msg); }

void set_log_level(Level l) {
    g_level.store(l, std::memory_order_relaxed);
}

Level current_log_level() {
    return g_level.load(std::memory_order_relaxed);
}

Level parse_log_level(std::string_view s) {
    // Lowercase copy — ASCII-only is fine, level names are ASCII.
    std::string lo;
    lo.reserve(s.size());
    for (char c : s) lo.push_back(static_cast<char>(std::tolower(
        static_cast<unsigned char>(c))));

    if (lo == "debug")   return Level::Debug;
    if (lo == "info")    return Level::Info;
    if (lo == "warning" || lo == "warn") return Level::Warning;
    if (lo == "error")   return Level::Error;
    return Level::Info;
}

} // namespace tash::io
