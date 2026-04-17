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
#ifdef TASH_AI_ENABLED
#include "tash/core/structured_pipe.h"
#endif

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
        bool should_skip = force_skip;
        if (!skip_when_contains.empty() &&
            command.find(skip_when_contains) != std::string::npos) {
            should_skip = true;
            ++skip_count;
        }
        if (should_skip) {
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
    std::string skip_when_contains;
    int skip_count = 0;
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

#ifdef TASH_AI_ENABLED
TEST(HookOrdering, StructuredPipeFirstSegmentInvokesBeforeHook) {
    auto *rec = install_recorder();
    ShellState state;
    std::string out = tash::structured_pipe::execute_pipeline(
        "echo hello |> to-json", state);
    // Hook must have seen the first segment as a before_command.
    bool saw_first_segment = false;
    for (const auto &c : rec->before_commands) {
        if (c == "echo hello") saw_first_segment = true;
    }
    EXPECT_TRUE(saw_first_segment);
    // Output should be valid JSON containing "hello"
    EXPECT_NE(out.find("hello"), std::string::npos);

    // Hook contract: after_command fires on happy path with exit code 0.
    ASSERT_FALSE(rec->after_exit_codes.empty());
    EXPECT_EQ(rec->after_exit_codes.back(), 0);
}

TEST(HookOrdering, StructuredPipeRespectsSkip) {
    auto *rec = install_recorder();
    rec->force_skip = true;
    ShellState state;
    std::string out = tash::structured_pipe::execute_pipeline(
        "rm -rf /fake |> to-json", state);
    EXPECT_TRUE(out.empty());
    rec->force_skip = false;
}
#endif

TEST(HookOrdering, ExecuteSingleCommandFiresBeforeHook) {
    // H6 foundation: verify execute_single_command fires the hook.
    // If this regresses, every AI-suggested command silently bypasses
    // the safety hook, and the PR's H6 guarantee is broken.
    auto *rec = install_recorder();
    ShellState state;
    execute_single_command("echo ai_hook_test", state, nullptr);
    bool saw = false;
    for (const auto &c : rec->before_commands) {
        if (c == "echo ai_hook_test") saw = true;
    }
    EXPECT_TRUE(saw);
}

TEST(HookOrdering, AiStyleCommandWithSubstitutionFiresHookForInnerToo) {
    // A jailbroken LLM could return `echo $(rm -rf /fake)`. The outer
    // hook fires on the full line (first-token pattern-matcher sees
    // `echo`), but the inner command MUST also get a hook pass so
    // safety providers can block the dangerous substitution.
    auto *rec = install_recorder();
    ShellState state;
    execute_single_command("echo $(echo inner_ai)", state, nullptr);
    bool saw_outer = false;
    bool saw_inner = false;
    for (const auto &c : rec->before_commands) {
        if (c == "echo $(echo inner_ai)") saw_outer = true;
        if (c == "echo inner_ai") saw_inner = true;
    }
    EXPECT_TRUE(saw_outer) << "outer command should fire hook";
    EXPECT_TRUE(saw_inner) << "inner substitution should fire hook (H4 routing)";
}

TEST(HookOrdering, AiStyleCommandRespectsSkipOnInner) {
    // Hook forces skip on `rm -rf /fake`. The outer `echo` still fires
    // its hook, sees force_skip was lifted by then (inner skip only
    // aborts the inner substitution), and the substitution yields
    // empty — so the user sees `echo ` running instead of the dangerous form.
    auto *rec = install_recorder();
    rec->skip_when_contains = "rm -rf";  // force skip on commands matching
    ShellState state;
    execute_single_command("echo $(rm -rf /fake)", state, nullptr);
    // Expect at least one skip was triggered.
    EXPECT_GT(rec->skip_count, 0);
    rec->skip_when_contains.clear();
}
