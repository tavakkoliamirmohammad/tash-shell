// Verifies that SqliteHistoryProvider::search treats SQL LIKE
// wildcards in the user's query as literals.
//
// Before the fix, searching for "%" matched every row because the
// provider built `"%" + query + "%"` -> `"%%%"`. After the fix the
// query is escaped and the LIKE clause uses ESCAPE '\', so the user's
// `%` matches a literal percent sign in the stored command.

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>

#include "tash/plugins/sqlite_history_provider.h"

namespace fs = std::filesystem;

namespace {

struct LikeEscapeFixture : public ::testing::Test {
    std::string tmp_db;

    void SetUp() override {
        tmp_db = "/tmp/tash_like_escape_test_" +
                 std::to_string(::getpid()) + ".db";
        std::error_code ec;
        fs::remove(tmp_db, ec);
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove(tmp_db, ec);
    }
};

HistoryEntry make_entry(const std::string &cmd) {
    HistoryEntry e;
    e.command = cmd;
    e.timestamp = 1000;
    e.directory = "/tmp";
    e.exit_code = 0;
    e.duration_ms = 1;
    e.hostname = "test";
    e.session_id = "s1";
    return e;
}

} // namespace

TEST_F(LikeEscapeFixture, PercentMatchesLiteralPercent) {
    SqliteHistoryProvider p(tmp_db);
    p.record(make_entry("progress is 100% complete"));
    p.record(make_entry("no special chars here"));
    p.record(make_entry("another plain line"));

    SearchFilter filter;
    auto hits = p.search("%", filter);
    // "%" must match ONLY the row that literally contains "%".
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_NE(hits[0].command.find('%'), std::string::npos);
}

TEST_F(LikeEscapeFixture, UnderscoreMatchesLiteralUnderscore) {
    SqliteHistoryProvider p(tmp_db);
    p.record(make_entry("my_var"));
    p.record(make_entry("myXvar"));  // underscore-as-wildcard would match this

    SearchFilter filter;
    auto hits = p.search("_", filter);
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].command, "my_var");
}

TEST_F(LikeEscapeFixture, RegularQueryStillMatches) {
    SqliteHistoryProvider p(tmp_db);
    p.record(make_entry("git status"));
    p.record(make_entry("git log"));
    p.record(make_entry("ls"));

    SearchFilter filter;
    auto hits = p.search("git", filter);
    EXPECT_EQ(hits.size(), 2u);
}
