#ifndef TASH_UI_CLIPBOARD_H
#define TASH_UI_CLIPBOARD_H

#include <string>

// ── Base64 encoding ──────────────────────────────────────────────

std::string base64_encode(const std::string &input);

// ── OSC 52 escape sequence ──────────────────────────────────────

std::string osc52_encode(const std::string &text);

// ── Clipboard operations ────────────────────────────────────────

bool copy_to_clipboard(const std::string &text);
std::string paste_from_clipboard();

// ── Paste protection ────────────────────────────────────────────

bool is_multiline(const std::string &text);

// ── Helpers ─────────────────────────────────────────────────────

std::string popen_read(const std::string &cmd);
int popen_write(const std::string &cmd, const std::string &input);

#endif // TASH_UI_CLIPBOARD_H
