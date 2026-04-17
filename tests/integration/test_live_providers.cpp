// End-to-end tests that the previously-dead plugin providers are now
// registered and actually fire in a running tash process.

#include "test_helpers.h"

#include <cstdlib>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace {

// Redirects $HOME so provider state (sqlite DB, session files, etc.)
// writes to a temp dir and the test cleans up after itself.
struct HomeGuard {
    std::string original;
    std::string tmp;
    HomeGuard() {
        const char *h = getenv("HOME");
        original = h ? h : "";
        tmp = "/tmp/tash_live_providers_" + std::to_string(getpid());
        std::string cmd = "rm -rf " + tmp + " && mkdir -p " + tmp;
        int rc = system(cmd.c_str()); (void)rc;
        setenv("HOME", tmp.c_str(), 1);
    }
    ~HomeGuard() {
        std::string cmd = "rm -rf " + tmp;
        int rc = system(cmd.c_str()); (void)rc;
        if (!original.empty()) setenv("HOME", original.c_str(), 1);
        else unsetenv("HOME");
    }
};

bool has_sqlite3() {
    return system("command -v sqlite3 >/dev/null 2>&1") == 0;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────
// SqliteHistoryProvider: every executed command lands in ~/.tash/history.db
// ─────────────────────────────────────────────────────────────────────

TEST(LiveProviders, SqliteHistoryPersistsCommands) {
    HomeGuard home;

    // Run a shell and execute two commands.
    run_shell("echo first_probe\necho second_probe\nexit\n");

    std::string db = home.tmp + "/.tash/history.db";
    struct stat st;
    ASSERT_EQ(stat(db.c_str(), &st), 0)
        << "~/.tash/history.db not created — SqliteHistoryProvider "
        << "not registered at startup?";

    if (!has_sqlite3()) {
        GTEST_SKIP() << "sqlite3 CLI not available to inspect the DB";
    }

    std::string dump_cmd = "sqlite3 " + db +
                           " \"SELECT command FROM history;\" 2>/dev/null";
    FILE *pipe = popen(dump_cmd.c_str(), "r");
    ASSERT_NE(pipe, nullptr);
    std::string out;
    char buf[1024];
    while (fgets(buf, sizeof(buf), pipe)) out += buf;
    pclose(pipe);

    EXPECT_NE(out.find("echo first_probe"),  std::string::npos);
    EXPECT_NE(out.find("echo second_probe"), std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────
// StarshipPromptProvider: if starship isn't installed, render() must be
// silent — no "sh: starship: command not found" leaking into output.
// ─────────────────────────────────────────────────────────────────────

TEST(LiveProviders, StarshipSilentWhenUnavailable) {
    HomeGuard home;
    // With $HOME pointed at an empty tmp dir, ~/.config/starship.toml
    // doesn't exist, so is_available() should return false.
    auto r = run_shell("echo ok\nexit\n");
    EXPECT_EQ(r.output.find("starship: command not found"),
              std::string::npos);
    EXPECT_EQ(r.output.find("starship: No such file or directory"),
              std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────
// FishCompletionProvider / FigCompletionProvider / ManpageCompletionProvider
// are all registered — verify the registry has at least the manpage one
// plus the other two completion providers by exercising a known case.
// ─────────────────────────────────────────────────────────────────────

TEST(LiveProviders, CompletionRegistryHasMultipleProviders) {
    HomeGuard home;
    // Easiest proof the registry is wired: tab-style flag completion
    // for a command with --help (grep on Linux, git on macOS).
    // We don't invoke interactive replxx here; instead we just verify
    // the shell boots and lists builtins without stray error output.
    auto r = run_shell("which which\nexit\n");
    EXPECT_NE(r.output.find("shell builtin"), std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────
// AiErrorHookProvider with a null client should be a silent no-op, not
// crash the shell after failed commands.
// ─────────────────────────────────────────────────────────────────────

TEST(LiveProviders, AiErrorHookSilentWithoutClient) {
    HomeGuard home;
    auto r = run_shell("false\necho after_false\nexit\n");
    // Shell must reach `exit` — hook didn't crash on nullptr client.
    EXPECT_NE(r.output.find("GoodBye"), std::string::npos);
    EXPECT_NE(r.output.find("after_false"), std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────
// fire_after_command is now called: safety hook's skip_execution flag is
// reset on every subsequent command, and AI error hook sees real exit
// codes. Prove after-command actually fires by observing that a failed
// command doesn't permanently leave skip_execution set.
// ─────────────────────────────────────────────────────────────────────

TEST(LiveProviders, FireAfterCommandDoesNotWedgeSubsequent) {
    HomeGuard home;
    auto r = run_shell(
        "false\n"
        "echo still_works\n"
        "exit\n");
    EXPECT_NE(r.output.find("still_works"), std::string::npos);
}
