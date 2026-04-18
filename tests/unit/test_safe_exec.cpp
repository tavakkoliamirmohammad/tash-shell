// Tests for tash::util::safe_exec.
//
// The critical property is that argv[1..] is delivered to the child
// process verbatim -- no shell substitution. Feeding "$(whoami)" as
// an argument to echo must produce the literal string, not the current
// user name.

#include <gtest/gtest.h>
#include "tash/util/safe_exec.h"

using tash::util::safe_exec;

TEST(SafeExecTest, ShellMetacharsAreLiteral) {
    auto r = safe_exec({"echo", "$(whoami)"});
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_text, "$(whoami)\n");
}

TEST(SafeExecTest, SemicolonDoesNotSplit) {
    auto r = safe_exec({"echo", "a; rm -rf /"});
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_text, "a; rm -rf /\n");
}

TEST(SafeExecTest, BackticksAreLiteral) {
    auto r = safe_exec({"echo", "`whoami`"});
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_text, "`whoami`\n");
}

TEST(SafeExecTest, ExitCodePropagates) {
    auto r = safe_exec({"false"});
    EXPECT_EQ(r.exit_code, 1);
    EXPECT_EQ(r.stdout_text, "");
}

TEST(SafeExecTest, CapturesStdout) {
    auto r = safe_exec({"printf", "hello"});
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_text, "hello");
}

TEST(SafeExecTest, EmptyArgvFailsCleanly) {
    auto r = safe_exec({});
    EXPECT_EQ(r.exit_code, -1);
    EXPECT_EQ(r.stdout_text, "");
}

TEST(SafeExecTest, UnknownCommandReturns127) {
    auto r = safe_exec({"tash-this-does-not-exist-xyzzy"});
    EXPECT_EQ(r.exit_code, 127);
}

TEST(SafeExecTest, TimeoutKillsLongSleep) {
    // sleep 5s with a 100ms budget: the helper must SIGKILL and
    // surface exit_code == -1 instead of blocking forever.
    auto r = safe_exec({"sleep", "5"}, 100);
    EXPECT_EQ(r.exit_code, -1);
}

TEST(SafeExecTest, NoTimeoutAllowsQuickExit) {
    auto r = safe_exec({"true"}, -1);
    EXPECT_EQ(r.exit_code, 0);
}
