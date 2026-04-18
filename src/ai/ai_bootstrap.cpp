// All AI-specific startup work in one place. Extracted from main.cpp +
// repl.cpp as part of the main-split refactor so everything
// AI-conditional is localised:
//
//   tash::ai::bootstrap(state)
//     ├─ builds the context-aware transition map from history
//     └─ prompts the user to run the setup wizard on first run
//
// Callers pass a ShellState so the map build can reference the current
// history path. Build guarded by TASH_AI_ENABLED; a no-op alternative
// is provided for the disabled build so main.cpp doesn't need #ifdefs.

#ifdef TASH_AI_ENABLED

#include "tash/ai.h"
#include "tash/ai/bootstrap.h"
#include "tash/ai/llm_registry.h"
#include "tash/core/signals.h"
#include "tash/history.h"
#include "theme.h"

#include <string>
#include <termios.h>
#include <unistd.h>

namespace tash::ai {

// Build the context-aware suggestion map from the recorded history
// file so repl hints can offer "after X, people run Y" completions.
// Also installs the built-in LLM provider factories (gemini/openai/
// ollama) into the registry — this is the well-defined startup point
// where every AI-enabled code path begins.
void build_history_context() {
    register_builtin_llm_providers();
    std::string hist = history_file_path();
    if (hist.empty()) return;
    build_transition_map(hist, get_transition_map());
}

// If the user hasn't set up an AI provider yet, offer the wizard.
// Silently no-ops when stdin isn't a TTY (scripts, tests) or the user
// is already configured / using ollama.
void offer_setup_wizard() {
    if (!isatty(STDIN_FILENO)) return;

    std::string provider = ai_get_provider();
    auto key = ai_load_provider_key(provider);
    if (key || provider == "ollama") return;

    write_stdout(AI_LABEL + "tash ai" CAT_RESET + AI_SEPARATOR + " ─ " CAT_RESET
                 "AI features available! Set up now? [y/n] ");
    char setup_ch = 0;
    struct termios old_t, new_t;
    tcgetattr(STDIN_FILENO, &old_t);
    new_t = old_t;
    new_t.c_lflag &= ~(ICANON | ECHO);
    new_t.c_cc[VMIN] = 1;
    new_t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_t);
    if (read(STDIN_FILENO, &setup_ch, 1) != 1) setup_ch = 'n';
    tcsetattr(STDIN_FILENO, TCSANOW, &old_t);
    write_stdout(std::string(1, setup_ch) + "\n");
    if (setup_ch == 'y' || setup_ch == 'Y') {
        ai_run_setup_wizard();
    } else {
        write_stdout(CAT_DIM "  Tip: run @ai config anytime to set up.\n" CAT_RESET);
    }
    write_stdout("\n");
}

} // namespace tash::ai

#else  // !TASH_AI_ENABLED — provide empty stubs so callers stay clean.

#include "tash/ai/bootstrap.h"

namespace tash::ai {
void build_history_context() {}
void offer_setup_wizard() {}
} // namespace tash::ai

#endif // TASH_AI_ENABLED
