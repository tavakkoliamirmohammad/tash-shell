#include "test_helpers.h"

TEST(History, ShowsCommandHistory) {
    auto r = run_shell("echo aaa\necho bbb\nhistory\nexit\n");
    EXPECT_NE(r.output.find("echo aaa"), std::string::npos);
    EXPECT_NE(r.output.find("echo bbb"), std::string::npos);
    EXPECT_NE(r.output.find("history"), std::string::npos);
}

TEST(History, NumberedEntries) {
    auto r = run_shell("echo first\nhistory\nexit\n");
    EXPECT_NE(r.output.find("1"), std::string::npos);
    EXPECT_NE(r.output.find("echo first"), std::string::npos);
}

TEST(History, BangBangRepeatsLastCommand) {
    auto r = run_shell("echo hello\n!!\nexit\n");
    // "hello" should appear at least twice: once from original, once from !!
    int count = count_occurrences(r.output, "hello");
    EXPECT_GE(count, 2) << "!! should repeat the last command. Output: " << r.output;
}

TEST(History, BangNRunsNthCommand) {
    auto r = run_shell("echo aaa\necho bbb\n!1\nexit\n");
    // "aaa" should appear at least twice: once from original, once from !1
    int count = count_occurrences(r.output, "aaa");
    EXPECT_GE(count, 2) << "!1 should re-run the first command. Output: " << r.output;
}
