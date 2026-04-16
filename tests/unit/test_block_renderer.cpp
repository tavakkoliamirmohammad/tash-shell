#include <gtest/gtest.h>
#include "tash/ui/block_renderer.h"

#include <cstdlib>
#include <string>

// ═══════════════════════════════════════════════════════════════
// format_duration tests
// ═══════════════════════════════════════════════════════════════

TEST(FormatDurationTest, SubSecond) {
    EXPECT_EQ(format_duration(0.02), "0.02s");
}

TEST(FormatDurationTest, Seconds) {
    EXPECT_EQ(format_duration(3.41), "3.41s");
}

TEST(FormatDurationTest, Minutes) {
    // 135.0s = 2m 15s
    EXPECT_EQ(format_duration(135.0), "2m 15s");
}

TEST(FormatDurationTest, Hours) {
    // 3900.0s = 1h 5m
    EXPECT_EQ(format_duration(3900.0), "1h 5m");
}

// ═══════════════════════════════════════════════════════════════
// render_block_header tests
// ═══════════════════════════════════════════════════════════════

TEST(BlockHeaderTest, Success) {
    Block b;
    b.command = "ls";
    b.exit_code = 0;
    b.duration_seconds = 0.01;
    std::string header = render_block_header(b);
    // Should contain the check mark (UTF-8: e2 9c 93)
    EXPECT_NE(header.find("\xe2\x9c\x93"), std::string::npos);
}

TEST(BlockHeaderTest, Failure) {
    Block b;
    b.command = "false";
    b.exit_code = 1;
    b.duration_seconds = 0.05;
    std::string header = render_block_header(b);
    // Should contain the cross mark (UTF-8: e2 9c 97)
    EXPECT_NE(header.find("\xe2\x9c\x97"), std::string::npos);
}

TEST(BlockHeaderTest, ContainsCommand) {
    Block b;
    b.command = "git status";
    b.exit_code = 0;
    b.duration_seconds = 0.1;
    std::string header = render_block_header(b);
    EXPECT_NE(header.find("git status"), std::string::npos);
}

TEST(BlockHeaderTest, ContainsDuration) {
    Block b;
    b.command = "sleep 3";
    b.exit_code = 0;
    b.duration_seconds = 3.41;
    std::string header = render_block_header(b);
    EXPECT_NE(header.find("3.41s"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════
// BlockManager tests
// ═══════════════════════════════════════════════════════════════

TEST(BlockManagerTest, StartEnd) {
    BlockManager mgr;
    mgr.start_block("echo hello");
    mgr.end_block("hello\n", 0, 0.01);
    EXPECT_EQ(mgr.block_count(), 1u);
    EXPECT_EQ(mgr.blocks()[0].command, "echo hello");
    EXPECT_EQ(mgr.blocks()[0].output, "hello\n");
    EXPECT_EQ(mgr.blocks()[0].exit_code, 0);
}

TEST(BlockManagerTest, Fold) {
    BlockManager mgr;
    mgr.start_block("cat file.txt");
    mgr.end_block("contents of file", 0, 0.02);

    mgr.fold(0);
    EXPECT_TRUE(mgr.blocks()[0].folded);

    // When folded, render_block should return header only (no output)
    std::string rendered = mgr.render_block(0);
    EXPECT_EQ(rendered.find("contents of file"), std::string::npos);
}

TEST(BlockManagerTest, Unfold) {
    BlockManager mgr;
    mgr.start_block("cat file.txt");
    mgr.end_block("contents of file", 0, 0.02);

    mgr.fold(0);
    mgr.unfold(0);
    EXPECT_FALSE(mgr.blocks()[0].folded);

    // When unfolded, render_block should include output
    std::string rendered = mgr.render_block(0);
    EXPECT_NE(rendered.find("contents of file"), std::string::npos);
}

TEST(BlockManagerTest, BlockCount) {
    BlockManager mgr;
    mgr.start_block("cmd1");
    mgr.end_block("out1", 0, 0.01);
    mgr.start_block("cmd2");
    mgr.end_block("out2", 0, 0.02);
    mgr.start_block("cmd3");
    mgr.end_block("out3", 1, 0.03);
    EXPECT_EQ(mgr.block_count(), 3u);
}

TEST(BlockManagerTest, GetTerminalWidthDefault) {
    // When TIOCGWINSZ is unavailable (e.g., in a test harness with
    // stdout redirected to a pipe), and COLUMNS is not set, the
    // function should return the default of 80.
    // We unset COLUMNS to ensure the env fallback is not used.
    unsetenv("COLUMNS");

    // In a test environment stdout is typically a pipe, so ioctl will
    // fail and we should get the default.  We accept either the real
    // terminal width (if running in an interactive terminal) or 80.
    int w = get_terminal_width();
    EXPECT_GT(w, 0);
}
