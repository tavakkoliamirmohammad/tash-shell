// Tests that $(...) command substitution routes through the safety
// hook system. Before this change, expand_command_substitution used
// popen() directly, so `ls $(rm -rf .)` would run the dangerous inner
// command unseen by any before_command hook. The inner command now
// fires its own before_command / after_command pair via
// run_command_with_hooks_capture, and a hook-initiated skip makes the
// $(...) expand to empty instead of running.

#include <gtest/gtest.h>
#include <string>
#include <memory>

#include "tash/core.h"
#include "tash/plugin.h"
#include "tash/shell.h"

namespace {

// Minimal recording hook provider — mirrors the one in
// test_hooked_capture.cpp. Copy-pasted (rather than sharing a helper
// header) because the registry has no unregister API and each ctest
// binary runs in its own process, so scope is test-binary-local
// anyway.
class RecordingHookProvider : public IHookProvider {
public:
    std::string name() const override { return "hook-ordering-test"; }

    void on_before_command(const std::string &command,
                            ShellState &state) override {
        before_commands.push_back(command);
        if (force_skip) {
            state.skip_execution = true;
        }
    }

    void on_after_command(const std::string &command, int exit_code,
                           const std::string &,
                           ShellState &) override {
        after_commands.push_back(command);
        after_exit_codes.push_back(exit_code);
    }

    bool force_skip = false;
    std::vector<std::string> before_commands;
    std::vector<std::string> after_commands;
    std::vector<int> after_exit_codes;
};

RecordingHookProvider *install_recorder() {
    auto rec = std::unique_ptr<RecordingHookProvider>(
        new RecordingHookProvider());
    auto *raw = rec.get();
    global_plugin_registry().register_hook_provider(std::move(rec));
    return raw;
}

} // namespace

TEST(HookOrdering, CommandSubstitutionInvokesBeforeHook) {
    auto *rec = install_recorder();
    ShellState state;

    std::string expanded =
        expand_command_substitution("echo $(echo hello)", state);

    // The outer `echo` is not routed through the hook here (it's just
    // literal text around the $(...)); only the inner command is.
    // Trailing newline is stripped by expand_command_substitution.
    EXPECT_EQ(expanded, "echo hello");

    // Hook must have seen the inner command as a separate before_command
    // invocation with the exact inner string.
    bool saw_inner = false;
    for (const auto &c : rec->before_commands) {
        if (c == "echo hello") {
            saw_inner = true;
            break;
        }
    }
    EXPECT_TRUE(saw_inner);
}

TEST(HookOrdering, CommandSubstitutionRespectsSkip) {
    auto *rec = install_recorder();
    rec->force_skip = true;

    ShellState state;
    std::string expanded =
        expand_command_substitution("echo $(rm -rf /fake)", state);

    // Skip path: $(...) yields empty output, so the surrounding text
    // becomes "echo " with the space preserved and nothing appended.
    EXPECT_EQ(expanded, "echo ");

    // The inner command must have been observed before being skipped
    // (that's how a hook decides to skip in the first place).
    bool saw_inner = false;
    for (const auto &c : rec->before_commands) {
        if (c == "rm -rf /fake") {
            saw_inner = true;
            break;
        }
    }
    EXPECT_TRUE(saw_inner);

    // Disable force_skip so later tests in this binary aren't affected.
    rec->force_skip = false;
}
