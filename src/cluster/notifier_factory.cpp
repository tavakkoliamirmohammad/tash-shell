// Per-platform INotifier: osascript on macOS, notify-send on Linux,
// NoOp elsewhere. Both real impls compile on every platform so tests
// can drive them cross-host; make_notifier() picks the one matching
// the target triple.

#include "tash/cluster/notifier_factory.h"

#include "tash/util/safe_exec.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace tash::cluster {

// ══════════════════════════════════════════════════════════════════════════════
// Shared helpers
// ══════════════════════════════════════════════════════════════════════════════

namespace {

void fire_and_forget(const std::vector<std::string>& argv) {
    // Block briefly so the child has time to actually run + log (tests
    // read the log after calling desktop()). One second is a lot for
    // osascript / notify-send; both should finish in tens of ms.
    (void)tash::util::safe_exec(argv, /*timeout_ms*/1000,
                                   /*suppress_stderr*/true);
}

// AppleScript string literal escaping: " -> \", \ -> \\.
std::string escape_applescript(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

// Ring the terminal bell. Non-platform-specific — a lone BEL byte
// on stderr is the terminal-protocol way to attract attention.
void ring_bell() {
    std::fputc('\a', stderr);
    std::fflush(stderr);
}

// ── macOS: osascript ─────────────────────────────────────────

class MacNotifier : public INotifier {
public:
    void desktop(const std::string& title, const std::string& body) override {
        const std::string script =
            "display notification \"" + escape_applescript(body) +
            "\" with title \""        + escape_applescript(title) + "\"";
        fire_and_forget({"osascript", "-e", script});
    }
    void bell() override { ring_bell(); }
};

// ── Linux: notify-send ───────────────────────────────────────

class LinuxNotifier : public INotifier {
public:
    void desktop(const std::string& title, const std::string& body) override {
        // notify-send takes title + body as positional args — no shell
        // parsing, so no quoting needed inside argv.
        fire_and_forget({"notify-send", title, body});
    }
    void bell() override { ring_bell(); }
};

// ── Last-ditch: NoOp (non-Linux, non-macOS builds) ───────────

class NoOpNotifier : public INotifier {
public:
    void desktop(const std::string&, const std::string&) override {}
    void bell() override { ring_bell(); }
};

}  // namespace

// ══════════════════════════════════════════════════════════════════════════════
// Public factory + test-only cross-platform accessors
// ══════════════════════════════════════════════════════════════════════════════

std::unique_ptr<INotifier> make_mac_notifier_for_testing()   { return std::make_unique<MacNotifier>();   }
std::unique_ptr<INotifier> make_linux_notifier_for_testing() { return std::make_unique<LinuxNotifier>(); }

std::unique_ptr<INotifier> make_notifier() {
#if defined(__APPLE__)
    return std::make_unique<MacNotifier>();
#elif defined(__linux__)
    return std::make_unique<LinuxNotifier>();
#else
    return std::make_unique<NoOpNotifier>();
#endif
}

}  // namespace tash::cluster
