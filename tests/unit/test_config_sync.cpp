#include <gtest/gtest.h>
#include "tash/core/config_sync.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

using namespace tash::config_sync;

// ── Test fixture: temp directory with automatic cleanup ──────

class ConfigSyncTest : public ::testing::Test {
protected:
    std::string temp_dir;

    void SetUp() override {
        // Create a unique temp directory
        char tmpl[] = "/tmp/tash_config_sync_test_XXXXXX";
        char *dir = mkdtemp(tmpl);
        ASSERT_NE(dir, nullptr) << "Failed to create temp directory";
        temp_dir = std::string(dir);
    }

    void TearDown() override {
        // Recursively remove the temp directory
        if (!temp_dir.empty()) {
            std::string cmd = "rm -rf \"" + temp_dir + "\"";
            (void)system(cmd.c_str());
        }
    }

    std::string read_file(const std::string &path) {
        std::ifstream ifs(path);
        if (!ifs.is_open()) return "";
        std::ostringstream oss;
        oss << ifs.rdbuf();
        return oss.str();
    }

    void write_file(const std::string &path, const std::string &content) {
        std::ofstream ofs(path);
        ofs << content;
    }

    bool dir_exists(const std::string &path) {
        struct stat st;
        return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }
};

// ── Tests ────────────────────────────────────────────────────

TEST_F(ConfigSyncTest, SyncInitCreatesGitRepo) {
    bool result = sync_init(temp_dir);
    ASSERT_TRUE(result);
    EXPECT_TRUE(dir_exists(temp_dir + "/.git"));
}

TEST_F(ConfigSyncTest, SyncInitCreatesGitignore) {
    bool result = sync_init(temp_dir);
    ASSERT_TRUE(result);

    std::string gitignore = read_file(temp_dir + "/.gitignore");
    EXPECT_FALSE(gitignore.empty());
    EXPECT_NE(gitignore.find("history.db"), std::string::npos);
    EXPECT_NE(gitignore.find("sessions/"), std::string::npos);
    EXPECT_NE(gitignore.find("trash/"), std::string::npos);
    EXPECT_NE(gitignore.find("cache/"), std::string::npos);
    EXPECT_NE(gitignore.find("*.sock"), std::string::npos);
    EXPECT_NE(gitignore.find("*.log"), std::string::npos);
}

TEST_F(ConfigSyncTest, SyncInitIdempotent) {
    bool first = sync_init(temp_dir);
    ASSERT_TRUE(first);

    bool second = sync_init(temp_dir);
    EXPECT_TRUE(second);

    // .git dir should still be there
    EXPECT_TRUE(dir_exists(temp_dir + "/.git"));
}

TEST_F(ConfigSyncTest, IsInitializedTrue) {
    sync_init(temp_dir);
    EXPECT_TRUE(sync_is_initialized(temp_dir));
}

TEST_F(ConfigSyncTest, IsInitializedFalse) {
    // Empty temp dir, no git repo
    EXPECT_FALSE(sync_is_initialized(temp_dir));
}

TEST_F(ConfigSyncTest, SetRemoteAddsOrigin) {
    sync_init(temp_dir);

    bool result = sync_set_remote(temp_dir, "https://github.com/user/tash-config.git");
    ASSERT_TRUE(result);

    CmdResult remote_out = run_git_command(temp_dir, "remote -v");
    EXPECT_EQ(remote_out.exit_code, 0);
    EXPECT_NE(remote_out.output.find("https://github.com/user/tash-config.git"),
              std::string::npos);
}

TEST_F(ConfigSyncTest, SetRemoteUpdatesExisting) {
    sync_init(temp_dir);

    sync_set_remote(temp_dir, "https://github.com/user/old-url.git");
    bool result = sync_set_remote(temp_dir, "https://github.com/user/new-url.git");
    ASSERT_TRUE(result);

    CmdResult remote_out = run_git_command(temp_dir, "remote -v");
    EXPECT_NE(remote_out.output.find("https://github.com/user/new-url.git"),
              std::string::npos);
    EXPECT_EQ(remote_out.output.find("https://github.com/user/old-url.git"),
              std::string::npos);
}

TEST_F(ConfigSyncTest, RunGitCommandSuccess) {
    sync_init(temp_dir);

    CmdResult result = run_git_command(temp_dir, "status");
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_FALSE(result.output.empty());
}

TEST_F(ConfigSyncTest, RunGitCommandFailure) {
    sync_init(temp_dir);

    CmdResult result = run_git_command(temp_dir, "checkout nonexistent-branch-xyz");
    EXPECT_NE(result.exit_code, 0);
}

TEST_F(ConfigSyncTest, SyncDiffShowsChanges) {
    sync_init(temp_dir);

    // Make an initial commit so HEAD exists
    run_git_command(temp_dir, "add -A");
    run_git_command(temp_dir, "commit -m \"initial\"");

    // Create a new file to generate diff output
    write_file(temp_dir + "/test_config.toml", "key = \"value\"\n");

    std::string diff = sync_diff(temp_dir);
    EXPECT_FALSE(diff.empty());
    // status --short should show the untracked or new file
    EXPECT_NE(diff.find("test_config.toml"), std::string::npos);
}

TEST_F(ConfigSyncTest, GitignoreExcludesHistoryDb) {
    sync_init(temp_dir);

    std::string gitignore = read_file(temp_dir + "/.gitignore");
    EXPECT_NE(gitignore.find("history.db"), std::string::npos);
}
