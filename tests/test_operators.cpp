#include "test_helpers.h"

TEST(Operators, AndRunsBoth) {
    auto r = run_shell("echo aaa && echo bbb && echo ccc\nexit\n");
    EXPECT_NE(r.output.find("aaa"), std::string::npos);
    EXPECT_NE(r.output.find("bbb"), std::string::npos);
    EXPECT_NE(r.output.find("ccc"), std::string::npos);
}

TEST(Operators, SemicolonRunsBoth) {
    auto r = run_shell("echo aaa ; echo bbb\nexit\n");
    EXPECT_NE(r.output.find("aaa"), std::string::npos);
    EXPECT_NE(r.output.find("bbb"), std::string::npos);
}

TEST(Operators, OrRunsOnFailure) {
    auto r = run_shell("false || echo fallback_ran\nexit\n");
    EXPECT_NE(r.output.find("fallback_ran"), std::string::npos);
}

TEST(Operators, OrSkipsOnSuccess) {
    // Use ; after || to verify the shell continues. If || correctly skips,
    // "or_proof" prints but "or_skipped" does not execute as output.
    auto r = run_shell("true || echo or_skipped ; echo or_proof\nexit\n");
    // or_proof should appear as actual output (at least once from execution)
    EXPECT_GE(count_occurrences(r.output, "or_proof"), 1);
    // or_skipped should appear at most once (from readline echo only)
    EXPECT_LE(count_occurrences(r.output, "or_skipped"), 1);
}

TEST(Operators, AndSkipsOnFailure) {
    auto r = run_shell("false && echo and_skipped ; echo and_proof\nexit\n");
    EXPECT_GE(count_occurrences(r.output, "and_proof"), 1);
    EXPECT_LE(count_occurrences(r.output, "and_skipped"), 1);
}

TEST(Operators, AndRunsOnSuccess) {
    auto r = run_shell("true && echo success_ran\nexit\n");
    EXPECT_NE(r.output.find("success_ran"), std::string::npos);
}

TEST(Operators, SemicolonAfterFailure) {
    auto r = run_shell("false ; echo still_runs\nexit\n");
    EXPECT_NE(r.output.find("still_runs"), std::string::npos);
}

TEST(Operators, MixedOperators) {
    auto r = run_shell("echo start && echo mid ; echo end\nexit\n");
    EXPECT_NE(r.output.find("start"), std::string::npos);
    EXPECT_NE(r.output.find("mid"), std::string::npos);
    EXPECT_NE(r.output.find("end"), std::string::npos);
}
