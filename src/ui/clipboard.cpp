#include "tash/ui/clipboard.h"
#include "tash/util/safe_exec.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

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
// Argv-based subprocess helpers
// ---------------------------------------------------------------
// Previously these used popen(cmd_string, ...). For the clipboard
// helpers the command string was static, so there was no concrete
// injection bug, but funnelling through /bin/sh just to copy/paste a
// buffer pulled in a whole shell process for no reason. The
// argv-vector approach matches the rest of the shell's subprocess
// hygiene and avoids the implicit shell entirely.
// ═══════════════════════════════════════════════════════════════

static std::string argv_read(const std::vector<std::string> &argv) {
    auto r = tash::util::safe_exec(argv);
    return (r.exit_code == 0) ? r.stdout_text : std::string();
}

// Fork + exec argv with `input` piped to stdin. Returns 0 on success,
// -1 on failure. Used to feed text into pbcopy/xclip/xsel/wl-copy.
static int argv_write(const std::vector<std::string> &argv,
                      const std::string &input) {
    if (argv.empty()) return -1;

    int pfd[2] = {-1, -1};
    if (::pipe(pfd) < 0) return -1;

    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pfd[0]);
        ::close(pfd[1]);
        return -1;
    }

    if (pid == 0) {
        ::close(pfd[1]);
        if (pfd[0] != STDIN_FILENO) {
            ::dup2(pfd[0], STDIN_FILENO);
            ::close(pfd[0]);
        }
        // Silence the child's stderr so missing clipboard helpers don't
        // spam the user's terminal -- a non-zero exit means "try the
        // next fallback".
        int devnull = ::open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            ::dup2(devnull, STDERR_FILENO);
            ::close(devnull);
        }
        std::vector<char *> c_argv;
        c_argv.reserve(argv.size() + 1);
        for (const auto &s : argv) {
            c_argv.push_back(const_cast<char *>(s.c_str()));
        }
        c_argv.push_back(nullptr);
        ::execvp(c_argv[0], c_argv.data());
        _exit(127);
    }

    ::close(pfd[0]);
    size_t off = 0;
    while (off < input.size()) {
        ssize_t w = ::write(pfd[1], input.data() + off, input.size() - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            break;
        }
        off += static_cast<size_t>(w);
    }
    ::close(pfd[1]);

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) break;
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0 && off == input.size()) {
        return 0;
    }
    return -1;
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
    if (argv_write({"pbcopy"}, text) == 0) {
        return true;
    }
#else
    // Try xclip first
    if (argv_write({"xclip", "-selection", "clipboard"}, text) == 0) {
        return true;
    }
    // Try xsel
    if (argv_write({"xsel", "--clipboard", "--input"}, text) == 0) {
        return true;
    }
    // Try wl-copy (Wayland)
    if (argv_write({"wl-copy"}, text) == 0) {
        return true;
    }
#endif

    return false;
}

std::string paste_from_clipboard() {
#ifdef __APPLE__
    return argv_read({"pbpaste"});
#else
    // Try xclip first
    std::string result = argv_read({"xclip", "-selection", "clipboard", "-o"});
    if (!result.empty()) {
        return result;
    }
    // Try xsel
    result = argv_read({"xsel", "--clipboard", "--output"});
    if (!result.empty()) {
        return result;
    }
    // Try wl-paste (Wayland)
    result = argv_read({"wl-paste"});
    return result;
#endif
}

// ═══════════════════════════════════════════════════════════════
// Paste protection
// ═══════════════════════════════════════════════════════════════

bool is_multiline(const std::string &text) {
    return text.find('\n') != std::string::npos;
}
