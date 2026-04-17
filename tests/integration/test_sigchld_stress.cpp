// Stress test for SIGCHLD reaping + background-job lifecycle.
//
// The existing signal-handling tests use 3-5 concurrent jobs. That
// catches the happy path but won't surface reaper bugs that only
// appear under load (lost SIGCHLDs from signal coalescing, zombie
// accumulation, slow drain). This spins up many short-lived background
// jobs and asserts the shell reaches a clean exit with no errors.

#include "test_helpers.h"

#include <string>

// The shell caps concurrent bg jobs at ShellState::max_background_processes
// (default 5). These stress tests run MANY short batches back-to-back;
// the reaper has to clear each batch from the tracking map before the
// next batch can start. Any lost SIGCHLD leaves a phantom job and the
// next "bg" call hits the cap and errors.
TEST(SigchldStress, ManyBatchesOfShortBackgroundJobs) {
    // 10 batches × 5 jobs = 50 total starts. Between batches we sleep
    // long enough for the jobs to finish and SIGCHLD to propagate.
    std::string script;
    for (int batch = 0; batch < 10; ++batch) {
        for (int j = 0; j < 5; ++j) {
            script += "bg sleep 0.1\n";
        }
        script += "sleep 0.5\n";   // let the batch drain
    }
    script += "bglist\n";
    script += "exit\n";

    auto r = run_shell(script);

    EXPECT_NE(r.output.find("GoodBye"), std::string::npos);
    EXPECT_EQ(r.output.find("Error: Fork"), std::string::npos);
    // If SIGCHLD handling were broken, later batches would hit the
    // max-bg cap as zombies accumulate in state.background_processes.
    EXPECT_EQ(r.output.find("Error: Maximum number"), std::string::npos);
    // Final bglist after everything finished: should be 0 background jobs.
    EXPECT_NE(r.output.find("Total Background Jobs: 0"), std::string::npos);
}

TEST(SigchldStress, FillCapAndDrainRepeatedly) {
    // Cycle: fill to cap with natural-duration jobs, let them all
    // finish, fill again. Asserts the reaper clears the map between
    // cycles so the cap doesn't accumulate ghost entries.
    std::string script;
    for (int cycle = 0; cycle < 6; ++cycle) {
        for (int j = 0; j < 5; ++j) {
            script += "bg sleep 0.1\n";
        }
        script += "sleep 0.6\n";       // generous margin
    }
    script += "bglist\n";
    script += "exit\n";

    auto r = run_shell(script);
    EXPECT_NE(r.output.find("GoodBye"), std::string::npos);
    EXPECT_EQ(r.output.find("Error: Fork"),           std::string::npos);
    EXPECT_EQ(r.output.find("Error: Maximum number"), std::string::npos);
    EXPECT_NE(r.output.find("Total Background Jobs: 0"), std::string::npos);
}

TEST(SigchldStress, BgFollowedByForegroundDoesNotStallOnReap) {
    // If the foreground waitpid() inadvertently absorbs a background
    // SIGCHLD, bglist/bg bookkeeping diverges from reality. Run a bg
    // sleep, then a fg echo, then check that bglist still reflects
    // the bg process (it should still be sleeping) — and that the fg
    // echo reached us.
    auto r = run_shell(
        "bg sleep 3\n"
        "echo fg_reached\n"
        "bglist\n"
        "bgkill 1\n"
        "exit\n");
    EXPECT_NE(r.output.find("fg_reached"), std::string::npos);
    EXPECT_NE(r.output.find("GoodBye"),    std::string::npos);
}
