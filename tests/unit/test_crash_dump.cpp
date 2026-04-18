// Tests for tash::util::install_crash_handler.
//
// Validating an async-signal-safe handler is awkward: by design it ends
// with raise(sig), which terminates the process. We side-step that by
// fork()'ing, installing the handler in the child, raising SIGABRT in
// the child, and capturing its stderr through a pipe in the parent.
// Then we assert on:
//
//   1. WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT
//      — confirms SA_RESETHAND took effect and the default disposition
//        re-terminated the process (not a graceful exit from the
//        handler).
//   2. Stderr contains the banner, signal name, and footer.
//   3. With last_executed_cmd set to "echo foo", the dump prints
//      "last command: echo foo".
//
// The whole crash test lives behind TASH_CRASH_DUMP_TESTS so that
// sanitizer / abort-on-CI lanes that don't tolerate forked SIGABRTs
// can skip it cleanly. The default build path enables it.

#include <gtest/gtest.h>

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include "tash/shell.h"
#include "tash/util/crash_dump.h"

namespace {

// Fork, run `body` in the child, capture its stderr, and return
// {stderr_text, wait_status}. The child always _exit(0)s via the crash
// path; if the handler didn't re-raise, the child would return from
// body and we'd _exit(0) — we assert against that in the caller.
struct ChildResult {
    std::string stderr_text;
    int status;
};

ChildResult run_in_child(void (*body)()) {
    int pipefd[2];
    if (::pipe(pipefd) != 0) {
        ADD_FAILURE() << "pipe() failed: " << std::strerror(errno);
        return {"", -1};
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        ADD_FAILURE() << "fork() failed: " << std::strerror(errno);
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        return {"", -1};
    }

    if (pid == 0) {
        // Child: redirect stderr into the pipe, close the read end,
        // then run body. If body returns (meaning the handler didn't
        // re-raise), exit 0 so the parent can detect the miss.
        ::close(pipefd[0]);
        ::dup2(pipefd[1], STDERR_FILENO);
        ::close(pipefd[1]);
        body();
        _exit(0);
    }

    // Parent.
    ::close(pipefd[1]);
    std::string out;
    char buf[1024];
    while (true) {
        ssize_t n = ::read(pipefd[0], buf, sizeof(buf));
        if (n > 0) out.append(buf, static_cast<size_t>(n));
        else if (n == 0) break;
        else if (errno == EINTR) continue;
        else break;
    }
    ::close(pipefd[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);
    return {out, status};
}

// Body for the basic-path test: fresh ShellState, SIGABRT.
void child_basic() {
    ShellState state;
    state.core.last_exit_status = 42;
    tash::util::install_crash_handler(state);
    ::raise(SIGABRT);
    // Unreached if the handler re-raised correctly.
}

// Body for the last-command test.
void child_with_last_cmd() {
    ShellState state;
    state.ai.last_executed_cmd = "echo foo";
    state.core.last_exit_status = 7;
    tash::util::install_crash_handler(state);
    ::raise(SIGABRT);
}

} // namespace

#ifdef TASH_CRASH_DUMP_TESTS

TEST(CrashDump, BasicHandlerFiresAndReraises) {
    ChildResult r = run_in_child(&child_basic);

    // Handler re-raised: child must have died to a signal, not exited.
    ASSERT_TRUE(WIFSIGNALED(r.status))
        << "child exited cleanly — handler didn't re-raise. stderr:\n"
        << r.stderr_text;
    EXPECT_EQ(WTERMSIG(r.status), SIGABRT)
        << "wrong terminating signal; stderr:\n" << r.stderr_text;

    // Banner and footer must be present.
    EXPECT_NE(r.stderr_text.find("=== tash crash"), std::string::npos)
        << r.stderr_text;
    EXPECT_NE(r.stderr_text.find("SIGABRT"), std::string::npos)
        << r.stderr_text;
    EXPECT_NE(r.stderr_text.find("report at"), std::string::npos)
        << r.stderr_text;

    // cwd line should be present (getcwd shouldn't fail in a test env).
    EXPECT_NE(r.stderr_text.find("cwd: "), std::string::npos)
        << r.stderr_text;

    // Exit status we seeded should round-trip.
    EXPECT_NE(r.stderr_text.find("last exit status: 42"), std::string::npos)
        << r.stderr_text;
}

TEST(CrashDump, PrintsLastExecutedCommand) {
    ChildResult r = run_in_child(&child_with_last_cmd);

    ASSERT_TRUE(WIFSIGNALED(r.status))
        << "child exited cleanly — handler didn't re-raise. stderr:\n"
        << r.stderr_text;
    EXPECT_EQ(WTERMSIG(r.status), SIGABRT);

    EXPECT_NE(r.stderr_text.find("last command: echo foo"), std::string::npos)
        << r.stderr_text;
    EXPECT_NE(r.stderr_text.find("last exit status: 7"), std::string::npos)
        << r.stderr_text;
}

#else // TASH_CRASH_DUMP_TESTS

TEST(CrashDump, DisabledOnThisBuild) {
    GTEST_SKIP()
        << "crash-dump tests are gated behind TASH_CRASH_DUMP_TESTS "
           "(fork + SIGABRT doesn't play well with some CI lanes)";
}

#endif // TASH_CRASH_DUMP_TESTS
