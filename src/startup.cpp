// One-time startup work: load theme, register the bundled providers,
// parse ~/.tashrc, and (optionally) handle `--benchmark` mode. Kept out
// of main.cpp so the entry point stays a thin dispatcher.

#include "tash/core.h"
#include "tash/history.h"
#include "tash/plugin.h"
#include "tash/ui.h"
#include "tash/plugins/alias_suggest_provider.h"
#include "tash/plugins/fig_completion_provider.h"
#include "tash/plugins/fish_completion_provider.h"
#include "tash/plugins/manpage_completion_provider.h"
#include "tash/plugins/safety_hook_provider.h"
#include "tash/plugins/starship_prompt_provider.h"
#include "tash/util/benchmark.h"
#include "tash/util/config_resolver.h"
#include "tash/startup.h"
#include "theme.h"

#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>

#ifdef TASH_SQLITE_ENABLED
#include "tash/plugins/sqlite_history_provider.h"
#endif

#ifdef TASH_AI_ENABLED
#include "tash/ai.h"
#include "tash/plugins/ai_error_hook_provider.h"
#endif

using std::string;

namespace tash {

// ── Env-var gating ────────────────────────────────────────────
//
// Users can disable any bundled provider without rebuilding. Non-empty
// and not "0" means disabled.
static bool plugin_disabled(const char *env) {
    const char *v = std::getenv(env);
    return v && *v && string(v) != "0";
}

// ── Provider registration ─────────────────────────────────────

void register_default_plugins() {
    auto &reg = global_plugin_registry();

    // Hook providers ------------------------------------------------
    if (!plugin_disabled("TASH_DISABLE_SAFETY_HOOK")) {
        reg.register_hook_provider(std::make_unique<SafetyHookProvider>());
    }
    if (!plugin_disabled("TASH_DISABLE_ALIAS_SUGGEST")) {
        reg.register_hook_provider(std::make_unique<AliasSuggestProvider>());
    }
#ifdef TASH_AI_ENABLED
    if (!plugin_disabled("TASH_DISABLE_AI_ERROR_HOOK")) {
        reg.register_hook_provider(std::make_unique<AiErrorHookProvider>(
            []() -> std::unique_ptr<LLMClient> { return ai_create_client(); }));
    }
#endif

    // Completion providers -----------------------------------------
    if (!plugin_disabled("TASH_DISABLE_MANPAGE_COMPLETION")) {
        reg.register_completion_provider(
            std::make_unique<ManpageCompletionProvider>());
    }
    if (!plugin_disabled("TASH_DISABLE_FISH_COMPLETION")) {
        reg.register_completion_provider(
            std::make_unique<FishCompletionProvider>());
    }
    if (!plugin_disabled("TASH_DISABLE_FIG_COMPLETION")) {
        reg.register_completion_provider(
            std::make_unique<FigCompletionProvider>());
    }

    // Prompt providers ---------------------------------------------
    if (!plugin_disabled("TASH_DISABLE_STARSHIP")) {
        reg.register_prompt_provider(
            std::make_unique<StarshipPromptProvider>());
    }

    // History providers --------------------------------------------
#ifdef TASH_SQLITE_ENABLED
    if (!plugin_disabled("TASH_DISABLE_SQLITE_HISTORY")) {
        try {
            reg.register_history_provider(
                std::make_unique<SqliteHistoryProvider>());
        } catch (const std::exception &e) {
            write_stderr(string("tash: sqlite history disabled: ") +
                         e.what() + "\n");
        }
    }
#endif
}

// ── Tashrc load ───────────────────────────────────────────────

void load_tashrc(ShellState &state) {
    std::string path = tash::config::get_tashrc_path();
    std::ifstream tashrc(path);
    if (!tashrc.is_open()) return;
    std::string rc_line;
    while (getline(tashrc, rc_line)) {
        if (rc_line.empty()) continue;
        std::vector<CommandSegment> segs = parse_command_line(rc_line);
        execute_command_line(segs, state);
    }
}

// ── Benchmark mode ────────────────────────────────────────────
//
// `tash --benchmark` times every startup stage and exits with the
// breakdown. Useful for regression detection on cold start.
int run_benchmark_mode() {
    StartupBenchmark bench;

    bench.start("Theme load");
    load_user_theme();
    bench.end();

    bench.start("Plugin registration");
    register_default_plugins();
    bench.end();

    bench.start("Shell state init");
    ShellState state; (void)state;
    bench.end();

    bench.start("Command cache");
    build_command_cache();
    bench.end();

    bench.start("History load");
    (void)history_file_path();
    bench.end();

    write_stdout(bench.report());
    return 0;
}

} // namespace tash
