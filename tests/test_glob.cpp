#include "test_helpers.h"

TEST(Glob, ExpandStar) {
    std::string dir = "/tmp/amish_glob_test_" + std::to_string(getpid());
    system(("mkdir -p " + dir).c_str());
    system(("touch " + dir + "/aaa.txt " + dir + "/bbb.txt " + dir + "/ccc.log").c_str());

    auto r = run_shell("echo " + dir + "/*.txt\nexit\n");
    EXPECT_NE(r.output.find("aaa.txt"), std::string::npos);
    EXPECT_NE(r.output.find("bbb.txt"), std::string::npos);
    EXPECT_EQ(r.output.find("ccc.log"), std::string::npos);

    system(("rm -rf " + dir).c_str());
}

TEST(Glob, UnmatchedPatternPreserved) {
    auto r = run_shell("echo *.nonexistent_ext_xyz\nexit\n");
    EXPECT_NE(r.output.find("*.nonexistent_ext_xyz"), std::string::npos);
}
