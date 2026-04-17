#include <gtest/gtest.h>
#include "tash/plugin.h"
#include "tash/shell.h"

// ── Mock providers for testing ────────────────────────────────

class MockCompletionProvider : public ICompletionProvider {
public:
    MockCompletionProvider(const std::string &n, int pri,
                           const std::string &cmd,
                           std::vector<Completion> comps)
        : name_(n), priority_(pri), command_(cmd),
          completions_(std::move(comps)) {}

    std::string name() const override { return name_; }
    int priority() const override { return priority_; }
    bool can_complete(const std::string &command) const override {
        return command_ == "*" || command == command_;
    }
    std::vector<Completion> complete(
        const std::string &, const std::string &,
        const std::vector<std::string> &,
        const ShellState &) const override {
        return completions_;
    }

private:
    std::string name_;
    int priority_;
    std::string command_;
    std::vector<Completion> completions_;
};

class MockPromptProvider : public IPromptProvider {
public:
    MockPromptProvider(const std::string &n, int pri, const std::string &output)
        : name_(n), priority_(pri), output_(output) {}

    std::string name() const override { return name_; }
    int priority() const override { return priority_; }
    std::string render(const ShellState &) override { return output_; }

private:
    std::string name_;
    int priority_;
    std::string output_;
};

class MockHistoryProvider : public IHistoryProvider {
public:
    std::string name() const override { return "mock-history"; }

    void record(const HistoryEntry &entry) override {
        entries_.push_back(entry);
    }

    std::vector<HistoryEntry> search(
        const std::string &query,
        const SearchFilter &filter) const override {
        std::vector<HistoryEntry> results;
        for (const auto &e : entries_) {
            if (e.command.find(query) != std::string::npos) {
                results.push_back(e);
                if ((int)results.size() >= filter.limit) break;
            }
        }
        return results;
    }

    std::vector<HistoryEntry> recent(int count) const override {
        std::vector<HistoryEntry> results;
        int start = std::max(0, (int)entries_.size() - count);
        for (int i = start; i < (int)entries_.size(); i++) {
            results.push_back(entries_[i]);
        }
        return results;
    }

    size_t recorded_count() const { return entries_.size(); }

private:
    std::vector<HistoryEntry> entries_;
};

class MockHookProvider : public IHookProvider {
public:
    MockHookProvider(const std::string &n) : name_(n) {}

    std::string name() const override { return name_; }

    void on_before_command(const std::string &command,
                            ShellState &) override {
        before_commands_.push_back(command);
    }

    void on_after_command(const std::string &command,
                           int exit_code,
                           const std::string &,
                           ShellState &) override {
        after_commands_.push_back(command);
        after_exit_codes_.push_back(exit_code);
    }

    const std::vector<std::string> &before_commands() const {
        return before_commands_;
    }
    const std::vector<std::string> &after_commands() const {
        return after_commands_;
    }
    const std::vector<int> &after_exit_codes() const {
        return after_exit_codes_;
    }

private:
    std::string name_;
    std::vector<std::string> before_commands_;
    std::vector<std::string> after_commands_;
    std::vector<int> after_exit_codes_;
};

// ── Test fixture ──────────────────────────────────────────────

class PluginRegistryTest : public ::testing::Test {
protected:
    PluginRegistry registry;
    ShellState state;
};

// ── Completion provider tests ─────────────────────────────────

TEST_F(PluginRegistryTest, RegisterCompletionProvider) {
    auto provider = std::unique_ptr<ICompletionProvider>(
        new MockCompletionProvider("test", 10, "git",
            {Completion("checkout", "Switch branches", Completion::SUBCOMMAND)}));
    registry.register_completion_provider(std::move(provider));
    EXPECT_EQ(registry.completion_provider_count(), 1u);
}

TEST_F(PluginRegistryTest, RegisterMultipleProviders) {
    registry.register_completion_provider(std::unique_ptr<ICompletionProvider>(
        new MockCompletionProvider("a", 10, "git", {})));
    registry.register_completion_provider(std::unique_ptr<ICompletionProvider>(
        new MockCompletionProvider("b", 20, "git", {})));
    EXPECT_EQ(registry.completion_provider_count(), 2u);
}

TEST_F(PluginRegistryTest, CompletionDispatchMergesResults) {
    registry.register_completion_provider(std::unique_ptr<ICompletionProvider>(
        new MockCompletionProvider("low", 5, "git",
            {Completion("add", "Stage files", Completion::SUBCOMMAND)})));
    registry.register_completion_provider(std::unique_ptr<ICompletionProvider>(
        new MockCompletionProvider("high", 20, "git",
            {Completion("commit", "Record changes", Completion::SUBCOMMAND)})));

    auto results = registry.complete("git", "", {}, state);
    ASSERT_EQ(results.size(), 2u);
    // Higher priority comes first
    EXPECT_EQ(results[0].text, "commit");
    EXPECT_EQ(results[1].text, "add");
}

TEST_F(PluginRegistryTest, CompletionPriorityDeduplicates) {
    // Both providers offer "checkout" -- higher priority wins
    registry.register_completion_provider(std::unique_ptr<ICompletionProvider>(
        new MockCompletionProvider("low", 5, "git",
            {Completion("checkout", "Low desc", Completion::SUBCOMMAND)})));
    registry.register_completion_provider(std::unique_ptr<ICompletionProvider>(
        new MockCompletionProvider("high", 20, "git",
            {Completion("checkout", "High desc", Completion::SUBCOMMAND)})));

    auto results = registry.complete("git", "", {}, state);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].description, "High desc");
}

TEST_F(PluginRegistryTest, CompletionCanCompleteFilter) {
    registry.register_completion_provider(std::unique_ptr<ICompletionProvider>(
        new MockCompletionProvider("git-only", 10, "git",
            {Completion("push", "Push", Completion::SUBCOMMAND)})));

    auto git_results = registry.complete("git", "", {}, state);
    EXPECT_EQ(git_results.size(), 1u);

    auto docker_results = registry.complete("docker", "", {}, state);
    EXPECT_EQ(docker_results.size(), 0u);
}

TEST_F(PluginRegistryTest, EmptyRegistryCompletionNoError) {
    auto results = registry.complete("git", "", {}, state);
    EXPECT_TRUE(results.empty());
}

// ── Prompt provider tests ─────────────────────────────────────

TEST_F(PluginRegistryTest, PromptHighestPriorityWins) {
    registry.register_prompt_provider(std::unique_ptr<IPromptProvider>(
        new MockPromptProvider("low", 10, "low> ")));
    registry.register_prompt_provider(std::unique_ptr<IPromptProvider>(
        new MockPromptProvider("high", 20, "high> ")));

    EXPECT_EQ(registry.render_prompt(state), "high> ");
}

TEST_F(PluginRegistryTest, PromptFallbackOnEmpty) {
    // Empty registry returns "" so callers can tell no provider wanted to
    // override and fall through to their builtin prompt (used to be "$ ").
    EXPECT_EQ(registry.render_prompt(state), "");
}

TEST_F(PluginRegistryTest, PromptSingleProvider) {
    registry.register_prompt_provider(std::unique_ptr<IPromptProvider>(
        new MockPromptProvider("only", 10, "tash> ")));
    EXPECT_EQ(registry.render_prompt(state), "tash> ");
}

// ── History provider tests ────────────────────────────────────

TEST_F(PluginRegistryTest, HistoryRecordToAll) {
    auto *h1_raw = new MockHistoryProvider();
    auto *h2_raw = new MockHistoryProvider();
    registry.register_history_provider(
        std::unique_ptr<IHistoryProvider>(h1_raw));
    registry.register_history_provider(
        std::unique_ptr<IHistoryProvider>(h2_raw));

    HistoryEntry entry;
    entry.command = "ls -la";
    registry.record_history(entry);

    EXPECT_EQ(h1_raw->recorded_count(), 1u);
    EXPECT_EQ(h2_raw->recorded_count(), 1u);
}

TEST_F(PluginRegistryTest, HistorySearchPrimary) {
    auto *h1_raw = new MockHistoryProvider();
    registry.register_history_provider(
        std::unique_ptr<IHistoryProvider>(h1_raw));

    HistoryEntry entry;
    entry.command = "git status";
    registry.record_history(entry);

    SearchFilter filter;
    auto results = registry.search_history("git", filter);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].command, "git status");
}

TEST_F(PluginRegistryTest, HistorySearchEmptyNoError) {
    SearchFilter filter;
    auto results = registry.search_history("git", filter);
    EXPECT_TRUE(results.empty());
}

// ── Hook provider tests ───────────────────────────────────────

TEST_F(PluginRegistryTest, HookFiresAllBeforeCommand) {
    auto *h1_raw = new MockHookProvider("hook1");
    auto *h2_raw = new MockHookProvider("hook2");
    registry.register_hook_provider(
        std::unique_ptr<IHookProvider>(h1_raw));
    registry.register_hook_provider(
        std::unique_ptr<IHookProvider>(h2_raw));

    registry.fire_before_command("ls", state);

    ASSERT_EQ(h1_raw->before_commands().size(), 1u);
    EXPECT_EQ(h1_raw->before_commands()[0], "ls");
    ASSERT_EQ(h2_raw->before_commands().size(), 1u);
    EXPECT_EQ(h2_raw->before_commands()[0], "ls");
}

TEST_F(PluginRegistryTest, HookFiresAllAfterCommand) {
    auto *h1_raw = new MockHookProvider("hook1");
    registry.register_hook_provider(
        std::unique_ptr<IHookProvider>(h1_raw));

    registry.fire_after_command("make", 2, "error: missing file", state);

    ASSERT_EQ(h1_raw->after_commands().size(), 1u);
    EXPECT_EQ(h1_raw->after_commands()[0], "make");
    ASSERT_EQ(h1_raw->after_exit_codes().size(), 1u);
    EXPECT_EQ(h1_raw->after_exit_codes()[0], 2);
}

TEST_F(PluginRegistryTest, HookOrderPreserved) {
    auto *h1_raw = new MockHookProvider("first");
    auto *h2_raw = new MockHookProvider("second");
    registry.register_hook_provider(
        std::unique_ptr<IHookProvider>(h1_raw));
    registry.register_hook_provider(
        std::unique_ptr<IHookProvider>(h2_raw));

    registry.fire_before_command("test", state);

    // Both fired -- order is registration order
    EXPECT_EQ(h1_raw->before_commands().size(), 1u);
    EXPECT_EQ(h2_raw->before_commands().size(), 1u);
}

TEST_F(PluginRegistryTest, EmptyRegistryHooksNoError) {
    // Should not crash
    registry.fire_before_command("ls", state);
    registry.fire_after_command("ls", 0, "", state);
}
