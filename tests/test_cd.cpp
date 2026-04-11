#include "test_helpers.h"
#include <vector>

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

TEST(Cd, CdDashReturnsToPreviousDirectory) {
    auto r = run_shell("pwd\ncd /tmp\ncd -\npwd\nexit\n");
    // The first pwd captures the starting directory.
    // After cd /tmp && cd -, we should be back in the starting directory.
    // Extract lines from output to verify.
    // On macOS /tmp may resolve to /private/tmp, so we just check that
    // the final pwd output matches the initial pwd output (both present).
    // Split output into lines and compare first and last pwd results.
    std::istringstream stream(r.output);
    std::string line;
    std::vector<std::string> pwd_lines;
    while (std::getline(stream, line)) {
        // Skip prompt lines and empty lines
        if (!line.empty() && line[0] == '/') {
            pwd_lines.push_back(line);
        }
    }
    // We expect at least 3 directory lines: initial pwd, cd - printing dir, final pwd
    ASSERT_GE(pwd_lines.size(), 3u);
    // The cd - output and the final pwd should match the initial directory
    EXPECT_EQ(pwd_lines.front(), pwd_lines.back());
}

TEST(Cd, CdDashNoOldpwd) {
    // On a fresh shell with no previous cd, cd - should print an error
    auto r = run_shell("cd -\nexit\n");
    EXPECT_NE(r.output.find("OLDPWD not set"), std::string::npos);
}

TEST(Cd, PushdPopd) {
    auto r = run_shell("pushd /tmp\npwd\npopd\npwd\nexit\n");
    // After pushd /tmp, pwd should show /tmp (or /private/tmp on macOS)
    bool found_tmp = r.output.find("/tmp") != std::string::npos ||
                     r.output.find("/private/tmp") != std::string::npos;
    EXPECT_TRUE(found_tmp);
    // After popd we should be back in original directory
    // popd prints the directory it returns to, so it should appear in output
    EXPECT_EQ(r.exit_code, 0);
}

TEST(Cd, DirsPrintsCurrent) {
    auto r = run_shell("dirs\nexit\n");
    // dirs should print at least the current working directory (a slash-prefixed path)
    EXPECT_NE(r.output.find("/"), std::string::npos);
}

TEST(Cd, PopdEmptyStack) {
    auto r = run_shell("popd\nexit\n");
    EXPECT_NE(r.output.find("directory stack empty"), std::string::npos);
}
