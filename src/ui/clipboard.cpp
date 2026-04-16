#include "tash/ui/clipboard.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>

// ═══════════════════════════════════════════════════════════════
// Base64 encoding
// ═══════════════════════════════════════════════════════════════

static const char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const std::string &input) {
    std::string output;
    size_t len = input.size();
    output.reserve(((len + 2) / 3) * 4);

    for (size_t i = 0; i < len; i += 3) {
        unsigned int octet_a = static_cast<unsigned char>(input[i]);
        unsigned int octet_b = (i + 1 < len) ? static_cast<unsigned char>(input[i + 1]) : 0;
        unsigned int octet_c = (i + 2 < len) ? static_cast<unsigned char>(input[i + 2]) : 0;

        unsigned int triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        output += base64_chars[(triple >> 18) & 0x3F];
        output += base64_chars[(triple >> 12) & 0x3F];
        output += (i + 1 < len) ? base64_chars[(triple >> 6) & 0x3F] : '=';
        output += (i + 2 < len) ? base64_chars[triple & 0x3F] : '=';
    }

    return output;
}

// ═══════════════════════════════════════════════════════════════
// OSC 52 escape sequence
// ═══════════════════════════════════════════════════════════════

std::string osc52_encode(const std::string &text) {
    std::string result;
    result += "\033]52;c;";
    result += base64_encode(text);
    result += "\a";
    return result;
}

// ═══════════════════════════════════════════════════════════════
// popen helpers
// ═══════════════════════════════════════════════════════════════

std::string popen_read(const std::string &cmd) {
    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return "";
    }

    std::string result;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }

    pclose(pipe);
    return result;
}

int popen_write(const std::string &cmd, const std::string &input) {
    FILE *pipe = popen(cmd.c_str(), "w");
    if (!pipe) {
        return -1;
    }

    size_t written = fwrite(input.data(), 1, input.size(), pipe);
    int status = pclose(pipe);

    if (written != input.size()) {
        return -1;
    }

    return status;
}

// ═══════════════════════════════════════════════════════════════
// Clipboard operations
// ═══════════════════════════════════════════════════════════════

bool copy_to_clipboard(const std::string &text) {
    // Try OSC 52 first — write escape sequence to stdout
    std::string osc = osc52_encode(text);
    std::cout << osc << std::flush;

    // Fall back to platform-specific clipboard commands
#ifdef __APPLE__
    if (popen_write("pbcopy", text) == 0) {
        return true;
    }
#else
    // Try xclip first
    if (popen_write("xclip -selection clipboard", text) == 0) {
        return true;
    }
    // Try xsel
    if (popen_write("xsel --clipboard --input", text) == 0) {
        return true;
    }
    // Try wl-copy (Wayland)
    if (popen_write("wl-copy", text) == 0) {
        return true;
    }
#endif

    return false;
}

std::string paste_from_clipboard() {
#ifdef __APPLE__
    return popen_read("pbpaste");
#else
    // Try xclip first
    std::string result = popen_read("xclip -selection clipboard -o");
    if (!result.empty()) {
        return result;
    }
    // Try xsel
    result = popen_read("xsel --clipboard --output");
    if (!result.empty()) {
        return result;
    }
    // Try wl-paste (Wayland)
    result = popen_read("wl-paste");
    return result;
#endif
}

// ═══════════════════════════════════════════════════════════════
// Paste protection
// ═══════════════════════════════════════════════════════════════

bool is_multiline(const std::string &text) {
    return text.find('\n') != std::string::npos;
}
