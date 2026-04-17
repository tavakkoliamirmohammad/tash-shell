// Tests for run_command_with_hooks_capture — the foundation helper
// that $(...) expansion and |> pipelines route through so the plugin
// registry's safety / AI hooks see inner commands they would otherwise
// miss (previously they were popen'd directly, bypassing hooks).

#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <string>

#include "tash/core.h"
#include "tash/plugin.h"
#include "tash/shell.h"

namespace {

// Minimal fake hook provider that records calls and optionally forces
// a skip on before_command. Registered into the global registry so the
// helper (which uses global_plugin_registry()) observes it.
class RecordingHookProvider : public IHookProvider {
public:
    std::string name() const override { return "hooked-capture-test"; }

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

    // Enable before_command to set state.skip_execution = true.
    bool force_skip = false;

    std::vector<std::string> before_commands;
    std::vector<std::string> after_commands;
    std::vector<int> after_exit_codes;
};

// Register a fresh recorder, return a borrowed pointer. The registry
// has no unregister API, but ctest runs each test binary in its own
// process, so cross-test pollution within the binary is the only risk.
// We gate behaviour on force_skip so stale recorders from earlier tests
// don't corrupt later ones.
RecordingHookProvider *install_recorder() {
    auto rec = std::unique_ptr<RecordingHookProvider>(
        new RecordingHookProvider());
    auto *raw = rec.get();
    global_plugin_registry().register_hook_provider(std::move(rec));
    return raw;
}

} // namespace

TEST(HookedCapture, HappyPathCapturesStdoutAndFiresBothHooks) {
    auto *rec = install_recorder();
    ShellState state;

    HookedCaptureResult r =
        run_command_with_hooks_capture("echo hello", state);

    EXPECT_FALSE(r.skipped);
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.captured_stdout, "hello\n");

    // before + after should each have fired exactly once with the
    // exact command string we passed in.
    ASSERT_EQ(rec->before_commands.size(), 1u);
    EXPECT_EQ(rec->before_commands[0], "echo hello");
    ASSERT_EQ(rec->after_commands.size(), 1u);
    EXPECT_EQ(rec->after_commands[0], "echo hello");
    ASSERT_EQ(rec->after_exit_codes.size(), 1u);
    EXPECT_EQ(rec->after_exit_codes[0], 0);
}

TEST(HookedCapture, SkipPathBlocksExecution) {
    auto *rec = install_recorder();
    rec->force_skip = true;

    // Use a sentinel file that a SUCCESSFUL child command would create
    // via /bin/sh touch. If the helper correctly honours skip_execution,
    // the child never forks and the file never appears.
    char path[128];
    std::snprintf(path, sizeof(path),
                  "/tmp/tash_hc_sentinel_%d", (int)::getpid());
    ::unlink(path);  // ensure clean state from any prior run

    ShellState state;
    std::string cmd = std::string("touch ") + path;
    HookedCaptureResult r =
        run_command_with_hooks_capture(cmd, state);

    EXPECT_TRUE(r.skipped);
    EXPECT_EQ(r.exit_code, 1);
    EXPECT_TRUE(r.captured_stdout.empty());

    // Sentinel must not exist — the child never ran.
    struct stat st;
    EXPECT_NE(::stat(path, &st), 0);

    // skip_execution must have been reset so downstream code isn't
    // affected by the latched flag.
    EXPECT_FALSE(state.skip_execution);

    // after_command must NOT fire on the skip path (the command never
    // ran, so there is no result to report).
    EXPECT_TRUE(rec->after_commands.empty());

    // Disable force_skip so subsequent tests in this binary run normally.
    rec->force_skip = false;
    ::unlink(path);
}

TEST(HookedCapture, NonZeroExitPropagates) {
    install_recorder();
    ShellState state;

    // `false` resolves via $PATH, which works on both macOS
    // (/usr/bin/false) and Linux (/bin/false, typically symlinked
    // to /usr/bin/false on modern distros).
    HookedCaptureResult r =
        run_command_with_hooks_capture("false", state);

    EXPECT_FALSE(r.skipped);
    EXPECT_EQ(r.exit_code, 1);
    EXPECT_TRUE(r.captured_stdout.empty());
}

TEST(HookedCapture, LargeStdoutIsCapturedPastPipeBuffer) {
    // Guard against the classic "parent waitpid's before draining pipe"
    // bug by writing well beyond the Linux 64 KB pipe-buffer threshold.
    // If drain-before-wait is wrong we'd deadlock here (test timeout).
    install_recorder();
    ShellState state;

    HookedCaptureResult r = run_command_with_hooks_capture(
        "yes x | head -c 200000", state);

    EXPECT_FALSE(r.skipped);
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.captured_stdout.size(), 200000u);
}
