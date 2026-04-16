#ifndef TASH_PLUGIN_H
#define TASH_PLUGIN_H

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

struct ShellState;

// ── Completion types ──────────────────────────────────────────

struct Completion {
    std::string text;
    std::string description;
    enum Type {
        COMMAND,
        SUBCOMMAND,
        OPTION_SHORT,
        OPTION_LONG,
        ARGUMENT,
        FILE_PATH,
        DIRECTORY,
        VARIABLE
    };
    Type type;

    Completion() : type(COMMAND) {}
    Completion(const std::string &t, const std::string &d, Type tp)
        : text(t), description(d), type(tp) {}
};

// ── Provider interfaces ───────────────────────────────────────

class ICompletionProvider {
public:
    virtual ~ICompletionProvider() = default;
    virtual std::string name() const = 0;
    virtual int priority() const = 0;
    virtual bool can_complete(const std::string &command) const = 0;
    virtual std::vector<Completion> complete(
        const std::string &command,
        const std::string &current_word,
        const std::vector<std::string> &args,
        const ShellState &state) const = 0;
};

class IPromptProvider {
public:
    virtual ~IPromptProvider() = default;
    virtual std::string name() const = 0;
    virtual int priority() const = 0;
    virtual std::string render(const ShellState &state) = 0;
};

struct HistoryEntry {
    int64_t id;
    std::string command;
    int64_t timestamp;
    std::string directory;
    int exit_code;
    int duration_ms;
    std::string hostname;
    std::string session_id;

    HistoryEntry()
        : id(0), timestamp(0), exit_code(0), duration_ms(0) {}
};

struct SearchFilter {
    std::string directory;
    int exit_code;
    int64_t since;
    int limit;

    SearchFilter()
        : exit_code(-1), since(0), limit(50) {}
};

class IHistoryProvider {
public:
    virtual ~IHistoryProvider() = default;
    virtual std::string name() const = 0;
    virtual void record(const HistoryEntry &entry) = 0;
    virtual std::vector<HistoryEntry> search(
        const std::string &query,
        const SearchFilter &filter) const = 0;
    virtual std::vector<HistoryEntry> recent(int count) const = 0;
};

class IHookProvider {
public:
    virtual ~IHookProvider() = default;
    virtual std::string name() const = 0;
    virtual void on_before_command(const std::string &command,
                                    ShellState &state) = 0;
    virtual void on_after_command(const std::string &command,
                                   int exit_code,
                                   const std::string &stderr_output,
                                   ShellState &state) = 0;
};

// ── Plugin Registry ───────────────────────────────────────────

class PluginRegistry {
public:
    void register_completion_provider(
        std::unique_ptr<ICompletionProvider> provider);
    void register_prompt_provider(
        std::unique_ptr<IPromptProvider> provider);
    void register_history_provider(
        std::unique_ptr<IHistoryProvider> provider);
    void register_hook_provider(
        std::unique_ptr<IHookProvider> provider);

    // Dispatch: merge completions from all providers, sorted by priority
    std::vector<Completion> complete(
        const std::string &command,
        const std::string &current_word,
        const std::vector<std::string> &args,
        const ShellState &state) const;

    // Dispatch: highest-priority prompt provider renders
    std::string render_prompt(const ShellState &state);

    // Dispatch: record to all history providers
    void record_history(const HistoryEntry &entry);

    // Dispatch: search primary (first) history provider
    std::vector<HistoryEntry> search_history(
        const std::string &query,
        const SearchFilter &filter) const;

    // Dispatch: fire all hooks
    void fire_before_command(const std::string &command,
                              ShellState &state);
    void fire_after_command(const std::string &command,
                             int exit_code,
                             const std::string &stderr_output,
                             ShellState &state);

    // Introspection
    size_t completion_provider_count() const;
    size_t prompt_provider_count() const;
    size_t history_provider_count() const;
    size_t hook_provider_count() const;

private:
    std::vector<std::unique_ptr<ICompletionProvider>> completion_providers_;
    std::vector<std::unique_ptr<IPromptProvider>> prompt_providers_;
    std::vector<std::unique_ptr<IHistoryProvider>> history_providers_;
    std::vector<std::unique_ptr<IHookProvider>> hook_providers_;
};

// Process-wide plugin registry. Populated at startup in main().
PluginRegistry& global_plugin_registry();

#endif // TASH_PLUGIN_H
