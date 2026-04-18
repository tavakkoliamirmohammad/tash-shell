// UI + terminal-output builtins: clear, copy, paste, linkify, block, table.

#include "tash/builtins.h"
#include "tash/core/executor.h"
#include "tash/core/signals.h"
#include "tash/ui/block_renderer.h"
#include "tash/ui/clipboard.h"
#include "tash/ui/rich_output.h"

#include <chrono>
#include <unistd.h>

using namespace std;

namespace {

string read_stdin_to_string() {
    string text;
    char buf[4096];
    while (true) {
        ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
        if (n <= 0) break;
        text.append(buf, n);
    }
    return text;
}

} // anonymous namespace

int builtin_clear(const vector<string> &, ShellState &) {
    // Terminal control (clear screen + cursor home), not a theme color.
    write_stdout("\033[2J\033[H");
    return 0;
}

int builtin_copy(const vector<string> &argv, ShellState &) {
    string text;
    if (argv.size() > 1) {
        for (size_t i = 1; i < argv.size(); i++) {
            if (i > 1) text += " ";
            text += argv[i];
        }
    } else {
        // Read from stdin until EOF (allows `echo foo | copy`).
        char buf[4096];
        while (true) {
            ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
            if (n <= 0) break;
            text.append(buf, n);
        }
    }
    if (!copy_to_clipboard(text)) {
        write_stderr("copy: failed to write to clipboard\n");
        return 1;
    }
    return 0;
}

int builtin_paste(const vector<string> &, ShellState &) {
    string text = paste_from_clipboard();
    if (text.empty()) {
        write_stderr("paste: clipboard is empty or unavailable\n");
        return 1;
    }
    if (!text.empty() && text.back() != '\n') text += '\n';
    write_stdout(text);
    return 0;
}

int builtin_linkify(const vector<string> &argv, ShellState &) {
    string text;
    if (argv.size() > 1) {
        for (size_t i = 1; i < argv.size(); i++) {
            if (i > 1) text += " ";
            text += argv[i];
        }
        text += "\n";
    } else {
        text = read_stdin_to_string();
    }
    write_stdout(tash::ui::linkify_urls(text));
    return 0;
}

// `block <command...>` runs the command and wraps its output with a
// header line (command + duration + ✓/✗) and a footer separator.
int builtin_block(const vector<string> &argv, ShellState &state) {
    if (argv.size() < 2) {
        write_stderr("block: usage: block <command> [args...]\n");
        return 1;
    }

    string cmd_display;
    for (size_t i = 1; i < argv.size(); i++) {
        if (i > 1) cmd_display += " ";
        cmd_display += argv[i];
    }

    auto t0 = std::chrono::steady_clock::now();
    int result = execute_single_command(cmd_display, state);
    auto t1 = std::chrono::steady_clock::now();
    double duration = std::chrono::duration<double>(t1 - t0).count();

    Block b;
    b.command = cmd_display;
    b.exit_code = result;
    b.duration_seconds = duration;
    write_stdout(render_block_header(b) + "\n");
    write_stdout(render_block_separator() + "\n");
    return result;
}

int builtin_table(const vector<string> &argv, ShellState &) {
    size_t max_width = 60;
    for (size_t i = 1; i + 1 < argv.size(); i++) {
        if (argv[i] == "--max-width") {
            try { max_width = static_cast<size_t>(std::stoi(argv[i + 1])); }
            catch (...) {}
        }
    }

    string text = read_stdin_to_string();
    if (text.empty()) {
        write_stderr("table: no input on stdin\n");
        return 1;
    }
    auto data = tash::ui::parse_table_output(text);
    if (data.headers.empty() || data.rows.empty()) {
        write_stdout(text);
        return 0;
    }

    auto truncate = [&](std::string &cell) {
        if (cell.size() > max_width) {
            cell.resize(max_width > 1 ? max_width - 1 : max_width);
            cell += "\xe2\x80\xa6"; // UTF-8 "…"
        }
    };
    for (auto &h : data.headers) truncate(h);
    for (auto &row : data.rows)
        for (auto &c : row) truncate(c);

    write_stdout(tash::ui::render_table(data));
    return 0;
}
