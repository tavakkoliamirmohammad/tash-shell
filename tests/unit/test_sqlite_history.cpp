#include <gtest/gtest.h>
#include "tash/plugins/sqlite_history_provider.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <string>

// ── Test fixture ──────────────────────────────────────────────

class SqliteHistoryFixture : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a unique temp db path for each test
        db_path_ = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp")
                  + "/tash_test_history_XXXXXX";
        // mktemp is fine for tests -- we just need a unique name
        char *tmp = &db_path_[0];
        mktemp(tmp);
        db_path_ += ".db";
    }

    void TearDown() override {
        std::remove(db_path_.c_str());
        // Also remove WAL/SHM files SQLite may have created
        std::remove((db_path_ + "-wal").c_str());
        std::remove((db_path_ + "-shm").c_str());
    }

    HistoryEntry make_entry(const std::string &cmd,
                            int64_t ts = 0,
                            const std::string &dir = "",
                            int exit_code = 0,
                            int duration_ms = 0,
                            const std::string &session = "s1") {
        HistoryEntry e;
        e.command = cmd;
        e.timestamp = ts ? ts : static_cast<int64_t>(std::time(nullptr));
        e.directory = dir;
        e.exit_code = exit_code;
        e.duration_ms = duration_ms;
        e.hostname = "testhost";
        e.session_id = session;
        return e;
    }

    std::string db_path_;
};

// ── Basic record / retrieve ───────────────────────────────────

TEST_F(SqliteHistoryFixture, RecordAndRetrieve) {
    SqliteHistoryProvider provider(db_path_);
    provider.record(make_entry("ls -la", 1000));

    auto results = provider.recent(10);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].command, "ls -la");
}

TEST_F(SqliteHistoryFixture, RecordAllFields) {
    SqliteHistoryProvider provider(db_path_);
    HistoryEntry e;
    e.command = "make build";
    e.timestamp = 1700000000;
    e.directory = "/home/user/project";
    e.exit_code = 2;
    e.duration_ms = 3456;
    e.hostname = "devbox";
    e.session_id = "sess-42";
    provider.record(e);

    auto results = provider.recent(10);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].command, "make build");
    EXPECT_EQ(results[0].timestamp, 1700000000);
    EXPECT_EQ(results[0].directory, "/home/user/project");
    EXPECT_EQ(results[0].exit_code, 2);
    EXPECT_EQ(results[0].duration_ms, 3456);
    EXPECT_EQ(results[0].hostname, "devbox");
    EXPECT_EQ(results[0].session_id, "sess-42");
    EXPECT_GT(results[0].id, 0);
}

// ── Search tests ──────────────────────────────────────────────

TEST_F(SqliteHistoryFixture, SearchByText) {
    SqliteHistoryProvider provider(db_path_);
    provider.record(make_entry("git status", 1000));
    provider.record(make_entry("git commit -m 'fix'", 1001));
    provider.record(make_entry("ls -la", 1002));

    SearchFilter filter;
    auto results = provider.search("git", filter);
    ASSERT_EQ(results.size(), 2u);
    // Ordered by timestamp DESC
    EXPECT_EQ(results[0].command, "git commit -m 'fix'");
    EXPECT_EQ(results[1].command, "git status");
}

TEST_F(SqliteHistoryFixture, SearchFuzzy) {
    SqliteHistoryProvider provider(db_path_);
    provider.record(make_entry("docker-compose up", 1000));
    provider.record(make_entry("docker run nginx", 1001));
    provider.record(make_entry("echo hello", 1002));

    SearchFilter filter;
    auto results = provider.search("dock", filter);
    ASSERT_EQ(results.size(), 2u);
}

// ── Filter tests ──────────────────────────────────────────────

TEST_F(SqliteHistoryFixture, FilterByDirectory) {
    SqliteHistoryProvider provider(db_path_);
    provider.record(make_entry("make", 1000, "/project/a"));
    provider.record(make_entry("make test", 1001, "/project/b"));
    provider.record(make_entry("make clean", 1002, "/project/a"));

    SearchFilter filter;
    filter.directory = "/project/a";
    auto results = provider.search("make", filter);
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].command, "make clean");
    EXPECT_EQ(results[1].command, "make");
}

TEST_F(SqliteHistoryFixture, FilterByExitCode) {
    SqliteHistoryProvider provider(db_path_);
    provider.record(make_entry("make", 1000, "", 0));
    provider.record(make_entry("make broken", 1001, "", 1));
    provider.record(make_entry("make fail", 1002, "", 1));

    SearchFilter filter;
    filter.exit_code = 1;
    auto results = provider.search("make", filter);
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].command, "make fail");
    EXPECT_EQ(results[1].command, "make broken");
}

TEST_F(SqliteHistoryFixture, FilterBySince) {
    SqliteHistoryProvider provider(db_path_);
    provider.record(make_entry("old cmd", 1000));
    provider.record(make_entry("new cmd", 5000));
    provider.record(make_entry("newer cmd", 9000));

    SearchFilter filter;
    filter.since = 3000;
    auto results = provider.search("cmd", filter);
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].command, "newer cmd");
    EXPECT_EQ(results[1].command, "new cmd");
}

TEST_F(SqliteHistoryFixture, FilterCombined) {
    SqliteHistoryProvider provider(db_path_);
    provider.record(make_entry("make", 1000, "/proj", 0));
    provider.record(make_entry("make test", 5000, "/proj", 1));
    provider.record(make_entry("make run", 6000, "/other", 1));
    provider.record(make_entry("make check", 7000, "/proj", 1));

    SearchFilter filter;
    filter.directory = "/proj";
    filter.exit_code = 1;
    filter.since = 4000;
    auto results = provider.search("make", filter);
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].command, "make check");
    EXPECT_EQ(results[1].command, "make test");
}

// ── Recent ordering ───────────────────────────────────────────

TEST_F(SqliteHistoryFixture, RecentReturnsOrdered) {
    SqliteHistoryProvider provider(db_path_);
    provider.record(make_entry("first", 1000));
    provider.record(make_entry("second", 2000));
    provider.record(make_entry("third", 3000));

    auto results = provider.recent(2);
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].command, "third");
    EXPECT_EQ(results[1].command, "second");
}

// ── Dedup tests ───────────────────────────────────────────────

TEST_F(SqliteHistoryFixture, DedupConsecutive) {
    SqliteHistoryProvider provider(db_path_);
    provider.record(make_entry("ls", 1000, "", 0, 0, "s1"));
    provider.record(make_entry("ls", 1001, "", 0, 0, "s1"));
    provider.record(make_entry("ls", 1002, "", 0, 0, "s1"));

    auto results = provider.recent(10);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].command, "ls");
}

TEST_F(SqliteHistoryFixture, DedupAllowsNonConsecutive) {
    SqliteHistoryProvider provider(db_path_);
    provider.record(make_entry("ls", 1000, "", 0, 0, "s1"));
    provider.record(make_entry("pwd", 1001, "", 0, 0, "s1"));
    provider.record(make_entry("ls", 1002, "", 0, 0, "s1"));

    auto results = provider.recent(10);
    ASSERT_EQ(results.size(), 3u);
}

// ── Privacy ───────────────────────────────────────────────────

TEST_F(SqliteHistoryFixture, IgnoreLeadingSpace) {
    SqliteHistoryProvider provider(db_path_);
    provider.record(make_entry(" secret-command", 1000));
    provider.record(make_entry("normal-command", 1001));

    auto results = provider.recent(10);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].command, "normal-command");
}

// ── Migration ─────────────────────────────────────────────────

TEST_F(SqliteHistoryFixture, MigrationFromPlainText) {
    // Set up a temp directory to act as HOME
    std::string temp_home = std::string(
        std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp")
        + "/tash_migration_test_XXXXXX";
    char *tmp = &temp_home[0];
    mkdtemp(tmp);

    std::string txt_path = temp_home + "/.tash_history";
    std::string tash_dir = temp_home + "/.tash";
    std::string db = tash_dir + "/history.db";

    // Write a plain text history file
    {
        std::ofstream out(txt_path);
        out << "echo hello\n";
        out << "ls -la\n";
        out << "git status\n";
    }

    // Temporarily override HOME
    std::string old_home = std::getenv("HOME") ? std::getenv("HOME") : "";
    setenv("HOME", temp_home.c_str(), 1);

    {
        // Use default path (empty string) to trigger migration
        SqliteHistoryProvider provider;

        auto results = provider.recent(10);
        ASSERT_EQ(results.size(), 3u);
        // All entries imported
        bool found_echo = false, found_ls = false, found_git = false;
        for (const auto &r : results) {
            if (r.command == "echo hello") found_echo = true;
            if (r.command == "ls -la") found_ls = true;
            if (r.command == "git status") found_git = true;
        }
        EXPECT_TRUE(found_echo);
        EXPECT_TRUE(found_ls);
        EXPECT_TRUE(found_git);
    }

    // Verify plain text file was renamed to .bak
    struct stat st;
    EXPECT_NE(stat(txt_path.c_str(), &st), 0);  // original gone
    EXPECT_EQ(stat((txt_path + ".bak").c_str(), &st), 0);  // .bak exists

    // Restore HOME
    setenv("HOME", old_home.c_str(), 1);

    // Cleanup
    std::remove((txt_path + ".bak").c_str());
    std::remove(db.c_str());
    std::remove((db + "-wal").c_str());
    std::remove((db + "-shm").c_str());
    rmdir(tash_dir.c_str());
    rmdir(temp_home.c_str());
}

// ── Large history / performance ───────────────────────────────

TEST_F(SqliteHistoryFixture, LargeHistory) {
    SqliteHistoryProvider provider(db_path_);

    // Insert 1000 entries
    for (int i = 0; i < 1000; i++) {
        std::string cmd = "command_" + std::to_string(i);
        provider.record(make_entry(cmd, 1000 + i, "/dir", 0, 10, "s1"));
        // Alternate commands to avoid dedup
    }

    // Search should complete quickly and return results
    auto start = std::chrono::steady_clock::now();

    SearchFilter filter;
    filter.limit = 50;
    auto results = provider.search("command_5", filter);
    EXPECT_GT(results.size(), 0u);

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    // Should complete in well under 1 second
    EXPECT_LT(ms, 1000);
}

// ── Empty queries / edge cases ────────────────────────────────

TEST_F(SqliteHistoryFixture, EmptySearchNoError) {
    SqliteHistoryProvider provider(db_path_);

    SearchFilter filter;
    auto results = provider.search("nonexistent", filter);
    EXPECT_TRUE(results.empty());
}

TEST_F(SqliteHistoryFixture, EmptyRecentNoError) {
    SqliteHistoryProvider provider(db_path_);

    auto results = provider.recent(10);
    EXPECT_TRUE(results.empty());
}

TEST_F(SqliteHistoryFixture, SearchLimitRespected) {
    SqliteHistoryProvider provider(db_path_);
    for (int i = 0; i < 20; i++) {
        provider.record(make_entry("cmd " + std::to_string(i), 1000 + i));
    }

    SearchFilter filter;
    filter.limit = 5;
    auto results = provider.search("cmd", filter);
    EXPECT_EQ(results.size(), 5u);
}

TEST_F(SqliteHistoryFixture, DedupAcrossSessions) {
    SqliteHistoryProvider provider(db_path_);
    // Same command, different sessions -- should NOT dedup
    provider.record(make_entry("ls", 1000, "", 0, 0, "s1"));
    provider.record(make_entry("ls", 1001, "", 0, 0, "s2"));

    auto results = provider.recent(10);
    ASSERT_EQ(results.size(), 2u);
}

TEST_F(SqliteHistoryFixture, ProviderName) {
    SqliteHistoryProvider provider(db_path_);
    EXPECT_EQ(provider.name(), "sqlite-history");
}

TEST_F(SqliteHistoryFixture, PersistenceAcrossInstances) {
    // Record in one instance
    {
        SqliteHistoryProvider provider(db_path_);
        provider.record(make_entry("persisted cmd", 1000));
    }

    // Read from another instance using the same db
    {
        SqliteHistoryProvider provider(db_path_);
        auto results = provider.recent(10);
        ASSERT_EQ(results.size(), 1u);
        EXPECT_EQ(results[0].command, "persisted cmd");
    }
}
