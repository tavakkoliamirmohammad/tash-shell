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
