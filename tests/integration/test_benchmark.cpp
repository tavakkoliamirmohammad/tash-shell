#include "test_helpers.h"

// `tash --benchmark` should print a stage-by-stage startup breakdown and
// exit 0. The breakdown format is documented in the PR description as
// "Startup breakdown:" followed by indented stage lines and a Total:.

TEST(Benchmark, PrintsStartupBreakdown) {
    std::string cmd = shell_binary + " --benchmark 2>&1";
    FILE *pipe = popen(cmd.c_str(), "r");
    ASSERT_NE(pipe, nullptr);
    std::string output;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) output += buf;
    int status = pclose(pipe);

    EXPECT_EQ(WEXITSTATUS(status), 0);
    EXPECT_NE(output.find("Startup breakdown:"), std::string::npos);
    EXPECT_NE(output.find("Total:"), std::string::npos);
    EXPECT_NE(output.find("ms"), std::string::npos);
}
