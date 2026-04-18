// ── trap ───────────────────────────────────────────────────────
//
// POSIX-style signal/exit trap builtin, split out of the old shell.cpp
// blob to keep signal-handling code in one place. Supported forms:
//
//   trap                      list current traps
//   trap 'cmd' SIG1 [SIG2...] install `cmd` as the handler for each signal
//   trap - SIG1 [SIG2...]     remove handler (restore default)
//   trap '' SIG1 [SIG2...]    ignore the signal
//   trap 'cmd' EXIT           run `cmd` on shell exit (pseudo-signal 0)
//
// Signal names are recognized with or without the SIG prefix; numeric
// signums are accepted too. Unknown names produce an error.

#include "tash/builtins.h"
#include "tash/core/signals.h"
#include <csignal>
#include <cstdio>

using namespace std;

namespace {

int signal_from_name(const string &raw) {
    if (raw.empty()) return -1;
    if (raw == "EXIT" || raw == "exit" || raw == "0") return 0;

    string name = raw;
    // Strip optional "SIG" prefix.
    if (name.size() >= 3 &&
        (name.substr(0, 3) == "SIG" || name.substr(0, 3) == "sig")) {
        name = name.substr(3);
    }
    // Uppercase for comparison.
    for (char &c : name) {
        if (c >= 'a' && c <= 'z') c -= 32;
    }

    // Purely numeric → accept.
    bool all_digits = !name.empty();
    for (char c : name) {
        if (c < '0' || c > '9') { all_digits = false; break; }
    }
    if (all_digits) {
        try { return stoi(name); }
        catch (...) { return -1; }
    }

    if (name == "HUP")  return SIGHUP;
    if (name == "INT")  return SIGINT;
    if (name == "QUIT") return SIGQUIT;
    if (name == "TERM") return SIGTERM;
    if (name == "USR1") return SIGUSR1;
    if (name == "USR2") return SIGUSR2;
    return -1;
}

string signal_to_name(int signum) {
    switch (signum) {
        case 0:        return "EXIT";
        case SIGHUP:   return "SIGHUP";
        case SIGINT:   return "SIGINT";
        case SIGQUIT: return "SIGQUIT";
        case SIGTERM: return "SIGTERM";
        case SIGUSR1: return "SIGUSR1";
        case SIGUSR2: return "SIGUSR2";
        default: {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", signum);
            return buf;
        }
    }
}

} // anonymous namespace

int builtin_trap(const vector<string> &argv, ShellState &state) {
    // `trap` with no args: list.
    if (argv.size() == 1) {
        for (const auto &kv : state.exec.traps) {
            write_stdout("trap -- '" + kv.second + "' " +
                         signal_to_name(kv.first) + "\n");
        }
        return 0;
    }

    // Need at least one signal after the command.
    if (argv.size() < 3) {
        write_stderr("trap: usage: trap [cmd] signal [signal...]\n");
        return 1;
    }

    const string &action = argv[1];
    // Accumulate all signum targets; only apply if all resolve.
    vector<int> signums;
    signums.reserve(argv.size() - 2);
    for (size_t i = 2; i < argv.size(); ++i) {
        int s = signal_from_name(argv[i]);
        if (s < 0) {
            write_stderr("trap: invalid signal: " + argv[i] + "\n");
            return 1;
        }
        signums.push_back(s);
    }

    if (action == "-") {
        // Remove handler(s): clear map entry + restore default.
        for (int s : signums) {
            state.exec.traps.erase(s);
            if (s > 0) uninstall_trap_handler(s);
        }
        return 0;
    }

    // Store the command (possibly empty for the "ignore" form).
    for (int s : signums) {
        state.exec.traps[s] = action;
        if (s == 0) continue;         // EXIT is fired by builtin_exit
        if (action.empty()) {
            ignore_signal(s);
        } else {
            install_trap_handler(s);
        }
    }
    return 0;
}
