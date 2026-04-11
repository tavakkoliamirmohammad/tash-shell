#include "test_helpers.h"

TEST(Cd, CdHome) {
    auto r = run_shell("cd ~\npwd\nexit\n");
    const char *home = getenv("HOME");
    ASSERT_NE(home, nullptr);
    EXPECT_NE(r.output.find(home), std::string::npos);
}

TEST(Cd, CdNoArgs) {
    auto r = run_shell("cd\npwd\nexit\n");
    const char *home = getenv("HOME");
    ASSERT_NE(home, nullptr);
    EXPECT_NE(r.output.find(home), std::string::npos);
}

TEST(Cd, CdTmp) {
    auto r = run_shell("cd /tmp\npwd\nexit\n");
    bool found = r.output.find("/tmp") != std::string::npos ||
                 r.output.find("/private/tmp") != std::string::npos;
    EXPECT_TRUE(found);
}

TEST(Cd, CdBadPath) {
    auto r = run_shell("cd /nonexistent_path_xyz\nexit\n");
    EXPECT_NE(r.output.find("No such file or directory"), std::string::npos);
}
