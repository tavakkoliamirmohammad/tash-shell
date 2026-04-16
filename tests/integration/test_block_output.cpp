#include "test_helpers.h"

// `block <cmd>` runs the wrapped command and appends a visual header
// (box-drawing prefix + command + duration + ✓/✗ status) followed by a
// full-width separator line. These "visual" checks look for the
// specific Unicode glyphs the renderer emits.

namespace {
constexpr const char *BOX_H    = "\xe2\x94\x80"; // ─
constexpr const char *CHECK    = "\xe2\x9c\x93"; // ✓
constexpr const char *CROSS    = "\xe2\x9c\x97"; // ✗
} // namespace

// Happy path: successful command produces a ✓ in the header.
TEST(BlockOutputIntegration, SuccessHeaderHasCheckMark) {
    auto r = run_shell("block echo block_test_payload\nexit\n");
    // Command output still appears
    EXPECT_NE(r.output.find("block_test_payload"), std::string::npos);
    // Header line has the box prefix
    EXPECT_NE(r.output.find(BOX_H), std::string::npos);
    // Status marker is the ✓ glyph
    EXPECT_NE(r.output.find(CHECK), std::string::npos);
    // Command label appears (it's bolded in the header)
    EXPECT_NE(r.output.find("echo block_test_payload"), std::string::npos);
}

// A failing command yields ✗ instead of ✓.
TEST(BlockOutputIntegration, FailureHeaderHasCrossMark) {
    auto r = run_shell("block false\nexit\n");
    EXPECT_NE(r.output.find(CROSS), std::string::npos);
    EXPECT_EQ(r.output.find(CHECK), std::string::npos);
}

// The footer line is a full row of ─ characters; we can verify it
// exists as a run of 10+ consecutive ─.
TEST(BlockOutputIntegration, FooterEmitsSeparatorRow) {
    auto r = run_shell("block echo x\nexit\n");
    std::string long_sep;
    for (int i = 0; i < 10; i++) long_sep += BOX_H;
    EXPECT_NE(r.output.find(long_sep), std::string::npos);
}

// Multi-word command is preserved verbatim in the header.
TEST(BlockOutputIntegration, CommandLabelPreservedVerbatim) {
    auto r = run_shell("block echo -n one two three\nexit\n");
    EXPECT_NE(r.output.find("echo -n one two three"), std::string::npos);
}

// Missing arg: usage message, non-zero exit-ish behaviour.
TEST(BlockOutputIntegration, MissingArgPrintsUsage) {
    auto r = run_shell("block\nexit\n");
    EXPECT_NE(r.output.find("usage"), std::string::npos);
}

// The command label in the header is bold (ANSI \e[1m). Verify the
// escape appears near the command text — a genuine "visual" assertion.
TEST(BlockOutputIntegration, HeaderUsesBoldForCommand) {
    auto r = run_shell("block echo visual_block_cmd\nexit\n");
    size_t bold = r.output.find("\x1b[1m");
    size_t cmd  = r.output.find("echo visual_block_cmd");
    ASSERT_NE(bold, std::string::npos);
    ASSERT_NE(cmd,  std::string::npos);
    EXPECT_LT(bold, cmd);
}

// Status marker is colour-coded: ✓ follows ANSI green (\e[32m).
TEST(BlockOutputIntegration, CheckMarkIsGreen) {
    auto r = run_shell("block true\nexit\n");
    EXPECT_NE(r.output.find(std::string("\x1b[32m") + CHECK),
              std::string::npos);
}

// ✗ follows ANSI red (\e[31m) on failure.
TEST(BlockOutputIntegration, CrossMarkIsRed) {
    auto r = run_shell("block false\nexit\n");
    EXPECT_NE(r.output.find(std::string("\x1b[31m") + CROSS),
              std::string::npos);
}
