#include <gtest/gtest.h>
#include "tash/plugins/safety_hook_provider.h"
#include "tash/shell.h"

// ── classify_command() tests ─────────────────────────────────

TEST(SafetyHookTest, DetectsRmRfSlash) {
    EXPECT_EQ(classify_command("rm -rf /"), BLOCKED);
}

TEST(SafetyHookTest, DetectsRmRfWildcard) {
    EXPECT_EQ(classify_command("rm -rf /*"), BLOCKED);
}

TEST(SafetyHookTest, DetectsRmRfPath) {
    EXPECT_EQ(classify_command("rm -rf /important"), HIGH);
}

TEST(SafetyHookTest, DetectsRmRecursive) {
    EXPECT_EQ(classify_command("rm -r dir/"), MEDIUM);
}

TEST(SafetyHookTest, DetectsChmodRecursive777) {
    EXPECT_EQ(classify_command("chmod -R 777 /"), HIGH);
}

TEST(SafetyHookTest, DetectsChmodRecursive) {
    EXPECT_EQ(classify_command("chmod -R 644 dir"), MEDIUM);
}

TEST(SafetyHookTest, DetectsGitForcePush) {
    EXPECT_EQ(classify_command("git push --force"), HIGH);
}

TEST(SafetyHookTest, DetectsGitForcePushShort) {
    EXPECT_EQ(classify_command("git push -f"), HIGH);
}

TEST(SafetyHookTest, DetectsGitResetHard) {
    EXPECT_EQ(classify_command("git reset --hard"), HIGH);
}

TEST(SafetyHookTest, DetectsDd) {
    EXPECT_EQ(classify_command("dd if=/dev/zero of=/dev/sda"), HIGH);
}

TEST(SafetyHookTest, DetectsMkfs) {
    EXPECT_EQ(classify_command("mkfs.ext4 /dev/sda1"), HIGH);
}

TEST(SafetyHookTest, AllowsSafeCommands) {
    EXPECT_EQ(classify_command("ls"), SAFE);
    EXPECT_EQ(classify_command("echo hello"), SAFE);
    EXPECT_EQ(classify_command("cat file"), SAFE);
}

TEST(SafetyHookTest, AllowsRmSingleFile) {
    EXPECT_EQ(classify_command("rm file.txt"), SAFE);
}

TEST(SafetyHookTest, AllowsRmWithoutRecursive) {
    EXPECT_EQ(classify_command("rm -f file.txt"), SAFE);
}

TEST(SafetyHookTest, BackslashBypass) {
    EXPECT_EQ(classify_command("\\rm -rf dir/"), SAFE);
}

TEST(SafetyHookTest, EmptyCommand) {
    EXPECT_EQ(classify_command(""), SAFE);
}

TEST(SafetyHookTest, CommandWithLeadingSpaces) {
    EXPECT_EQ(classify_command("  rm -rf /"), BLOCKED);
}

TEST(SafetyHookTest, RmRfWithExtraFlags) {
    EXPECT_EQ(classify_command("rm -rfv dir/"), HIGH);
}

TEST(SafetyHookTest, GitPushForceWithBranch) {
    EXPECT_EQ(classify_command("git push --force origin main"), HIGH);
}

TEST(SafetyHookTest, TruncationDetected) {
    EXPECT_EQ(classify_command("> existing_file"), MEDIUM);
}

// ── SafetyHookProvider basic interface tests ─────────────────

TEST(SafetyHookTest, ProviderNameIsSafety) {
    SafetyHookProvider provider;
    EXPECT_EQ(provider.name(), "safety");
}

TEST(SafetyHookTest, BlockedCommandSetsSkipExecution) {
    SafetyHookProvider provider;
    ShellState state;
    // BLOCKED commands should set skip_execution without needing terminal
    provider.on_before_command("rm -rf /", state);
    EXPECT_TRUE(state.skip_execution);
}

TEST(SafetyHookTest, SafeCommandDoesNotSkipExecution) {
    SafetyHookProvider provider;
    ShellState state;
    provider.on_before_command("ls -la", state);
    EXPECT_FALSE(state.skip_execution);
}
