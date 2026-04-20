#include "test_helpers.h"

TEST(Safety, BlocksRmRfRoot) {
    auto r = run_shell("rm -rf /\nexit\n");
    // The BLOCKED warning is printed to stderr by the hook.
    EXPECT_NE(r.output.find("BLOCKED"), std::string::npos);
}

TEST(Safety, BackslashBypassRunsCommand) {
    auto r = run_shell("\\echo safety_bypass_ok\nexit\n");
    EXPECT_NE(r.output.find("safety_bypass_ok"), std::string::npos);
    EXPECT_EQ(r.output.find("BLOCKED"), std::string::npos);
}

TEST(Safety, SafeCommandNotTouched) {
    auto r = run_shell("echo ordinary_command_ok\nexit\n");
    EXPECT_NE(r.output.find("ordinary_command_ok"), std::string::npos);
    EXPECT_EQ(r.output.find("BLOCKED"), std::string::npos);
    EXPECT_EQ(r.output.find("WARNING"), std::string::npos);
}

// Regression: the safety hook used to fire before alias expansion,
// so `alias del='rm -rf'; del /` classified as harmless — the hook saw
// the raw "del /" text, the executor then alias-expanded to `rm -rf /`
// and ran it. The hook now sees the expanded text.
TEST(Safety, AliasedDangerousCommandStillBlocks) {
    auto r = run_shell("alias del='rm -rf'\ndel /\nexit\n");
    EXPECT_NE(r.output.find("BLOCKED"), std::string::npos)
        << "Aliased `rm -rf /` must still classify as BLOCKED. Output: "
        << r.output;
}

// H2 regression: nested $(...) used to bypass the classifier because
// the outer hook only saw the literal `ls $(rm -rf /)` text, then
// /bin/sh evaluated the inner $() unseen. The parser now recursively
// expands each level through tash's hook-aware helper first.
TEST(Safety, NestedCommandSubstitutionHonoursHook) {
    auto r = run_shell("echo hi $(rm -rf /)\nexit\n");
    EXPECT_NE(r.output.find("BLOCKED"), std::string::npos)
        << "Inner $(rm -rf /) must classify as BLOCKED. Output: "
        << r.output;
}

TEST(Safety, DoublyNestedCommandSubstitutionHonoursHook) {
    auto r = run_shell("echo hi $(echo $(rm -rf /))\nexit\n");
    EXPECT_NE(r.output.find("BLOCKED"), std::string::npos)
        << "Deeply nested $(rm -rf /) must classify as BLOCKED. Output: "
        << r.output;
}
