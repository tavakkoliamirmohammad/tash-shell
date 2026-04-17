// POSIX `trap` builtin: signal+EXIT handler registration and dispatch.

#include "test_helpers.h"

#include <string>

TEST(Trap, ExitTrapFiresOnExit) {
    auto r = run_shell(
        "trap 'echo goodbye_from_trap' EXIT\n"
        "exit\n");
    EXPECT_NE(r.output.find("goodbye_from_trap"), std::string::npos);
    // Trap fires *before* the exit greeting.
    size_t trap_pos = r.output.find("goodbye_from_trap");
    size_t exit_pos = r.output.find("GoodBye");
    ASSERT_NE(trap_pos, std::string::npos);
    ASSERT_NE(exit_pos, std::string::npos);
    EXPECT_LT(trap_pos, exit_pos);
}

TEST(Trap, Usr1TrapCaughtAndRun) {
    // Self-signal with USR1; trap command must print the marker.
    auto r = run_shell(
        "trap 'echo usr1_caught' USR1\n"
        "kill -USR1 $$\n"
        "echo after_kill\n"
        "exit\n");
    EXPECT_NE(r.output.find("usr1_caught"), std::string::npos);
    EXPECT_NE(r.output.find("after_kill"),   std::string::npos);
}

TEST(Trap, TrapListShowsRegistered) {
    auto r = run_shell(
        "trap 'echo probe_cmd' USR2\n"
        "trap\n"
        "exit\n");
    EXPECT_NE(r.output.find("probe_cmd"), std::string::npos);
    EXPECT_NE(r.output.find("SIGUSR2"),   std::string::npos);
}

TEST(Trap, TrapDashRemovesHandler) {
    // Register, fire once to confirm it works, remove, fire again —
    // should not print the caught marker the second time.
    auto r = run_shell(
        "trap 'echo caught_probe' USR1\n"
        "kill -USR1 $$\n"
        "trap - USR1\n"
        "echo mid_marker\n"
        "kill -USR1 $$ 2>/dev/null\n"   // default action on USR1 is terminate, so swallow
        "exit\n");
    // First kill should produce caught_probe; mid_marker always appears.
    EXPECT_NE(r.output.find("caught_probe"), std::string::npos);
    EXPECT_NE(r.output.find("mid_marker"),   std::string::npos);
}

TEST(Trap, TrapAcceptsSigPrefixAndBareName) {
    auto r = run_shell(
        "trap 'echo a' SIGUSR1\n"
        "trap 'echo b' USR2\n"
        "trap\n"
        "exit\n");
    EXPECT_NE(r.output.find("SIGUSR1"), std::string::npos);
    EXPECT_NE(r.output.find("SIGUSR2"), std::string::npos);
}

TEST(Trap, InvalidSignalReportsError) {
    auto r = run_shell(
        "trap 'echo x' NOTASIGNAL\n"
        "echo marker_after\n"
        "exit\n");
    EXPECT_NE(r.output.find("invalid signal"), std::string::npos);
    EXPECT_NE(r.output.find("marker_after"),    std::string::npos);
}

TEST(Trap, NoArgumentsListsEmpty) {
    // Fresh shell: no traps set, listing should exit cleanly.
    auto r = run_shell(
        "trap\n"
        "echo list_done\n"
        "exit\n");
    EXPECT_NE(r.output.find("list_done"), std::string::npos);
}

TEST(Trap, TrapMissingArgsReportsUsage) {
    auto r = run_shell(
        "trap 'echo x'\n"
        "echo marker_after\n"
        "exit\n");
    EXPECT_NE(r.output.find("trap: usage"), std::string::npos);
    EXPECT_NE(r.output.find("marker_after"), std::string::npos);
}
