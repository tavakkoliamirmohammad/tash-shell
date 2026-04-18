#ifdef TASH_AI_ENABLED

#include "tash/ai.h"
#include "tash/ai/llm_registry.h"
#include "tash/ai/model_defaults.h"
#include "tash/core/executor.h"
#include "tash/core/signals.h"
#include "tash/util/io.h"
#include "theme.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <sys/stat.h>
#include <termios.h>
#include <memory>
#include <optional>
#include <thread>
#include <atomic>
#include <random>

using json = nlohmann::json;

using namespace std;

// ── Read a single character without waiting for Enter ─────────

static char read_single_char() {
    struct termios old_term, new_term;
    tcgetattr(STDIN_FILENO, &old_term);
    new_term = old_term;
    new_term.c_lflag &= ~(ICANON | ECHO);
    new_term.c_cc[VMIN] = 1;
    new_term.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_term);

    char ch = 0;
    if (read(STDIN_FILENO, &ch, 1) != 1) ch = 0;

    tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
    return ch;
}

// ── Loading spinner with fun messages ────────────────────────

static atomic<bool> spinner_active(false);

static const char *SPINNER_MESSAGES[] = {
    "Reticulating splines",
    "Consulting the oracle",
    "Defragmenting neurons",
    "Calibrating flux capacitor",
    "Compiling witty response",
    "Traversing the syntax tree",
    "Unfolding higher dimensions",
    "Brewing digital espresso",
    "Summoning shell spirits",
    "Rearranging electrons",
    "Negotiating with the kernel",
    "Decoding the matrix",
    "Harmonizing syscalls",
    "Waking up the hamsters",
    "Consulting man pages telepathically",
    "Polishing the pipeline",
    "Untangling spaghetti code",
    "Asking the rubber duck",
};
static const int NUM_SPINNER_MESSAGES = 18;

// std::optional<std::thread> replaces the raw new/delete pair the previous
// version used. Ownership semantics are identical (nullable, single
// owner), but the destructor path auto-joins via reset() and the code
// can't leak the thread on an early return (deep-review finding C3.1).
static std::optional<std::thread> spinner_thread;

static void spinner_thread_fn() {
    const char *braille[] = {
        "\xe2\xa0\x8b", "\xe2\xa0\x99", "\xe2\xa0\xb9", "\xe2\xa0\xb8",
        "\xe2\xa0\xbc", "\xe2\xa0\xb4", "\xe2\xa0\xa6", "\xe2\xa0\xa7",
        "\xe2\xa0\x87", "\xe2\xa0\x8f",
    };

    // Pick a random starting message using thread-safe RNG
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, NUM_SPINNER_MESSAGES - 1);
    int msg_idx = dist(rng);
    int frame = 0;
    int ticks_per_message = 25; // ~2 seconds per message (25 * 80ms)

    while (spinner_active.load()) {
        string line = string("\r\033[K") + AI_LABEL + braille[frame % 10] + CAT_RESET
                      + " " + CAT_DIM + SPINNER_MESSAGES[msg_idx] + "..." + CAT_RESET;
        write_stdout(line);
        frame++;
        if (frame % ticks_per_message == 0) {
            msg_idx = (msg_idx + 1) % NUM_SPINNER_MESSAGES;
        }
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 80000000; // 80ms
        nanosleep(&ts, NULL);
    }
    // Clear spinner line
    write_stdout("\r\033[K");
}

static void start_spinner() {
    if (!isatty(STDOUT_FILENO)) return;
    if (spinner_active.load()) return; // prevent double-start
    spinner_active.store(true);
    spinner_thread.emplace(spinner_thread_fn);
}

static void stop_spinner() {
    spinner_active.store(false);
    if (spinner_thread) {
        spinner_thread->join();
        spinner_thread.reset();
    }
}

// ── System prompt ────────────────────────────────────────────

static const char *PROMPT_UNIFIED =
    "You are an AI assistant embedded in a Unix shell. The user types natural language "
    "and you respond based on what they ask.\n\n"
    "Guidelines:\n"
    "- Single command: set response_type to \"command\", content to the command.\n"
    "- Multiple commands/steps: set response_type to \"steps\", and steps to an array "
    "of objects each with \"description\" (what it does) and \"command\" (the shell command). "
    "Use this when the task requires creating directories, writing files, running multiple "
    "commands in sequence, or any multi-step workflow.\n"
    "- Script file: set response_type to \"script\", content to the script, "
    "filename to a suggested name.\n"
    "- Explanations or help: set response_type to \"answer\", content to your response.\n\n"
    "Keep responses concise. No markdown formatting in content.";

// ── Output helpers ────────────────────────────────────────────

static void ai_print_label() {
    write_stdout(AI_LABEL + "tash ai" CAT_RESET + AI_SEPARATOR + " ─ " CAT_RESET);
}

static void ai_print_error(const string &msg) {
    ai_print_label();
    write_stdout(AI_ERROR + msg + CAT_RESET "\n");
}

// ── Conversation context ─────────────────────────────────────

static vector<ConversationTurn> conversation_history;
static const size_t MAX_CONVERSATION_TURNS = 10;

static void add_to_conversation(const string &role, const string &text) {
    conversation_history.push_back({role, text});
    while (conversation_history.size() > MAX_CONVERSATION_TURNS) {
        conversation_history.erase(conversation_history.begin());
    }
}

// ── Rate limiter ─────────────────────────────────────────────

static AiRateLimiter rate_limiter(10, 60); // Gemini free tier: 10 RPM

// ── Context-aware system prompt ──────────────────────────────

static string build_system_context() {
    string ctx = "You are an AI assistant embedded in tash (Tavakkoli's Shell). ";

    // OS
    #ifdef __APPLE__
    ctx += "The user is on macOS. ";
    #else
    ctx += "The user is on Linux. ";
    #endif

    // Current directory
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd))) {
        ctx += "Current directory: " + string(cwd) + ". ";
    }

    ctx += "Shell features: pipes (|), redirections (>, >>, <, 2>), "
           "aliases, background jobs (bg), globs (*,?), env vars ($VAR), "
           "command substitution ($(...)), operators (&&, ||, ;), auto-cd, "
           "and tilde expansion (~). ";

    return ctx;
}

// ── Menu input hygiene ───────────────────────────────────────
//
// Config-menu prompts read user input with getline(cin, ...) in cooked
// line mode. Two distinct problems when the user pastes:
//
// 1. The terminal is in bracketed-paste mode (replxx enables it and
//    doesn't restore before handing off to us), so the pasted bytes
//    arrive wrapped in ESC[200~ ... ESC[201~. The kernel tty driver
//    echoes those raw bytes to the screen — so the user literally
//    sees "^[[200~gemini-3.1-flash-preview^[[201~" while typing.
//
// 2. Even once we read the line, those markers are in the std::string,
//    and naive code would happily save them to disk as the "model
//    name".
//
// Fix both: temporarily disable bracketed paste (CSI ?2004l) before
// every menu read, re-enable on the way out, AND sanitize the string
// anyway to survive paste-without-bracketed-mode (screen, mosh, older
// emulators) plus any other stray ESC sequence.
namespace {
struct BracketedPasteGuard {
    bool active = false;
    BracketedPasteGuard() {
        if (isatty(STDIN_FILENO)) {
            if (write(STDOUT_FILENO, "\x1b[?2004l", 8)) {}
            active = true;
        }
    }
    ~BracketedPasteGuard() {
        if (active) {
            if (write(STDOUT_FILENO, "\x1b[?2004h", 8)) {}
        }
    }
};
} // namespace
static std::string sanitize_menu_input(const std::string &in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(in[i]);
        if (c == 0x1b) {
            // Skip an ANSI escape sequence: ESC '[' ... final-byte (0x40-0x7E).
            // Covers bracketed-paste (ESC[200~ / ESC[201~) and generic CSI.
            if (i + 1 < in.size() && in[i + 1] == '[') {
                size_t j = i + 2;
                while (j < in.size() && !(in[j] >= 0x40 && in[j] <= 0x7E)) ++j;
                i = j; // loop ++ skips past the final byte
                continue;
            }
            // Lone ESC or non-CSI sequence: drop the ESC, keep going.
            continue;
        }
        // Drop other control chars (except tab — turn into space for safety).
        if (c < 0x20 || c == 0x7f) continue;
        out.push_back(static_cast<char>(c));
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) out.pop_back();
    while (!out.empty() && (out.front() == ' ' || out.front() == '\t')) out.erase(out.begin());
    return out;
}

// Model names in the wild: "gemini-3-flash-preview", "gpt-4.1-nano",
// "llama3.2:3b", "qwen2.5-coder:7b-instruct". Accept the union of
// identifier-ish chars so new providers' naming schemes don't trip
// the check. Cap length so nothing absurd lands on disk.
static bool is_valid_model_name(const std::string &s) {
    if (s.empty() || s.size() > 128) return false;
    for (char c : s) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') ||
                  c == '-' || c == '_' || c == '.' || c == ':' || c == '/';
        if (!ok) return false;
    }
    return true;
}

// ── LLM client helpers ───────────────────────────────────────

// Provider-model compatibility check — good enough to self-heal from
// stale overrides written by pre-#128 test runs (e.g. "gpt-4o" saved
// while provider was gemini). Prefixes come from the data-driven
// registry (data/ai_models.json) so bumping provider SKU conventions
// doesn't require a code change.
static bool model_matches_provider(const std::string &provider,
                                    const std::string &model) {
    if (model.empty()) return true;
    const auto &prefixes = tash::ai::id_prefixes_for(provider);
    if (prefixes.empty()) {
        // Providers without prefix discipline (ollama hosts user-chosen
        // local model names) accept anything.
        return true;
    }
    for (const auto &p : prefixes) {
        if (model.rfind(p, 0) == 0) return true;
    }
    return false;
}

static unique_ptr<LLMClient> create_current_client() {
    string provider = ai_get_provider();

    // Self-heal FIRST — before building the client, before the null
    // check. A missing API key returns client=nullptr on some paths,
    // and we still want to clean up a bogus model override in that
    // case (the @ai setup menu runs repeatedly without a valid key
    // while the user is getting set up, and its display should not
    // show stale junk from a previous provider).
    auto model_override = ai_get_model_override();
    bool override_ok = !model_override ||
                       model_matches_provider(provider, *model_override);
    if (model_override && !override_ok) {
        static bool warned = false;
        if (!warned) {
            tash::io::warning("ignoring saved model '" + *model_override +
                               "' — incompatible with provider '" + provider +
                               "'. Using provider default.");
            warned = true;
        }
        ai_set_model_override("");
        model_override.reset();  // so the apply-step below doesn't use it
    }

    string key;
    if (provider == "gemini") {
        key = ai_load_provider_key("gemini").value_or("");
    } else if (provider == "openai") {
        key = ai_load_provider_key("openai").value_or("");
    } else if (provider == "ollama") {
        key = ai_get_ollama_url();
    }

    unique_ptr<LLMClient> client = tash::ai::create_llm_client(provider, key);
    if (!client) return client;

    if (model_override) {
        client->set_model(*model_override);
    }
    return client;
}

// Public re-export for consumers outside ai_handler.cpp (e.g. hooks).
std::unique_ptr<LLMClient> ai_create_client() {
    auto client = create_current_client();
    // One-shot debug: log the first time a client is successfully built
    // (subsequent calls are common — hooks rebuild per request). The
    // PR #124 HTTP-level debug lines are per-request and live elsewhere.
    if (client) {
        static std::atomic<bool> logged{false};
        bool expected = false;
        if (logged.compare_exchange_strong(expected, true)) {
            tash::io::debug("ai: client built for provider=" +
                            ai_get_provider() +
                            " model=" + client->get_model());
        }
    }
    return client;
}

static unique_ptr<LLMClient> ensure_client(ShellState &state) {
    if (!state.ai.ai_enabled) {
        ai_print_error("AI is disabled. Run @ai on to enable.");
        return unique_ptr<LLMClient>();
    }

    string provider = ai_get_provider();

    // Ollama doesn't need a key
    if (provider == "ollama") {
        auto client = create_current_client();
        if (!client) ai_print_error("failed to create Ollama client.");
        return client;
    }

    // Validate provider
    if (provider != "gemini" && provider != "openai") {
        ai_print_error("unknown provider '" + provider + "'. Run @ai config.");
        return unique_ptr<LLMClient>();
    }

    auto key = ai_load_provider_key(provider);
    if (!key) {
        if (!ai_run_setup_wizard()) return unique_ptr<LLMClient>();
        key = ai_load_provider_key(provider);
        if (!key) return unique_ptr<LLMClient>();
    }
    return create_current_client();
}

// ── Response tag parsing ─────────────────────────────────────

ParsedResponse parse_ai_response(const string &raw) {
    ParsedResponse result;
    result.type = RESP_ANSWER;
    result.script_filename = "script.sh";

    try {
        json j = json::parse(raw);

        string type_str = j.at("response_type").get<string>();
        result.content = j.at("content").get<string>();

        if (type_str == "command") {
            result.type = RESP_COMMAND;
        } else if (type_str == "script") {
            result.type = RESP_SCRIPT;
            if (j.count("filename") && j["filename"].is_string()) {
                string fname = j["filename"].get<string>();
                if (!fname.empty()) result.script_filename = fname;
            }
        } else if (type_str == "steps") {
            result.type = RESP_STEPS;
            if (j.count("steps") && j["steps"].is_array()) {
                for (size_t i = 0; i < j["steps"].size(); i++) {
                    StepInfo step;
                    if (j["steps"][i].count("description"))
                        step.description = j["steps"][i]["description"].get<string>();
                    if (j["steps"][i].count("command"))
                        step.command = j["steps"][i]["command"].get<string>();
                    result.steps.push_back(step);
                }
            }
        } else {
            result.type = RESP_ANSWER;
        }
    } catch (const json::exception &) {
        // JSON parse failed -- treat raw text as answer (fallback)
        result.content = raw;
        // Strip any tag-based prefixes as last resort
        string text = raw;
        while (!text.empty() && (text.front() == '\n' || text.front() == ' '))
            text.erase(text.begin());
        if (text.size() > 9 && text.substr(0, 9) == "[COMMAND]") {
            result.type = RESP_COMMAND;
            result.content = text.substr(9);
        } else if (text.size() > 8 && text.substr(0, 8) == "[SCRIPT:") {
            result.type = RESP_SCRIPT;
            size_t end = text.find(']', 8);
            if (end != string::npos) {
                result.script_filename = text.substr(8, end - 8);
                result.content = text.substr(end + 1);
            }
        } else if (text.size() > 8 && text.substr(0, 8) == "[ANSWER]") {
            result.content = text.substr(8);
        }
        while (!result.content.empty() && result.content.front() == '\n')
            result.content.erase(result.content.begin());
        while (!result.content.empty() && result.content.back() == '\n')
            result.content.pop_back();
    }

    return result;
}

// ── Unified AI handler ───────────────────────────────────────

static int handle_ask(const string &query, ShellState &state, string *prefill_cmd) {
    if (!rate_limiter.allow()) {
        ai_print_error("rate limit exceeded. Please wait a moment.");
        return 1;
    }

    unique_ptr<LLMClient> client = ensure_client(state);
    if (!client) return 1;

    string system_prompt = build_system_context() + PROMPT_UNIFIED;

    // Enrich query with error context if asking about last error
    string enriched_query = query;
    if (state.core.last_exit_status != 0 && !state.ai.last_command_text.empty()) {
        string lower_query = query;
        for (size_t i = 0; i < lower_query.size(); i++)
            lower_query[i] = tolower(lower_query[i]);

        if (lower_query.find("explain") != string::npos ||
            lower_query.find("error") != string::npos ||
            lower_query.find("wrong") != string::npos ||
            lower_query.find("fail") != string::npos ||
            lower_query.find("fix") != string::npos ||
            query == "explain") {
            enriched_query += "\n\nContext — last failed command:\n";
            enriched_query += "Command: " + state.ai.last_command_text +
                              "\nExit code: " + to_string(state.core.last_exit_status);
            if (!state.ai.last_stderr_output.empty()) {
                enriched_query += "\nError output: " + state.ai.last_stderr_output;
            }
        }
    }

    // Generate response using structured output for reliable JSON parsing
    start_spinner();
    LLMResponse resp;
    if (!conversation_history.empty()) {
        resp = client->generate_structured_with_context(system_prompt, conversation_history, enriched_query);
    } else {
        resp = client->generate_structured(system_prompt, enriched_query);
    }
    stop_spinner();

    if (!resp.success) {
        ai_print_error(resp.error_message);
        if (resp.http_status == 403) state.ai.ai_enabled = false;
        if (resp.http_status == 401 && isatty(STDIN_FILENO)) {
            write_stdout("\n");
            ai_print_label();
            write_stdout("Your API key appears invalid. Run setup? [y/n] ");
            char ch = read_single_char();
            write_stdout(string(1, ch) + "\n");
            if (ch == 'y' || ch == 'Y') {
                ai_run_setup_wizard();
            }
        }
        return 1;
    }

    ai_increment_usage();
    add_to_conversation("user", query);
    add_to_conversation("assistant", resp.text);

    // Parse the structured response
    ParsedResponse parsed = parse_ai_response(resp.text);

    switch (parsed.type) {
        case RESP_COMMAND: {
            // Show command and offer to run
            ai_print_label();
            write_stdout(AI_CMD + parsed.content + CAT_RESET "\n\n");

            if (!isatty(STDIN_FILENO)) return 0;

            write_stdout(AI_PROMPT + "Run?" CAT_RESET " [y/n/e] ");
            char ch = read_single_char();
            write_stdout(string(1, ch) + "\n");

            if (ch == 'y' || ch == 'Y') {
                write_stdout("\n");
                // H6: AI-suggested commands route through execute_single_command,
                // which fires the safety hook. Do NOT bypass — a jailbroken LLM
                // could return dangerous substitutions that the hook must still see.
                // Regression covered by test_hook_ordering.cpp::AiStyleCommand*.
                return execute_single_command(parsed.content, state);
            } else if (ch == 'e' || ch == 'E') {
                if (prefill_cmd) *prefill_cmd = parsed.content;
                write_stdout("\n");
                return 0;
            }
            write_stdout("\n");
            return 0;
        }

        case RESP_SCRIPT: {
            // Show script and offer to save
            ai_print_label();
            write_stdout("\n" + parsed.content + "\n\n");

            if (!isatty(STDIN_FILENO)) return 0;

            write_stdout(AI_PROMPT + "Save to?" CAT_RESET " [" + parsed.script_filename + "/n] ");

            string filename;
            if (!getline(cin, filename) || filename.empty() || filename[0] == 'n' || filename[0] == 'N') {
                return 0;
            }

            while (!filename.empty() && filename.back() == ' ') filename.pop_back();
            while (!filename.empty() && filename.front() == ' ') filename.erase(filename.begin());

            // If user just pressed Enter, use suggested filename
            if (filename.empty() || filename == "\n") {
                filename = parsed.script_filename;
            }

            if (filename.find("..") != string::npos) {
                ai_print_error("invalid path: '..' not allowed in filename.");
                return 1;
            }
            if (!filename.empty() && filename[0] == '/') {
                ai_print_error("invalid path: absolute paths not allowed.");
                return 1;
            }
            if (!filename.empty() && filename[0] == '~') {
                ai_print_error("invalid path: use a relative filename.");
                return 1;
            }

            ofstream out(filename);
            if (!out.is_open()) {
                ai_print_error("couldn't write to " + filename);
                return 1;
            }
            out << parsed.content << "\n";
            bool write_ok = out.good();
            out.close();

            if (!write_ok) {
                ai_print_error("failed to write to " + filename);
                return 1;
            }

            chmod(filename.c_str(), S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);

            ai_print_label();
            write_stdout(AI_CMD + "saved to " + filename + CAT_RESET "\n");
            return 0;
        }

        case RESP_STEPS: {
            // Show steps and execute one by one with confirmation
            if (parsed.steps.empty()) {
                ai_print_label();
                write_stdout("\n" + parsed.content + "\n");
                return 0;
            }

            ai_print_label();
            write_stdout(to_string(parsed.steps.size()) + " steps:\n\n");

            bool run_all = false;
            int last_status = 0;

            for (size_t i = 0; i < parsed.steps.size(); i++) {
                write_stdout(AI_STEP_NUM + "  " + to_string(i + 1) + "." CAT_RESET " "
                             + parsed.steps[i].description + "\n");
                write_stdout("     " + AI_CMD + parsed.steps[i].command + CAT_RESET "\n");

                if (!isatty(STDIN_FILENO)) continue;

                if (!run_all) {
                    write_stdout(AI_PROMPT + "     Run?" CAT_RESET " [y/n/a(ll)/s(kip)] ");
                    char ch = read_single_char();
                    write_stdout(string(1, ch) + "\n");

                    if (ch == 'n' || ch == 'N') {
                        write_stdout("\n");
                        return 0;
                    } else if (ch == 'a' || ch == 'A') {
                        run_all = true;
                    } else if (ch == 's' || ch == 'S') {
                        write_stdout("\n");
                        continue;
                    }
                    // y or a: fall through to execute
                }

                write_stdout("\n");
                // H6: AI-suggested commands route through execute_single_command,
                // which fires the safety hook. Do NOT bypass — a jailbroken LLM
                // could return dangerous substitutions that the hook must still see.
                // Regression covered by test_hook_ordering.cpp::AiStyleCommand*.
                last_status = execute_single_command(parsed.steps[i].command, state);
                if (last_status != 0 && !run_all) {
                    ai_print_error("step " + to_string(i + 1) + " failed (exit " + to_string(last_status) + "). Stop? [y/n] ");
                    char ch = read_single_char();
                    write_stdout(string(1, ch) + "\n");
                    if (ch == 'y' || ch == 'Y') return last_status;
                }
                write_stdout("\n");
            }
            return last_status;
        }

        case RESP_ANSWER:
        default: {
            // Just display the answer
            ai_print_label();
            write_stdout("\n" + parsed.content + "\n");
            return 0;
        }
    }
}

// ── Feature: Status ───────────────────────────────────────────

static int handle_status(ShellState &state) {
    static const int RPM_LIMIT = 15;

    string provider = ai_get_provider();
    auto model_override = ai_get_model_override();
    unique_ptr<LLMClient> client = create_current_client();
    string current_model = client ? client->get_model() : "unknown";
    if (model_override) current_model = *model_override;

    ai_print_label();
    write_stdout("AI Status\n\n");

    auto key = ai_load_provider_key(provider);
    bool key_ok = (provider == "ollama") || key.has_value();

    write_stdout("  Provider: " + AI_CMD + provider + CAT_RESET "\n");
    write_stdout("  Model:    " + AI_CMD + current_model + CAT_RESET "\n");
    write_stdout("  Key:      " + string(key_ok ? AI_CMD + "configured" : AI_ERROR + "not configured") + CAT_RESET "\n");
    write_stdout("  Status:   " + string(state.ai.ai_enabled ? AI_CMD + "enabled" : AI_ERROR + "disabled") + CAT_RESET "\n");

    int usage = ai_get_today_usage();
    write_stdout("  Today:    " + AI_CMD + to_string(usage) + " requests" CAT_RESET "\n");
    write_stdout("  Rate:     " CAT_DIM + to_string(RPM_LIMIT) + " requests/min (enforced)" CAT_RESET "\n\n");

    return 0;
}

// ── Public API ────────────────────────────────────────────────

bool is_ai_command(const string &input) {
    string trimmed = input;
    while (!trimmed.empty() && trimmed.front() == ' ') trimmed.erase(trimmed.begin());

    if (trimmed.size() < 3) return false;
    if (trimmed.substr(0, 3) != "@ai") return false;
    // Must be exactly "@ai" or "@ai " (not "@airplane")
    if (trimmed.size() > 3 && trimmed[3] != ' ') return false;
    return true;
}

int handle_ai_command(const string &input, ShellState &state, string *prefill_cmd) {
    string trimmed = input;
    while (!trimmed.empty() && trimmed.front() == ' ') trimmed.erase(trimmed.begin());
    string rest = (trimmed.size() > 3) ? trimmed.substr(4) : "";
    while (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());

    if (rest.empty()) {
        ai_print_label();
        write_stdout("Usage:\n\n");
        write_stdout("  @ai <anything>           ask the AI in natural language\n");
        write_stdout("  @ai config               configure provider, model, and API keys\n");
        write_stdout("  @ai clear                clear conversation history\n");
        write_stdout("  @ai on / off             enable or disable AI\n\n");
        write_stdout(CAT_DIM "  Examples:" CAT_RESET "\n");
        write_stdout(CAT_DIM "    @ai find files larger than 100MB" CAT_RESET "\n");
        write_stdout(CAT_DIM "    @ai explain this error" CAT_RESET "\n");
        write_stdout(CAT_DIM "    @ai what does tar -xzvf archive.tar.gz" CAT_RESET "\n");
        write_stdout(CAT_DIM "    @ai write a script to backup my home" CAT_RESET "\n");
        return 0;
    }

    // ── 1. config (also handles setup, status) ─────────────────

    if (rest == "config" || rest == "status" || rest == "setup") {
        if (!isatty(STDIN_FILENO)) {
            // Non-interactive: just show status
            return handle_status(state);
        }

        string provider = ai_get_provider();
        // IMPORTANT: call create_current_client() first — it self-heals
        // stale overrides (e.g. model "gpt-4o" saved while provider was
        // gemini by pre-#128 test runs) by clearing them from disk.
        // Read the override AFTER that so the display shows the real
        // current state, not the junk about to be cleaned up.
        unique_ptr<LLMClient> client = create_current_client();
        auto model = ai_get_model_override();
        string current_model = client ? client->get_model() : "unknown";
        if (model) current_model = *model;

        auto key = ai_load_provider_key(provider);
        bool key_ok = (provider == "ollama") || key.has_value();
        int usage = ai_get_today_usage();

        // Turn off bracketed-paste for the duration of the menu — otherwise
        // pasted input echoes ESC[200~ ... ESC[201~ literally to the screen.
        BracketedPasteGuard paste_guard;

        ai_print_label();
        write_stdout("Configuration\n\n");
        write_stdout("  Provider: " + AI_CMD + provider + CAT_RESET "  Model: " + AI_CMD + current_model + CAT_RESET "\n");
        write_stdout("  Key:      " + string(key_ok ? AI_CMD + "configured" : AI_ERROR + "not configured") + CAT_RESET);
        write_stdout("  Status:   " + string(state.ai.ai_enabled ? AI_CMD + "enabled" : AI_ERROR + "disabled") + CAT_RESET "\n");
        write_stdout("  Today:    " + AI_CMD + to_string(usage) + " requests" CAT_RESET
                     "  Rate: " CAT_DIM "10/min (enforced)" CAT_RESET "\n\n");
        write_stdout(AI_STEP_NUM + "  1." CAT_RESET " Switch provider (gemini/openai/ollama)\n");
        write_stdout(AI_STEP_NUM + "  2." CAT_RESET " Change model\n");
        write_stdout(AI_STEP_NUM + "  3." CAT_RESET " Set API key\n");
        write_stdout(AI_STEP_NUM + "  4." CAT_RESET " Set Ollama URL\n");
        write_stdout(AI_STEP_NUM + "  5." CAT_RESET " Test connection\n");
        write_stdout(AI_STEP_NUM + "  q." CAT_RESET " Back\n\n");
        write_stdout(AI_PROMPT + "  Choice" CAT_RESET " [1-5/q]: ");

        char ch = read_single_char();
        write_stdout(string(1, ch) + "\n\n");

        if (ch == '1') {
            write_stdout("  Provider (gemini/openai/ollama): ");
            string p;
            if (getline(cin, p)) {
                p = sanitize_menu_input(p);
                if (p == "gemini" || p == "openai" || p == "ollama") {
                    ai_set_provider(p);
                    ai_set_model_override("");
                    ai_print_label();
                    write_stdout(AI_CMD + "Provider set to " + p + "." CAT_RESET "\n");
                } else {
                    ai_print_error("unknown provider.");
                }
            }
        } else if (ch == '2') {
            write_stdout("  Model name: ");
            string m;
            if (getline(cin, m)) {
                m = sanitize_menu_input(m);
                if (!is_valid_model_name(m)) {
                    ai_print_error("invalid model name (letters, digits, dots, "
                                   "dashes, underscores only; max 128 chars).");
                } else if (!m.empty()) {
                    ai_set_model_override(m);
                    ai_print_label();
                    write_stdout(AI_CMD + "Model set to " + m + "." CAT_RESET "\n");
                }
            }
        } else if (ch == '3') {
            ai_run_setup_wizard();
        } else if (ch == '4') {
            write_stdout("  Ollama URL [http://localhost:11434]: ");
            string url;
            if (getline(cin, url)) {
                url = sanitize_menu_input(url);
                if (url.empty()) url = "http://localhost:11434";
                ai_set_ollama_url(url);
                ai_print_label();
                write_stdout(AI_CMD + "Ollama URL set to " + url + "." CAT_RESET "\n");
            }
        } else if (ch == '5') {
            // Test connection
            if (!rate_limiter.allow()) {
                ai_print_error("rate limit exceeded. Please wait a moment.");
                return 1;
            }
            unique_ptr<LLMClient> test_client = create_current_client();
            if (!test_client) {
                ai_print_error("couldn't create client. Check your config.");
                return 1;
            }
            ai_print_label();
            write_stdout("testing connection...\n");
            start_spinner();
            LLMResponse test_resp = test_client->generate("Reply with exactly: ok", "test");
            stop_spinner();
            if (test_resp.success) {
                ai_print_label();
                write_stdout(AI_CMD + "Connection successful!" CAT_RESET "\n");
            } else {
                ai_print_error("connection failed: " + test_resp.error_message);
            }
        }

        return 0;
    }

    // ── 2. on, off ────────────────────────────────────────────

    if (rest == "on") {
        state.ai.ai_enabled = true;
        ai_print_label();
        write_stdout(AI_CMD + "AI enabled." CAT_RESET "\n");
        return 0;
    }

    if (rest == "off") {
        state.ai.ai_enabled = false;
        ai_print_label();
        write_stdout(CAT_YELLOW + "AI disabled." CAT_RESET "\n");
        return 0;
    }

    // ── 3. clear ───────────────────────────────────────────────

    if (rest == "clear") {
        conversation_history.clear();
        ai_print_label();
        write_stdout(AI_CMD + "Conversation cleared." CAT_RESET "\n");
        return 0;
    }

    // ── 4. Hidden shortcuts (still work, not shown in help) ──

    if (rest == "test") {
        if (!rate_limiter.allow()) {
            ai_print_error("rate limit exceeded. Please wait a moment.");
            return 1;
        }
        unique_ptr<LLMClient> client = create_current_client();
        if (!client) {
            ai_print_error("not configured. Run @ai config.");
            return 1;
        }
        ai_print_label();
        write_stdout("testing connection...\n");
        start_spinner();
        LLMResponse resp = client->generate("Reply with exactly: ok", "test");
        stop_spinner();
        if (resp.success) {
            ai_print_label();
            write_stdout(AI_CMD + "Connection successful!" CAT_RESET "\n");
            return 0;
        }
        ai_print_error("test failed: " + resp.error_message);
        return 1;
    }

    if (rest.size() > 9 && rest.substr(0, 9) == "provider ") {
        string provider = rest.substr(9);
        while (!provider.empty() && provider.front() == ' ') provider.erase(provider.begin());
        while (!provider.empty() && provider.back() == ' ') provider.pop_back();
        if (provider != "gemini" && provider != "openai" && provider != "ollama") {
            ai_print_error("unknown provider. Use: gemini, openai, or ollama");
            return 1;
        }
        ai_set_provider(provider);
        ai_set_model_override("");
        ai_print_label();
        write_stdout(AI_CMD + "Provider set to " + provider + "." CAT_RESET "\n");
        return 0;
    }

    if (rest.size() > 6 && rest.substr(0, 6) == "model ") {
        string model = rest.substr(6);
        while (!model.empty() && model.front() == ' ') model.erase(model.begin());
        while (!model.empty() && model.back() == ' ') model.pop_back();
        if (!model.empty()) {
            ai_set_model_override(model);
            ai_print_label();
            write_stdout(AI_CMD + "Model set to " + model + "." CAT_RESET "\n");
        }
        return 0;
    }

    // ── 5. Everything else: ask the AI ───────────────────────

    return handle_ask(rest, state, prefill_cmd);
}

#endif // TASH_AI_ENABLED
