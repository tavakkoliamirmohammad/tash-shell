// Unit tests for the `help` builtin and the builtin metadata registry.
//
// builtin_help() writes to STDOUT_FILENO / STDERR_FILENO directly via
// the write_stdout / write_stderr inline helpers, so we dup2 a temp
// file over those descriptors to capture the exact bytes a user sees.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

#include "tash/builtins.h"
#include "tash/core/builtins.h"
#include "tash/shell.h"

namespace {

// RAII fd capture: redirects `which_fd` (STDOUT_FILENO or STDERR_FILENO)
// to a tmpfile for the scope, then lets you read back what was written.
class FdCapture {
public:
    explicit FdCapture(int which_fd) : which_(which_fd) {
        char tmpl[] = "/tmp/tash_help_capture_XXXXXX";
        fd_ = ::mkstemp(tmpl);
        if (fd_ < 0) std::abort();
        path_ = tmpl;
        saved_ = ::dup(which_);
        ::dup2(fd_, which_);
    }
    ~FdCapture() {
        if (saved_ >= 0) { ::dup2(saved_, which_); ::close(saved_); }
        if (fd_ >= 0) ::close(fd_);
        ::unlink(path_.c_str());
    }
    std::string read_all() {
        ::fsync(fd_);
        ::lseek(fd_, 0, SEEK_SET);
        std::string out;
        char buf[4096];
        ssize_t n;
        while ((n = ::read(fd_, buf, sizeof(buf))) > 0) {
            out.append(buf, static_cast<size_t>(n));
        }
        return out;
    }
private:
    int which_ = -1;
    int fd_ = -1;
    int saved_ = -1;
    std::string path_;
};

} // namespace

// ── get_builtins_info / get_builtins consistency ─────────────────

TEST(BuiltinRegistry, InfoTableIsNonEmpty) {
    EXPECT_FALSE(get_builtins_info().empty());
}

TEST(BuiltinRegistry, MapDerivedFromInfoTable) {
    // Every entry in the info table must be reachable via get_builtins().
    const auto &info = get_builtins_info();
    const auto &map  = get_builtins();
    EXPECT_EQ(map.size(), info.size());
    for (const auto &b : info) {
        auto it = map.find(b.name);
        ASSERT_NE(it, map.end()) << "missing from map: " << b.name;
    }
}

TEST(BuiltinRegistry, ExpectedCoreBuiltinsRegistered) {
    // A minimal sanity check: canonical POSIX names are present.
    for (const char *name : {"cd", "pwd", "exit", "export", "source",
                              "alias", "which", "help", "trap"}) {
        EXPECT_TRUE(is_builtin(name)) << name;
    }
}

TEST(BuiltinRegistry, AllBriefsAreShortAndNonEmpty) {
    for (const auto &b : get_builtins_info()) {
        ASSERT_NE(b.brief, nullptr) << b.name;
        std::string s = b.brief;
        EXPECT_FALSE(s.empty()) << b.name;
        EXPECT_LE(s.size(), 70u)
            << b.name << ": brief is " << s.size() << " chars";
    }
}

// ── help builtin behavior ────────────────────────────────────────

TEST(BuiltinHelp, ListsAllBuiltinsOnStdout) {
    ShellState state;
    std::string out;
    {
        FdCapture cap(STDOUT_FILENO);
        int rc = builtin_help({"help"}, state);
        EXPECT_EQ(rc, 0);
        out = cap.read_all();
    }
    // Every registered builtin name should appear followed by its brief.
    for (const auto &b : get_builtins_info()) {
        EXPECT_NE(out.find(std::string(b.name) + "  "),
                  std::string::npos)
            << "missing name line: " << b.name;
        EXPECT_NE(out.find(b.brief), std::string::npos)
            << "missing brief for: " << b.name;
    }
    // And `help` lists itself, so users can rediscover the command.
    EXPECT_NE(out.find("help"), std::string::npos);
    EXPECT_NE(out.find("exit"), std::string::npos);
}

TEST(BuiltinHelp, ShowsUsageForNamedBuiltin) {
    ShellState state;
    std::string out;
    {
        FdCapture cap(STDOUT_FILENO);
        int rc = builtin_help({"help", "exit"}, state);
        EXPECT_EQ(rc, 0);
        out = cap.read_all();
    }
    EXPECT_NE(out.find("usage: exit"), std::string::npos);
    EXPECT_NE(out.find("Exit the shell"), std::string::npos);
}

TEST(BuiltinHelp, NonexistentBuiltinReturnsErrorOnStderr) {
    ShellState state;
    std::string err;
    int rc;
    {
        FdCapture cap(STDERR_FILENO);
        rc = builtin_help({"help", "totally_not_a_builtin"}, state);
        err = cap.read_all();
    }
    EXPECT_NE(rc, 0);
    EXPECT_NE(err.find("no such builtin"), std::string::npos);
    EXPECT_NE(err.find("totally_not_a_builtin"), std::string::npos);
}
