// Interactive read-eval-print loop. Everything REPL-specific lives here:
// replxx setup (keybindings, hint callback, highlighter), the animated
// banner, the AI setup wizard prompt on first run, and the main input
// loop. Extracted from main.cpp during the god-file split.

#include "tash/core.h"
#include "tash/history.h"
#include "tash/ui.h"
#include "tash/repl.h"
#include "theme.h"

#include <cstring>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

#include "tash/ai/bootstrap.h"

#ifdef TASH_AI_ENABLED
#include "tash/ai.h"
#include "tash/ai/contextual_ai.h"
#endif

using namespace std;
using namespace replxx;

namespace tash {

// ── Hint callback (history + context-aware AI) ────────────────

// Shared between the hint callback and the right-arrow key handler.
static string current_hint;

static Replxx::hints_t history_hint_callback(
    const string &input, int &context_len, Replxx::Color &color,
    Replxx &rx, const ShellState &state) {
    Replxx::hints_t hints;
    current_hint.clear();

    if (input.size() < 2) return hints;

    color = Replxx::Color::GRAY;
    context_len = (int)input.size();

    // Most recent prefix match from history.
    string best;
    Replxx::HistoryScan hs(rx.history_scan());
    while (hs.next()) {
        Replxx::HistoryEntry he(hs.get());
        string entry(he.text());
        if (entry.size() > input.size() &&
            entry.compare(0, input.size(), input) == 0) {
            best = entry;
        }
    }
#ifdef TASH_AI_ENABLED
    if (best.empty() && !state.last_executed_cmd.empty()) {
        string ctx = context_suggest(state.last_executed_cmd,
                                     get_transition_map());
        if (!ctx.empty() && ctx.size() > input.size() &&
            ctx.compare(0, input.size(), input) == 0) {
            best = ctx;
        }
    }
#endif

    if (!best.empty()) {
        hints.push_back(best);
        current_hint = best;
    }
    return hints;
}

// ── Timing helper ─────────────────────────────────────────────

static double get_time_s() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

// ── Banner + AI setup prompt ──────────────────────────────────

static void print_banner() {
    if (!isatty(STDIN_FILENO)) return;

    write_stdout("\n");
    write_stdout(BANNER_FRAME + "   ╔══════════════════════════════════════════════╗" CAT_RESET "\n");
    write_stdout(BANNER_FRAME + "   ║" CAT_RESET "                                              " + BANNER_FRAME + "║" CAT_RESET "\n");
    write_stdout(BANNER_FRAME + "   ║" CAT_RESET "   " + BANNER_LOGO + "████████╗ █████╗ ███████╗██╗  ██╗" CAT_RESET "          " + BANNER_FRAME + "║" CAT_RESET "\n");
    write_stdout(BANNER_FRAME + "   ║" CAT_RESET "   " + BANNER_LOGO + "╚══██╔══╝██╔══██╗██╔════╝██║  ██║" CAT_RESET "          " + BANNER_FRAME + "║" CAT_RESET "\n");
    write_stdout(BANNER_FRAME + "   ║" CAT_RESET "   " + BANNER_LOGO + "   ██║   ███████║███████╗███████║" CAT_RESET "          " + BANNER_FRAME + "║" CAT_RESET "\n");
    write_stdout(BANNER_FRAME + "   ║" CAT_RESET "   " + BANNER_LOGO + "   ██║   ██╔══██║╚════██║██╔══██║" CAT_RESET "          " + BANNER_FRAME + "║" CAT_RESET "\n");
    write_stdout(BANNER_FRAME + "   ║" CAT_RESET "   " + BANNER_LOGO + "   ██║   ██║  ██║███████║██║  ██║" CAT_RESET "          " + BANNER_FRAME + "║" CAT_RESET "\n");
    write_stdout(BANNER_FRAME + "   ║" CAT_RESET "   " + BANNER_LOGO + "   ╚═╝   ╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝" CAT_RESET "          " + BANNER_FRAME + "║" CAT_RESET "\n");
    write_stdout(BANNER_FRAME + "   ║" CAT_RESET "                                              " + BANNER_FRAME + "║" CAT_RESET "\n");
    write_stdout(BANNER_FRAME + "   ║" CAT_RESET "   " + BANNER_TITLE + "Tavakkoli's Shell" CAT_RESET " " CAT_DIM "───" CAT_RESET " " + BANNER_VERSION + "v" TASH_VERSION_STRING CAT_RESET "               " + BANNER_FRAME + "║" CAT_RESET "\n");
    // Discovery hints instead of a feature list — feature lists on a
    // banner rot, cost screen every launch, and experienced users skip
    // them. Point at where features actually live (the man page, the
    // --features flag, the AI entrypoints) and let users pull what
    // they want.
    write_stdout(BANNER_FRAME + "   ║" CAT_RESET "   " + BANNER_FEATURE + "man tash " CAT_DIM "·" CAT_RESET " " + BANNER_FEATURE + "tash --features" CAT_RESET "                 " + BANNER_FRAME + "║" CAT_RESET "\n");
#ifdef TASH_AI_ENABLED
    write_stdout(BANNER_FRAME + "   ║" CAT_RESET "   " + BANNER_FEATURE + "@ai <question>" CAT_RESET " or " + BANNER_FEATURE + "question?" CAT_RESET " for AI help   " + BANNER_FRAME + "║" CAT_RESET "\n");
#endif
    write_stdout(BANNER_FRAME + "   ║" CAT_RESET "                                              " + BANNER_FRAME + "║" CAT_RESET "\n");
    write_stdout(BANNER_FRAME + "   ╚══════════════════════════════════════════════╝" CAT_RESET "\n");
    write_stdout("\n");

    // The AI setup wizard (and any future AI-visible first-run prompts)
    // live in src/ai/ai_bootstrap.cpp. Noop when AI is disabled or the
    // user already has a provider configured.
    tash::ai::offer_setup_wizard();
}

// ── Replxx keybinding setup ───────────────────────────────────

static void configure_replxx(Replxx &rx, ShellState &state) {
    rx.set_max_history_size(1000);

    string hist_path = history_file_path();
    if (!hist_path.empty() && isatty(STDIN_FILENO)) {
        rx.history_load(hist_path);
    }

    rx.set_completion_callback(
        [](const string &input, int &ctx) {
            return completion_callback(input, ctx);
        });

    if (isatty(STDIN_FILENO)) {
        rx.set_highlighter_callback(
            [](const string &input, Replxx::colors_t &colors) {
                syntax_highlighter(input, colors);
            });
        rx.set_hint_callback(
            [&rx, &state](const string &input, int &ctx,
                          Replxx::Color &color) {
                return history_hint_callback(input, ctx, color, rx, state);
            });
        rx.enable_bracketed_paste();
    }

    // Ctrl-L clears screen.
    rx.bind_key(Replxx::KEY::control('L'),
        [&rx](char32_t) {
            rx.clear_screen();
            return Replxx::ACTION_RESULT::CONTINUE;
        });

    rx.set_max_hint_rows(1);
    rx.set_immediate_completion(true);
    rx.set_beep_on_ambiguous_completion(false);

    // Right arrow at end of line accepts full hint (fish-style).
    rx.bind_key(Replxx::KEY::RIGHT,
        [&rx](char32_t code) {
            Replxx::State st(rx.get_state());
            int len = (int)strlen(st.text());
            if (st.cursor_position() >= len && !current_hint.empty()) {
                rx.set_state(Replxx::State(current_hint.c_str(),
                                           (int)current_hint.size()));
                current_hint.clear();
                return Replxx::ACTION_RESULT::CONTINUE;
            }
            return rx.invoke(Replxx::ACTION::MOVE_CURSOR_RIGHT, code);
        });

    // Alt+Right accepts one word from the hint.
    rx.bind_key(Replxx::KEY::meta(Replxx::KEY::RIGHT),
        [&rx](char32_t code) {
            Replxx::State st(rx.get_state());
            string current(st.text());
            int len = (int)current.size();
            if (st.cursor_position() >= len && !current_hint.empty() &&
                current_hint.size() > current.size()) {
                size_t pos = current.size();
                while (pos < current_hint.size() && current_hint[pos] == ' ') pos++;
                while (pos < current_hint.size() && current_hint[pos] != ' ') pos++;
                string partial = current_hint.substr(0, pos);
                rx.set_state(Replxx::State(partial.c_str(),
                                           (int)partial.size()));
                return Replxx::ACTION_RESULT::CONTINUE;
            }
            return rx.invoke(Replxx::ACTION::MOVE_CURSOR_ONE_WORD_RIGHT, code);
        });

    // Alt+. inserts the last argument of the previous command.
    rx.bind_key(Replxx::KEY::meta('.'),
        [&rx](char32_t) {
            Replxx::HistoryScan hs(rx.history_scan());
            string last_arg;
            while (hs.next()) {
                Replxx::HistoryEntry he(hs.get());
                string entry(he.text());
                size_t last_space = entry.find_last_of(" \t");
                if (last_space != string::npos && last_space + 1 < entry.size()) {
                    last_arg = entry.substr(last_space + 1);
                } else {
                    last_arg = entry;
                }
            }
            if (!last_arg.empty()) {
                Replxx::State st(rx.get_state());
                string current(st.text());
                int cursor = st.cursor_position();
                string new_text = current.substr(0, cursor) + last_arg +
                                  current.substr(cursor);
                rx.set_state(Replxx::State(new_text.c_str(),
                                           cursor + (int)last_arg.size()));
            }
            return Replxx::ACTION_RESULT::CONTINUE;
        });
}

// ── Entry point ───────────────────────────────────────────────

int run_interactive(ShellState &state) {
    print_banner();

    Replxx rx;
    configure_replxx(rx, state);

    string hist_path = history_file_path();

    while (true) {
        reap_background_processes(state.background_processes);

        string prompt = write_shell_prefix(state);
        char const *line = rx.input(prompt);

        if (line == nullptr) {
            state.ctrl_d_count++;
            if (state.ctrl_d_count >= 2) {
                write_stdout("\n");
                break;
            }
            write_stdout("\n");
            write_stderr("tash: press Ctrl-D again or type 'exit' to quit\n");
            continue;
        }
        state.ctrl_d_count = 0;

        string raw_line(line);
        if (raw_line.empty()) continue;

        while (!is_input_complete(raw_line)) {
            char const *cont = rx.input("> ");
            if (cont == nullptr) break;
            raw_line += "\n" + string(cont);
        }

        // Heredoc body collection. If the user typed `cmd <<EOF`, prompt
        // for body lines at "heredoc> " until each declared delimiter is
        // seen. Bodies get stitched onto segments below so
        // parse_redirections can attach them to their Redirection.
        std::vector<PendingHeredoc> all_heredocs = scan_pending_heredocs(raw_line);
        if (!all_heredocs.empty()) {
            bool ok = collect_heredoc_bodies(
                all_heredocs,
                [&rx](std::string &out) -> bool {
                    char const *hl = rx.input("heredoc> ");
                    if (!hl) return false;
                    out = hl;
                    return true;
                });
            if (!ok) {
                write_stderr("tash: heredoc: unexpected EOF\n");
                continue;
            }
        }

        for (size_t i = 0; i < raw_line.size(); i++) {
            if (raw_line[i] == '\n') raw_line[i] = ';';
        }

        string expanded = expand_history_bang(raw_line, rx);
        if (expanded.empty()) continue;
        if (expanded != raw_line) {
            write_stdout(expanded + "\n");
        }

        if (should_record_history(expanded, rx)) {
            rx.history_add(expanded);
            if (!hist_path.empty()) {
                rx.history_save(hist_path);
            }
        }

#ifdef TASH_AI_ENABLED
        if (is_ai_command(expanded)) {
            string prefill;
            state.last_exit_status =
                handle_ai_command(expanded, state, &prefill);
            if (!prefill.empty()) {
                rx.set_state(Replxx::State(prefill.c_str(),
                                           (int)prefill.size()));
            }
            continue;
        }
        if (is_ai_question(expanded)) {
            string prefill;
            state.last_exit_status =
                handle_ai_command("@ai " + expanded, state, &prefill);
            if (!prefill.empty()) {
                rx.set_state(Replxx::State(prefill.c_str(),
                                           (int)prefill.size()));
            }
            continue;
        }
#else
        {
            string ai_check = expanded;
            while (!ai_check.empty() && ai_check.front() == ' ')
                ai_check.erase(ai_check.begin());
            if (ai_check.size() >= 3 && ai_check.substr(0, 3) == "@ai" &&
                (ai_check.size() == 3 || ai_check[3] == ' ')) {
                write_stderr("tash: AI features not available (built without OpenSSL)\n");
                state.last_exit_status = 1;
                continue;
            }
        }
#endif

        reap_background_processes(state.background_processes);

        double start_time = get_time_s();
        vector<CommandSegment> segments = parse_command_line(expanded);
        // Distribute collected heredoc bodies across segments by
        // appearance order (segments left-to-right, each pulling as
        // many bodies as its command declares).
        size_t bod_idx = 0;
        for (auto &seg : segments) {
            auto seg_pending = scan_pending_heredocs(seg.command);
            for (size_t k = 0; k < seg_pending.size() &&
                               bod_idx < all_heredocs.size(); ++k, ++bod_idx) {
                seg_pending[k].body = all_heredocs[bod_idx].body;
            }
            seg.heredocs = std::move(seg_pending);
        }
        execute_command_line(segments, state);

        state.last_command_text = expanded;
#ifdef TASH_AI_ENABLED
        state.last_executed_cmd = expanded;
#endif
        state.last_cmd_duration = get_time_s() - start_time;

        reap_background_processes(state.background_processes);
    }

    if (!hist_path.empty()) {
        rx.history_save(hist_path);
    }
    return 0;
}

} // namespace tash
