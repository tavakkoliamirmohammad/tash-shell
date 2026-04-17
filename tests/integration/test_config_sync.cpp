#include "test_helpers.h"
#include <cstdlib>
#include <dirent.h>
#include <filesystem>
#include <sys/stat.h>
#include <unistd.h>

// config_sync initialises a git repo at ~/.tash. We redirect $HOME per
// test so the suite never touches the developer's real one.
namespace {
struct HomeGuard {
    std::string original;
    std::string tmp;
    HomeGuard() {
        const char *h = getenv("HOME");
        original = h ? h : "";
        tmp = "/tmp/tash_cfg_sync_test_" + std::to_string(getpid());
        std::error_code ec;
        std::filesystem::create_directories(tmp, ec);
        setenv("HOME", tmp.c_str(), 1);
    }
    ~HomeGuard() {
        std::error_code ec;
        std::filesystem::remove_all(tmp, ec);
        if (!original.empty()) setenv("HOME", original.c_str(), 1);
        else unsetenv("HOME");
    }
};
} // namespace

TEST(ConfigSync, MissingSubcommandShowsUsage) {
    HomeGuard home;
    auto r = run_shell("config\nexit\n");
    EXPECT_NE(r.output.find("usage"), std::string::npos);
    EXPECT_NE(r.output.find("config sync init"),   std::string::npos);
    EXPECT_NE(r.output.find("config sync remote"), std::string::npos);
    EXPECT_NE(r.output.find("config sync push"),   std::string::npos);
    EXPECT_NE(r.output.find("config sync pull"),   std::string::npos);
}

TEST(ConfigSync, StatusBeforeInit) {
    HomeGuard home;
    auto r = run_shell("config sync status\nexit\n");
    EXPECT_NE(r.output.find("not initialized"), std::string::npos);
}

TEST(ConfigSync, InitThenStatusReports) {
    HomeGuard home;
    auto r = run_shell(
        "config sync init\n"
        "config sync status\n"
        "exit\n");
    EXPECT_NE(r.output.find("config: initialized"), std::string::npos);
    // After init, status should no longer say "not initialized".
    size_t first = r.output.find("config: initialized");
    ASSERT_NE(first, std::string::npos);
    // Find status line AFTER the init line.
    size_t second = r.output.find("config: initialized at", first + 1);
    EXPECT_NE(second, std::string::npos);
}

TEST(ConfigSync, UnknownSyncActionErrors) {
    HomeGuard home;
    auto r = run_shell("config sync nonsense\nexit\n");
    EXPECT_NE(r.output.find("unknown sync action"), std::string::npos);
}

TEST(ConfigSync, RemoteRequiresUrl) {
    HomeGuard home;
    auto r = run_shell(
        "config sync init\n"
        "config sync remote\n"
        "exit\n");
    EXPECT_NE(r.output.find("remote requires a URL"), std::string::npos);
}

TEST(ConfigSync, InitCreatesGitRepo) {
    HomeGuard home;
    run_shell("config sync init\nexit\n");
    // .git directory should now exist under $HOME/.tash
    std::string git_dir = home.tmp + "/.tash/.git";
    struct stat st;
    EXPECT_EQ(stat(git_dir.c_str(), &st), 0);
    EXPECT_TRUE(S_ISDIR(st.st_mode));
}
