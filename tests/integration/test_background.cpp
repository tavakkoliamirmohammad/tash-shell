#include "test_helpers.h"

TEST(Background, BgStartsProcess) {
    auto r = run_shell("bg sleep 1\nbglist\nexit\n");
    EXPECT_NE(r.output.find("Executing"), std::string::npos);
    EXPECT_NE(r.output.find("sleep"), std::string::npos);
    EXPECT_NE(r.output.find("Total Background Jobs: 1"), std::string::npos);
}

TEST(Background, BgkillNoArgs) {
    auto r = run_shell("bgkill\nexit\n");
    EXPECT_NE(r.output.find("missing process number"), std::string::npos);
}

TEST(Background, BgstopNoArgs) {
    auto r = run_shell("bgstop\nexit\n");
    EXPECT_NE(r.output.find("missing process number"), std::string::npos);
}

TEST(Background, BgstartNoArgs) {
    auto r = run_shell("bgstart\nexit\n");
    EXPECT_NE(r.output.find("missing process number"), std::string::npos);
}

TEST(Background, BgkillNonNumeric) {
    auto r = run_shell("bgkill abc\nexit\n");
    EXPECT_NE(r.output.find("invalid process number"), std::string::npos);
}

TEST(Background, FgNoJobs) {
    auto r = run_shell("fg\nexit\n");
    EXPECT_NE(r.output.find("fg: no background jobs"), std::string::npos);
}

TEST(Background, FgWithNumberNoJobs) {
    auto r = run_shell("fg 1\nexit\n");
    EXPECT_NE(r.output.find("fg: no background jobs"), std::string::npos);
}

TEST(Background, FgInvalidJobNumber) {
    auto r = run_shell("bg sleep 8\nfg abc\nexit\n");
    EXPECT_NE(r.output.find("fg: invalid job number"), std::string::npos);
}
