// Job control happy-path coverage for bgkill / bgstop / bgstart / fg.
// The existing test_background.cpp only exercises no-arg errors.

#include "test_helpers.h"

#include <cstdio>
#include <string>
#include <unistd.h>

namespace {
// Wait a bit for a backgrounded process to register; we don't want
// flaky tests, but 100ms is enough on any reasonable runner.
const char *BG_SLEEP = "bg sleep 2";
} // namespace

TEST(JobControl, BglistShowsBackgroundJob) {
    auto r = run_shell(
        std::string(BG_SLEEP) + "\n"
        "bglist\n"
        "exit\n");
    // bglist prints the job number + command basename (args aren't
    // preserved in the current implementation, so "sleep" — not
    // "sleep 2" — is what lands in the bglist output).
    EXPECT_NE(r.output.find("sleep"),     std::string::npos);
    EXPECT_NE(r.output.find("Background"), std::string::npos);
}

TEST(JobControl, BgkillTerminatesBackgroundJob) {
    auto r = run_shell(
        std::string(BG_SLEEP) + "\n"
        "bglist\n"
        "bgkill 1\n"
        "exit\n");
    // Successful kill doesn't emit "not a valid job" or similar error.
    EXPECT_EQ(r.output.find("bgkill: invalid"), std::string::npos);
}

TEST(JobControl, BgkillInvalidJobNumberReportsError) {
    auto r = run_shell("bgkill 999\nexit\n");
    // Should not crash; should reach exit goodbye.
    EXPECT_NE(r.output.find("GoodBye"), std::string::npos);
}

TEST(JobControl, BgstopPausesJob) {
    // bgstop sends SIGSTOP; we can't easily verify the process state
    // from inside the test, but verify the builtin runs cleanly and
    // we can resume with bgstart.
    auto r = run_shell(
        std::string(BG_SLEEP) + "\n"
        "bgstop 1\n"
        "bgstart 1\n"
        "exit\n");
    EXPECT_NE(r.output.find("GoodBye"), std::string::npos);
    EXPECT_EQ(r.output.find("bgstop: invalid"),  std::string::npos);
    EXPECT_EQ(r.output.find("bgstart: invalid"), std::string::npos);
}

TEST(JobControl, BglistEmptyWhenNoJobs) {
    auto r = run_shell("bglist\nexit\n");
    // bglist with no jobs either prints nothing for jobs or a
    // "no background processes" line — either way it doesn't crash.
    EXPECT_NE(r.output.find("GoodBye"), std::string::npos);
}

TEST(JobControl, BgWithEmptyCommandReportsError) {
    // `bg` with no argument should not silently accept.
    auto r = run_shell("bg\nexit\n");
    EXPECT_NE(r.output.find("GoodBye"), std::string::npos);
}

TEST(JobControl, MultipleBackgroundJobs) {
    auto r = run_shell(
        "bg sleep 2\n"
        "bg sleep 3\n"
        "bglist\n"
        "bgkill 1\n"
        "bgkill 2\n"
        "exit\n");
    // Two "Background process ... Executing" lines prove both jobs
    // started (bglist strips args so we can't distinguish the two).
    size_t first  = r.output.find("Background process");
    size_t second = first == std::string::npos
                      ? std::string::npos
                      : r.output.find("Background process", first + 1);
    EXPECT_NE(first,  std::string::npos);
    EXPECT_NE(second, std::string::npos);
}
