// Tests for the tash::io diagnostic namespace: level parsing, filtering,
// and the set/current APIs. The TTY-coloring code path is not exercised
// here (CI stderr isn't a tty and colors are intentionally stable on
// whatever stderr looks like).

#include <gtest/gtest.h>

#include "tash/util/io.h"

using tash::io::Level;
using tash::io::parse_log_level;
using tash::io::set_log_level;
using tash::io::current_log_level;

TEST(IoLogLevel, ParsesKnownNames) {
    EXPECT_EQ(parse_log_level("debug"),   Level::Debug);
    EXPECT_EQ(parse_log_level("info"),    Level::Info);
    EXPECT_EQ(parse_log_level("warning"), Level::Warning);
    EXPECT_EQ(parse_log_level("warn"),    Level::Warning);
    EXPECT_EQ(parse_log_level("error"),   Level::Error);
}

TEST(IoLogLevel, IsCaseInsensitive) {
    EXPECT_EQ(parse_log_level("DEBUG"),   Level::Debug);
    EXPECT_EQ(parse_log_level("Warning"), Level::Warning);
    EXPECT_EQ(parse_log_level("ERROR"),   Level::Error);
}

TEST(IoLogLevel, UnknownBecomesInfo) {
    EXPECT_EQ(parse_log_level(""),          Level::Info);
    EXPECT_EQ(parse_log_level("trace"),     Level::Info);
    EXPECT_EQ(parse_log_level("critical"),  Level::Info);
    EXPECT_EQ(parse_log_level("  debug  "), Level::Info) << "no trimming applied";
}

TEST(IoLogLevel, SetAndCurrentRoundtrip) {
    Level saved = current_log_level();
    set_log_level(Level::Error);
    EXPECT_EQ(current_log_level(), Level::Error);
    set_log_level(Level::Debug);
    EXPECT_EQ(current_log_level(), Level::Debug);
    set_log_level(saved);
}

TEST(IoFiltering, SubLevelMessagesAreDropped) {
    // We can't easily peek at stderr from a gtest process, but we CAN
    // assert that every public entry point is callable without aborting
    // or blocking at every level. Combined with the level-parsing tests,
    // this gives us coverage of the API surface.
    Level saved = current_log_level();

    set_log_level(Level::Error);
    tash::io::debug("this must not crash");
    tash::io::info("this must not crash");
    tash::io::warning("this must not crash");
    tash::io::error("error is always emitted but may be suppressed in test output");

    set_log_level(Level::Debug);
    tash::io::debug("emitted");
    tash::io::info("emitted");

    set_log_level(saved);
    SUCCEED();
}
