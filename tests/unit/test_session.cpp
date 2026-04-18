#include <gtest/gtest.h>
#include "tash/core/session.h"
#include "tash/shell.h"

#include <sys/stat.h>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <unistd.h>
#include <dirent.h>
#include <fstream>
#include <ctime>
#include <algorithm>

// ═══════════════════════════════════════════════════════════════
// Test fixture: creates a temp directory for sessions, cleans up
// ═══════════════════════════════════════════════════════════════

class SessionTest : public ::testing::Test {
protected:
    std::string tmp_dir;
    std::string sessions_dir;

    void SetUp() override {
        // Build a unique temp directory per test.
        std::string raw_dir = "/tmp/tash_session_test_" + std::to_string(getpid())
                              + "_" + std::to_string(std::time(nullptr));
        std::filesystem::create_directories(raw_dir);
        // Resolve symlinks (on macOS /tmp -> /private/tmp).
        char *resolved = realpath(raw_dir.c_str(), nullptr);
        if (resolved) {
            tmp_dir = resolved;
            free(resolved);
        } else {
            tmp_dir = raw_dir;
        }
        sessions_dir = tmp_dir + "/sessions/";
        std::filesystem::create_directories(sessions_dir);

        // Save and override HOME so that get_sessions_dir() (the no-arg
        // variant) does not touch the real home directory during other
        // tests.  We only use the overloads that take a directory in this
        // fixture, but store the original HOME defensively.
        original_home_ = std::getenv("HOME") ? std::getenv("HOME") : "";
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(tmp_dir, ec);
    }

    // Build a full path for a session file inside the temp dir.
    std::string session_path(const std::string &name) {
        return sessions_dir + name + ".json";
    }

    // Helper: make a sample SessionInfo with all fields populated.
    SessionInfo make_sample(const std::string &name = "mywork") {
        SessionInfo info;
        info.name = name;
        info.working_directory = "/home/testuser/project";
        info.created_at = 1713283200;
        info.last_active = 1713290400;
        info.socket_path = "/tmp/tash_" + name + ".sock";
        info.aliases["gst"] = "git status";
        info.aliases["gco"] = "git checkout";
        info.env_vars["EDITOR"] = "vim";
        info.env_vars["LANG"] = "en_US.UTF-8";
        return info;
    }

private:
    std::string original_home_;
};

// ═══════════════════════════════════════════════════════════════
// 1. SaveAndLoad
// ═══════════════════════════════════════════════════════════════

TEST_F(SessionTest, SaveAndLoad) {
    SessionInfo original = make_sample();
    std::string path = session_path("mywork");

    ASSERT_TRUE(save_session(path, original));

    SessionInfo loaded = load_session(path);
    EXPECT_EQ(loaded.name, original.name);
    EXPECT_EQ(loaded.working_directory, original.working_directory);
    EXPECT_EQ(loaded.created_at, original.created_at);
    EXPECT_EQ(loaded.last_active, original.last_active);
    EXPECT_EQ(loaded.socket_path, original.socket_path);
    EXPECT_EQ(loaded.aliases.size(), original.aliases.size());
    EXPECT_EQ(loaded.aliases.at("gst"), "git status");
    EXPECT_EQ(loaded.aliases.at("gco"), "git checkout");
    EXPECT_EQ(loaded.env_vars.size(), original.env_vars.size());
    EXPECT_EQ(loaded.env_vars.at("EDITOR"), "vim");
    EXPECT_EQ(loaded.env_vars.at("LANG"), "en_US.UTF-8");
}

// ═══════════════════════════════════════════════════════════════
// 2. SaveCreatesFile
// ═══════════════════════════════════════════════════════════════

TEST_F(SessionTest, SaveCreatesFile) {
    SessionInfo info = make_sample("newone");
    std::string path = session_path("newone");

    // File should not exist yet.
    struct stat st;
    ASSERT_NE(stat(path.c_str(), &st), 0);

    ASSERT_TRUE(save_session(path, info));

    // File should now exist.
    EXPECT_EQ(stat(path.c_str(), &st), 0);
}

// ═══════════════════════════════════════════════════════════════
// 3. LoadMissingFile
// ═══════════════════════════════════════════════════════════════

TEST_F(SessionTest, LoadMissingFile) {
    SessionInfo info = load_session(session_path("nonexistent"));
    EXPECT_TRUE(info.name.empty());
    EXPECT_TRUE(info.working_directory.empty());
    EXPECT_EQ(info.created_at, 0);
    EXPECT_EQ(info.last_active, 0);
    EXPECT_TRUE(info.aliases.empty());
    EXPECT_TRUE(info.env_vars.empty());
}

// ═══════════════════════════════════════════════════════════════
// 4. CaptureCurrentState
// ═══════════════════════════════════════════════════════════════

TEST_F(SessionTest, CaptureCurrentState) {
    ShellState state;
    state.core.aliases["ll"] = "ls -la";
    state.core.aliases["gs"] = "git status";

    // Set an env var that capture_current_state looks for.
    setenv("EDITOR", "nano", 1);

    SessionInfo captured = capture_current_state("test_capture", state);

    EXPECT_EQ(captured.name, "test_capture");
    // Working directory should be the actual cwd.
    char cwd[4096];
    ASSERT_NE(getcwd(cwd, sizeof(cwd)), nullptr);
    EXPECT_EQ(captured.working_directory, std::string(cwd));

    // Aliases should be captured.
    EXPECT_EQ(captured.aliases.size(), 2u);
    EXPECT_EQ(captured.aliases.at("ll"), "ls -la");
    EXPECT_EQ(captured.aliases.at("gs"), "git status");

    // Timestamps should be recent.
    int64_t now = static_cast<int64_t>(std::time(nullptr));
    EXPECT_LE(captured.created_at, now);
    EXPECT_GE(captured.created_at, now - 5);

    // EDITOR should be captured.
    EXPECT_EQ(captured.env_vars.count("EDITOR"), 1u);
    EXPECT_EQ(captured.env_vars.at("EDITOR"), "nano");

    unsetenv("EDITOR");
}

// ═══════════════════════════════════════════════════════════════
// 5. RestoreSession
// ═══════════════════════════════════════════════════════════════

TEST_F(SessionTest, RestoreSession) {
    SessionInfo info;
    info.name = "restore_test";
    info.working_directory = tmp_dir; // use our temp dir (exists)
    info.aliases["hello"] = "echo hello";
    info.env_vars["TASH_SESSION_TEST_VAR"] = "restored_value";

    ShellState state;

    // Save original cwd so we can restore it.
    char original_cwd[4096];
    ASSERT_NE(getcwd(original_cwd, sizeof(original_cwd)), nullptr);

    restore_session(info, state);

    // Verify cwd changed.
    char new_cwd[4096];
    ASSERT_NE(getcwd(new_cwd, sizeof(new_cwd)), nullptr);
    EXPECT_EQ(std::string(new_cwd), tmp_dir);

    // Verify aliases restored.
    EXPECT_EQ(state.core.aliases.count("hello"), 1u);
    EXPECT_EQ(state.core.aliases.at("hello"), "echo hello");

    // Verify env var set.
    const char *val = std::getenv("TASH_SESSION_TEST_VAR");
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(std::string(val), "restored_value");

    // Cleanup: restore original cwd and unset the test env var.
    chdir(original_cwd);
    unsetenv("TASH_SESSION_TEST_VAR");
}

// ═══════════════════════════════════════════════════════════════
// 6. ListSessions
// ═══════════════════════════════════════════════════════════════

TEST_F(SessionTest, ListSessions) {
    // Create 3 session files.
    ASSERT_TRUE(save_session(session_path("alpha"), make_sample("alpha")));
    ASSERT_TRUE(save_session(session_path("beta"), make_sample("beta")));
    ASSERT_TRUE(save_session(session_path("gamma"), make_sample("gamma")));

    std::vector<SessionInfo> sessions = list_sessions(sessions_dir);
    EXPECT_EQ(sessions.size(), 3u);

    // Collect names and sort for deterministic comparison.
    std::vector<std::string> names;
    for (size_t i = 0; i < sessions.size(); ++i) {
        names.push_back(sessions[i].name);
    }
    std::sort(names.begin(), names.end());
    EXPECT_EQ(names[0], "alpha");
    EXPECT_EQ(names[1], "beta");
    EXPECT_EQ(names[2], "gamma");
}

// ═══════════════════════════════════════════════════════════════
// 7. ListEmptyDir
// ═══════════════════════════════════════════════════════════════

TEST_F(SessionTest, ListEmptyDir) {
    std::vector<SessionInfo> sessions = list_sessions(sessions_dir);
    EXPECT_TRUE(sessions.empty());
}

// ═══════════════════════════════════════════════════════════════
// 8. SessionExists
// ═══════════════════════════════════════════════════════════════

TEST_F(SessionTest, SessionExists) {
    ASSERT_TRUE(save_session(session_path("exists_test"),
                             make_sample("exists_test")));

    EXPECT_TRUE(session_exists("exists_test", sessions_dir));
    EXPECT_FALSE(session_exists("no_such_session", sessions_dir));
}

// ═══════════════════════════════════════════════════════════════
// 9. DeleteSession
// ═══════════════════════════════════════════════════════════════

TEST_F(SessionTest, DeleteSession) {
    ASSERT_TRUE(save_session(session_path("to_delete"),
                             make_sample("to_delete")));
    EXPECT_TRUE(session_exists("to_delete", sessions_dir));

    EXPECT_TRUE(delete_session("to_delete", sessions_dir));
    EXPECT_FALSE(session_exists("to_delete", sessions_dir));

    // File should be gone.
    struct stat st;
    EXPECT_NE(stat(session_path("to_delete").c_str(), &st), 0);
}

// ═══════════════════════════════════════════════════════════════
// 10. DeleteNonExistent
// ═══════════════════════════════════════════════════════════════

TEST_F(SessionTest, DeleteNonExistent) {
    EXPECT_FALSE(delete_session("nonexistent", sessions_dir));
}

// ═══════════════════════════════════════════════════════════════
// 11. SessionDirCreated
// ═══════════════════════════════════════════════════════════════

TEST_F(SessionTest, SessionDirCreated) {
    // Use a subdirectory that does not exist yet.
    std::string new_dir = tmp_dir + "/brand_new/sub/sessions";
    struct stat st;
    ASSERT_NE(stat(new_dir.c_str(), &st), 0) << "dir should not exist yet";

    std::string result = get_sessions_dir(new_dir);
    EXPECT_FALSE(result.empty());

    // The directory should now exist.
    EXPECT_EQ(stat(new_dir.c_str(), &st), 0);
    EXPECT_TRUE(S_ISDIR(st.st_mode));
}

// ═══════════════════════════════════════════════════════════════
// 12. SerializationRoundTrip (special characters)
// ═══════════════════════════════════════════════════════════════

TEST_F(SessionTest, SerializationRoundTrip) {
    SessionInfo original;
    original.name = "special_chars";
    original.working_directory = "/home/user/my project";
    original.created_at = 1713283200;
    original.last_active = 1713290400;
    original.socket_path = "";

    // Aliases with special characters.
    original.aliases["log"] = "git log --oneline --graph";
    original.aliases["find-big"] = "find . -size +100M";
    original.aliases["multi"] = "echo line1\\nline2";
    original.aliases["equals"] = "echo a=b";
    original.aliases["backslash"] = "echo \\\\path";

    // Env vars with special chars.
    original.env_vars["PATH_EXTRA"] = "/usr/local/bin:/opt/bin";
    original.env_vars["MSG"] = "hello world";

    std::string path = session_path("special_chars");
    ASSERT_TRUE(save_session(path, original));

    SessionInfo loaded = load_session(path);
    EXPECT_EQ(loaded.name, original.name);
    EXPECT_EQ(loaded.working_directory, original.working_directory);
    EXPECT_EQ(loaded.created_at, original.created_at);
    EXPECT_EQ(loaded.last_active, original.last_active);

    // Verify all aliases round-tripped correctly.
    ASSERT_EQ(loaded.aliases.size(), original.aliases.size());
    for (auto it = original.aliases.begin(); it != original.aliases.end(); ++it) {
        EXPECT_EQ(loaded.aliases.at(it->first), it->second)
            << "Alias '" << it->first << "' did not round-trip";
    }

    // Verify all env vars round-tripped correctly.
    ASSERT_EQ(loaded.env_vars.size(), original.env_vars.size());
    for (auto it = original.env_vars.begin(); it != original.env_vars.end(); ++it) {
        EXPECT_EQ(loaded.env_vars.at(it->first), it->second)
            << "Env var '" << it->first << "' did not round-trip";
    }
}
