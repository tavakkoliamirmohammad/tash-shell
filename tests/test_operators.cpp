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
    // Use file side-effect: if || correctly skips, the file won't be created
    std::string marker = "/tmp/amish_or_skip_" + std::to_string(getpid());
    unlink(marker.c_str());
    run_shell("true || echo proof > " + marker + "\nexit\n");
    struct stat st;
    EXPECT_NE(stat(marker.c_str(), &st), 0) << "File should not exist if || correctly skipped";
    unlink(marker.c_str());
}

TEST(Operators, AndSkipsOnFailure) {
    // Use file side-effect: if && correctly skips, the file won't be created
    std::string marker = "/tmp/amish_and_skip_" + std::to_string(getpid());
    unlink(marker.c_str());
    run_shell("false && echo proof > " + marker + "\nexit\n");
    struct stat st;
    EXPECT_NE(stat(marker.c_str(), &st), 0) << "File should not exist if && correctly skipped";
    unlink(marker.c_str());
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
