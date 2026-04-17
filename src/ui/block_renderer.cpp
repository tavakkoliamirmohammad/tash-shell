#include "tash/ui/block_renderer.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <sstream>
#include <chrono>

#ifdef __unix__
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <sys/ioctl.h>
#include <unistd.h>
#endif

// ── ANSI escape helpers (raw, not readline-wrapped) ───────────

static const char *ANSI_RESET = "\x1B[0m";
static const char *ANSI_BOLD  = "\x1B[1m";
static const char *ANSI_DIM   = "\x1B[2m";
static const char *ANSI_GREEN = "\x1B[32m";
static const char *ANSI_RED   = "\x1B[31m";

// Unicode box-drawing horizontal line: U+2500
static const char *BOX_H = "\xe2\x94\x80";  // ─

// Status symbols
static const char *CHECK_MARK = "\xe2\x9c\x93";  // ✓
static const char *CROSS_MARK = "\xe2\x9c\x97";  // ✗

// ── Terminal width detection ──────────────────────────────────

int get_terminal_width() {
#if defined(__unix__) || defined(__APPLE__)
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        return ws.ws_col;
    }
#endif
    // Fallback: check COLUMNS environment variable
    const char *cols_env = std::getenv("COLUMNS");
    if (cols_env) {
        int cols = std::atoi(cols_env);
        if (cols > 0) {
            return cols;
        }
    }
    return 80;
}

// ── Duration formatting ───────────────────────────────────────

std::string format_duration(double seconds) {
    if (seconds < 0.0) {
        seconds = 0.0;
    }

    if (seconds < 60.0) {
        // Sub-minute: show with 2 decimal places
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.2fs", seconds);
        return std::string(buf);
    }

    if (seconds < 3600.0) {
        // Minutes + seconds
        int total_sec = static_cast<int>(seconds);
        int mins = total_sec / 60;
        int secs = total_sec % 60;
        std::ostringstream oss;
        oss << mins << "m " << secs << "s";
        return oss.str();
    }

    // Hours + minutes
    int total_sec = static_cast<int>(seconds);
    int hours = total_sec / 3600;
    int mins = (total_sec % 3600) / 60;
    std::ostringstream oss;
    oss << hours << "h " << mins << "m";
    return oss.str();
}

// ── Helper: repeat a UTF-8 string n times ─────────────────────

static std::string repeat_str(const char *s, int n) {
    std::string result;
    for (int i = 0; i < n; ++i) {
        result += s;
    }
    return result;
}

// ── Block header rendering ────────────────────────────────────
//
// Format: "─── <command> ──────────── <duration> <status> ───"
// <command> in bold
// <duration> in dim
// <status>: green "✓" or red "✗"
// Padded with ─ to fill terminal width

std::string render_block_header(const Block &block) {
    int width = get_terminal_width();

    // Build the status indicator
    std::string status_str;
    if (block.exit_code == 0) {
        status_str = std::string(ANSI_GREEN) + CHECK_MARK + ANSI_RESET;
    } else {
        status_str = std::string(ANSI_RED) + CROSS_MARK + ANSI_RESET;
    }

    // Build the duration string (dim)
    std::string dur = format_duration(block.duration_seconds);
    std::string dur_str = std::string(ANSI_DIM) + dur + ANSI_RESET;

    // Build the command string (bold)
    std::string cmd_str = std::string(ANSI_BOLD) + block.command + ANSI_RESET;

    // Prefix: "─── "
    std::string prefix = repeat_str(BOX_H, 3) + " ";

    // Suffix: " <duration> <status> ───"
    std::string suffix = " " + dur_str + " " + status_str + " " + repeat_str(BOX_H, 3);

    // Calculate visible lengths for padding
    // prefix visible: 3 (box chars) + 1 (space) = 4
    size_t prefix_vis = 4;
    // command visible: length of command text
    size_t cmd_vis = block.command.size();
    // suffix visible: 1 (space) + dur.size() + 1 (space) + 1 (check/cross) + 1 (space) + 3 (box) = dur.size() + 7
    size_t suffix_vis = dur.size() + 7;

    // Middle padding fills remaining space
    size_t used = prefix_vis + cmd_vis + suffix_vis + 1; // +1 for space after command
    int pad_count = 0;
    if (used < static_cast<size_t>(width)) {
        pad_count = static_cast<int>(static_cast<size_t>(width) - used);
    }

    std::string mid_pad = " " + repeat_str(BOX_H, pad_count);

    return prefix + cmd_str + mid_pad + suffix;
}

// ── Block separator ───────────────────────────────────────────

std::string render_block_separator() {
    int width = get_terminal_width();
    return repeat_str(BOX_H, width);
}

// ── BlockManager implementation ───────────────────────────────

void BlockManager::start_block(const std::string &command) {
    Block b;
    b.command = command;
    b.exit_code = 0;
    b.duration_seconds = 0.0;
    b.folded = false;
    b.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    blocks_.push_back(b);
}

void BlockManager::end_block(const std::string &output, int exit_code, double duration) {
    if (blocks_.empty()) {
        return;
    }
    Block &b = blocks_.back();
    b.output = output;
    b.exit_code = exit_code;
    b.duration_seconds = duration;
}

const std::vector<Block> &BlockManager::blocks() const {
    return blocks_;
}

void BlockManager::fold(size_t index) {
    if (index < blocks_.size()) {
        blocks_[index].folded = true;
    }
}

void BlockManager::unfold(size_t index) {
    if (index < blocks_.size()) {
        blocks_[index].folded = false;
    }
}

void BlockManager::fold_all() {
    for (size_t i = 0; i < blocks_.size(); ++i) {
        blocks_[i].folded = true;
    }
}

void BlockManager::unfold_all() {
    for (size_t i = 0; i < blocks_.size(); ++i) {
        blocks_[i].folded = false;
    }
}

std::string BlockManager::render_block(size_t index) const {
    if (index >= blocks_.size()) {
        return "";
    }
    const Block &b = blocks_[index];
    std::string result = render_block_header(b);
    if (!b.folded && !b.output.empty()) {
        result += "\n" + b.output;
    }
    return result;
}

size_t BlockManager::block_count() const {
    return blocks_.size();
}
