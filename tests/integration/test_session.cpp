#include "test_helpers.h"
#include <cstdlib>
#include <dirent.h>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

// Test sessions are stored under ~/.tash/sessions. To avoid polluting
// a developer's real home, redirect $HOME to a per-test temp dir.
namespace {
struct HomeGuard {
    std::string original;
    std::string tmp;
    HomeGuard() {
        const char *h = getenv("HOME");
        original = h ? h : "";
        tmp = "/tmp/tash_session_test_home_" + std::to_string(getpid());
        std::error_code ec;
        std::filesystem::create_directories(tmp, ec);
        setenv("HOME", tmp.c_str(), 1);
    }
    ~HomeGuard() {
        std::error_code ec;
        std::filesystem::remove_all(tmp, ec);
        if (!original.empty()) setenv("HOME", original.c_str(), 1);
        else unsetenv("HOME");
    }
};
} // namespace

TEST(SessionBuiltin, SaveListRmRoundTrip) {
    HomeGuard home;
    auto r = run_shell(
        "alias probe='echo hi'\n"
        "session save mywork\n"
        "session list\n"
        "session rm mywork\n"
        "session list\n"
        "exit\n");
    EXPECT_NE(r.output.find("session: saved 'mywork'"), std::string::npos);
    EXPECT_NE(r.output.find("mywork"), std::string::npos);
    EXPECT_NE(r.output.find("session: removed 'mywork'"), std::string::npos);
    EXPECT_NE(r.output.find("(no saved sessions)"), std::string::npos);
}

TEST(SessionBuiltin, LoadRestoresAliases) {
    HomeGuard home;
    // Save with an alias, then start a fresh shell and load — the alias
    // should come back.
    run_shell("alias sessprobe='echo restored'\nsession save restoredset\nexit\n");
    auto r = run_shell(
        "session load restoredset\n"
        "alias\n"
        "exit\n");
    EXPECT_NE(r.output.find("session: loaded 'restoredset'"),
              std::string::npos);
    EXPECT_NE(r.output.find("sessprobe"), std::string::npos);
}

TEST(SessionBuiltin, LoadUnknownSessionErrors) {
    HomeGuard home;
    auto r = run_shell("session load nonexistent_xyz\nexit\n");
    EXPECT_NE(r.output.find("no such session"), std::string::npos);
}

TEST(SessionBuiltin, MissingSubcommandShowsUsage) {
    HomeGuard home;
    auto r = run_shell("session\nexit\n");
    EXPECT_NE(r.output.find("usage"), std::string::npos);
    EXPECT_NE(r.output.find("session list"), std::string::npos);
    EXPECT_NE(r.output.find("session save"), std::string::npos);
    EXPECT_NE(r.output.find("session load"), std::string::npos);
    EXPECT_NE(r.output.find("session rm"), std::string::npos);
}

TEST(SessionBuiltin, SaveMissingNameErrors) {
    HomeGuard home;
    auto r = run_shell("session save\nexit\n");
    EXPECT_NE(r.output.find("save requires a name"), std::string::npos);
}

TEST(SessionBuiltin, ListWhenEmpty) {
    HomeGuard home;
    auto r = run_shell("session list\nexit\n");
    EXPECT_NE(r.output.find("(no saved sessions)"), std::string::npos);
}
