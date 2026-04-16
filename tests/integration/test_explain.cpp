#include "test_helpers.h"

TEST(Explain, ShowsCommandHintAndFlags) {
    auto r = run_shell("explain tar -xzf archive.tar.gz\nexit\n");
    EXPECT_NE(r.output.find("Create or extract compressed archives"),
              std::string::npos);
    EXPECT_NE(r.output.find("Extract files from archive"),
              std::string::npos);
    EXPECT_NE(r.output.find("Filter through gzip"), std::string::npos);
}

TEST(Explain, UnknownCommandReportsError) {
    auto r = run_shell("explain totally_fake_command\nexit\n");
    EXPECT_NE(r.output.find("no entry for"), std::string::npos);
}

TEST(Explain, MissingArgumentPrintsUsage) {
    auto r = run_shell("explain\nexit\n");
    EXPECT_NE(r.output.find("usage"), std::string::npos);
}
