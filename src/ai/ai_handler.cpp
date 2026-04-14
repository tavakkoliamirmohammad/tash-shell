#ifdef TASH_AI_ENABLED

#include "tash/ai.h"
#include "tash/core.h"
#include "theme.h"
#include <iostream>
#include <fstream>
#include <sys/stat.h>
#include <termios.h>

using namespace std;

// Read a single character without waiting for Enter
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

// ── System prompts ────────────────────────────────────────────

static const char *PROMPT_NL_TO_CMD =
    "You are a shell command expert. Given a natural language description, "
    "output ONLY the shell command. No explanation, no markdown, no backticks.";

static const char *PROMPT_ERROR_EXPLAIN =
    "Explain this shell error concisely. Include what went wrong and how to fix it. "
    "Do not use markdown formatting.";

static const char *PROMPT_CMD_EXPLAIN =
    "Explain what this command does, flag by flag, in plain English. "
    "Format each flag on its own line like: -x  description. "
    "Do not use markdown formatting.";

static const char *PROMPT_SCRIPT_GEN =
    "Write a bash script for the following task. Include brief comments. "
    "Output ONLY the script, no explanation, no markdown backticks.";

static const char *PROMPT_WORKFLOW =
    "Give step-by-step instructions for this task. Be concise and practical. "
    "Number each step. Include actual commands where relevant. "
    "Do not use markdown formatting.";

// ── Output helpers ────────────────────────────────────────────

static void ai_print_label() {
    write_stdout(AI_LABEL "tash ai" CAT_RESET AI_SEPARATOR " ─ " CAT_RESET);
}

static void ai_print_error(const string &msg) {
    ai_print_label();
    write_stdout(AI_ERROR + msg + CAT_RESET "\n");
}

// ── Ensure AI is ready ────────────────────────────────────────
//
// Returns true if the currently selected backend is usable. For Gemini,
// this ensures an API key exists (runs the setup wizard if not). For
// Ollama, no key is needed — the sentinel string "ollama" is returned so
// callers can keep their "empty means error" check.

static string ensure_api_key(ShellState &state) {
    if (!state.ai_enabled) {
        ai_print_error("AI is disabled. Run @ai on to enable.");
        return "";
    }

    if (ai_get_backend() == AI_BACKEND_OLLAMA) {
        return "ollama"; // sentinel; ai_generate ignores the value
    }

    string key = ai_load_key();
    if (key.empty()) {
        if (!ai_run_setup_wizard()) return "";
        key = ai_load_key();
    }
    return key;
}

// ── Parse @ai input ───────────────────────────────────────────

static string extract_quoted_query(const string &input, size_t start) {
    size_t q1 = input.find('"', start);
    if (q1 == string::npos) {
        string query = input.substr(start);
        while (!query.empty() && query.front() == ' ') query.erase(query.begin());
        while (!query.empty() && query.back() == ' ') query.pop_back();
        return query;
    }
    size_t q2 = input.find('"', q1 + 1);
    if (q2 == string::npos) q2 = input.size();
    return input.substr(q1 + 1, q2 - q1 - 1);
}

// ── Feature: Natural language to command ──────────────────────

static int handle_nl_to_cmd(const string &query, ShellState &state) {
    string key = ensure_api_key(state);
    if (key.empty()) return 1;

    GeminiResponse resp = ai_generate(PROMPT_NL_TO_CMD, query, key);
    ai_increment_usage();

    if (!resp.success) {
        ai_print_error(resp.error_message);
        if (resp.http_status == 403) state.ai_enabled = false;
        return 1;
    }

    string cmd = resp.text;
    while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r')) cmd.pop_back();
    while (!cmd.empty() && (cmd.front() == '\n' || cmd.front() == '\r')) cmd.erase(cmd.begin());

    ai_print_label();
    write_stdout(AI_CMD + cmd + CAT_RESET "\n\n");
    write_stdout(AI_PROMPT "Run?" CAT_RESET " [y/n/e] ");

    char ch = read_single_char();
    write_stdout(string(1, ch) + "\n");

    if (ch == 'y' || ch == 'Y') {
        write_stdout("\n");
        return execute_single_command(cmd, state);
    } else if (ch == 'e' || ch == 'E') {
        write_stdout("\n" CAT_DIM "Command: " CAT_RESET + cmd + "\n");
        return 0;
    }

    write_stdout("\n");
    return 0;
}

// ── Feature: Error explanation ────────────────────────────────

static int handle_explain_error(ShellState &state) {
    if (state.last_command_text.empty()) {
        ai_print_error("no recent errors to explain.");
        return 1;
    }

    string key = ensure_api_key(state);
    if (key.empty()) return 1;

    string user_prompt = "Command: " + state.last_command_text +
                         "\nExit code: " + to_string(state.last_exit_status);
    if (!state.last_stderr_output.empty()) {
        user_prompt += "\nError output: " + state.last_stderr_output;
    }

    GeminiResponse resp = ai_generate(PROMPT_ERROR_EXPLAIN, user_prompt, key);
    ai_increment_usage();

    if (!resp.success) {
        ai_print_error(resp.error_message);
        if (resp.http_status == 403) state.ai_enabled = false;
        return 1;
    }

    ai_print_label();
    write_stdout(AI_ERROR + state.last_command_text + CAT_RESET
                 " exited with " + to_string(state.last_exit_status) + "\n\n");
    write_stdout(resp.text + "\n");
    return 0;
}

// ── Feature: Command explanation ──────────────────────────────

static int handle_explain_cmd(const string &cmd_text, ShellState &state) {
    string key = ensure_api_key(state);
    if (key.empty()) return 1;

    GeminiResponse resp = ai_generate(PROMPT_CMD_EXPLAIN, cmd_text, key);
    ai_increment_usage();

    if (!resp.success) {
        ai_print_error(resp.error_message);
        if (resp.http_status == 403) state.ai_enabled = false;
        return 1;
    }

    ai_print_label();
    write_stdout(AI_CMD + cmd_text + CAT_RESET "\n\n");
    write_stdout(resp.text + "\n");
    return 0;
}

// ── Feature: Script generation ────────────────────────────────

static int handle_script(const string &query, ShellState &state) {
    string key = ensure_api_key(state);
    if (key.empty()) return 1;

    GeminiResponse resp = ai_generate(PROMPT_SCRIPT_GEN, query, key);
    ai_increment_usage();

    if (!resp.success) {
        ai_print_error(resp.error_message);
        if (resp.http_status == 403) state.ai_enabled = false;
        return 1;
    }

    ai_print_label();
    write_stdout("\n" + resp.text + "\n\n");
    write_stdout(AI_PROMPT "Save to?" CAT_RESET " [filename/n] ");

    // For save prompt, we need a full filename so use getline
    string filename;
    if (!getline(cin, filename) || filename.empty() || filename[0] == 'n' || filename[0] == 'N') {
        return 0;
    }

    while (!filename.empty() && filename.back() == ' ') filename.pop_back();
    while (!filename.empty() && filename.front() == ' ') filename.erase(filename.begin());

    ofstream out(filename);
    if (!out.is_open()) {
        ai_print_error("couldn't write to " + filename);
        return 1;
    }
    out << resp.text << "\n";
    out.close();

    chmod(filename.c_str(), S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);

    ai_print_label();
    write_stdout(AI_CMD "saved to " + filename + CAT_RESET "\n");
    return 0;
}

// ── Feature: Workflow help ────────────────────────────────────

static int handle_help(const string &query, ShellState &state) {
    string key = ensure_api_key(state);
    if (key.empty()) return 1;

    GeminiResponse resp = ai_generate(PROMPT_WORKFLOW, query, key);
    ai_increment_usage();

    if (!resp.success) {
        ai_print_error(resp.error_message);
        if (resp.http_status == 403) state.ai_enabled = false;
        return 1;
    }

    ai_print_label();
    write_stdout("\n" + resp.text + "\n");
    return 0;
}

// ── Feature: Status ───────────────────────────────────────────

static int handle_status(ShellState &state) {
    ai_print_label();
    write_stdout("AI Status\n\n");

    AIBackend backend = ai_get_backend();
    write_stdout("  Backend:  " AI_CMD + string(ai_backend_name(backend)) + CAT_RESET "\n");
    write_stdout("  Status:   " + string(state.ai_enabled ? AI_CMD "enabled" : AI_ERROR "disabled") + CAT_RESET "\n");

    if (backend == AI_BACKEND_OLLAMA) {
        write_stdout("  URL:      " CAT_DIM + ai_get_ollama_url() + CAT_RESET "\n");
        write_stdout("  Model:    " CAT_DIM + ai_get_ollama_model() + CAT_RESET "\n");
        write_stdout("\n");
        return 0;
    }

    // Gemini 3.1 Flash Lite free tier limits
    static const int DAILY_LIMIT = 500;
    static const int RPM_LIMIT = 15;

    string key = ai_load_key();
    write_stdout("  Key:      " + string(key.empty() ? AI_ERROR "not configured" : AI_CMD "configured") + CAT_RESET "\n");

    int usage = ai_get_today_usage();
    int remaining = DAILY_LIMIT - usage;
    if (remaining < 0) remaining = 0;

    string usage_color = (remaining > 100) ? AI_CMD : (remaining > 0) ? CAT_YELLOW : AI_ERROR;
    write_stdout("  Today:    " + usage_color + to_string(usage) + " / " + to_string(DAILY_LIMIT) +
                 " requests" CAT_RESET CAT_DIM " (" + to_string(remaining) + " remaining)" CAT_RESET "\n");
    write_stdout("  Rate:     " CAT_DIM + to_string(RPM_LIMIT) + " requests/min" CAT_RESET "\n");

    write_stdout("  Model:    " CAT_DIM "gemini-3.1-flash-lite-preview" CAT_RESET "\n");
    write_stdout("\n");
    return 0;
}

// ── Feature: Backend switching ────────────────────────────────

static int handle_backend(const string &arg) {
    if (arg.empty()) {
        ai_print_label();
        write_stdout("Current backend: " AI_CMD + string(ai_backend_name(ai_get_backend())) + CAT_RESET "\n");
        write_stdout(CAT_DIM "  Switch with: @ai backend gemini | @ai backend ollama" CAT_RESET "\n");
        return 0;
    }
    if (arg == "gemini") {
        if (!ai_set_backend(AI_BACKEND_GEMINI)) {
            ai_print_error("failed to save backend preference.");
            return 1;
        }
        ai_print_label();
        write_stdout(AI_CMD "Backend set to gemini." CAT_RESET "\n");
        return 0;
    }
    if (arg == "ollama") {
        if (!ai_set_backend(AI_BACKEND_OLLAMA)) {
            ai_print_error("failed to save backend preference.");
            return 1;
        }
        ai_print_label();
        write_stdout(AI_CMD "Backend set to ollama." CAT_RESET "\n");
        write_stdout(CAT_DIM "  URL:   " + ai_get_ollama_url() + CAT_RESET "\n");
        write_stdout(CAT_DIM "  Model: " + ai_get_ollama_model() + CAT_RESET "\n");
        write_stdout(CAT_DIM "  Make sure `ollama serve` is running and the model is pulled." CAT_RESET "\n");
        return 0;
    }
    ai_print_error("unknown backend '" + arg + "'. Use 'gemini' or 'ollama'.");
    return 1;
}

static int handle_model(const string &arg) {
    if (ai_get_backend() != AI_BACKEND_OLLAMA) {
        ai_print_error("@ai model only applies to the ollama backend.");
        return 1;
    }
    if (arg.empty()) {
        ai_print_label();
        write_stdout("Current ollama model: " AI_CMD + ai_get_ollama_model() + CAT_RESET "\n");
        write_stdout(CAT_DIM "  Change with: @ai model <name>  (e.g. @ai model llama3.2)" CAT_RESET "\n");
        return 0;
    }
    if (!ai_set_ollama_model(arg)) {
        ai_print_error("failed to save ollama model preference.");
        return 1;
    }
    ai_print_label();
    write_stdout(AI_CMD "Ollama model set to " + arg + CAT_RESET "\n");
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

int handle_ai_command(const string &input, ShellState &state) {
    string trimmed = input;
    while (!trimmed.empty() && trimmed.front() == ' ') trimmed.erase(trimmed.begin());
    string rest = (trimmed.size() > 3) ? trimmed.substr(4) : "";
    while (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());

    if (rest.empty()) {
        ai_print_label();
        write_stdout("Usage:\n");
        write_stdout("  @ai \"question\"           generate a shell command\n");
        write_stdout("  @ai explain              explain last failed command\n");
        write_stdout("  @ai what does <cmd>      explain a command\n");
        write_stdout("  @ai script \"task\"         generate a bash script\n");
        write_stdout("  @ai help \"topic\"          step-by-step guidance\n");
        write_stdout("  @ai status               show AI usage status\n");
        write_stdout("  @ai setup                configure Gemini API key\n");
        write_stdout("  @ai backend [name]       show/switch backend (gemini, ollama)\n");
        write_stdout("  @ai model [name]         show/set ollama model\n");
        write_stdout("  @ai on / off             enable or disable AI\n");
        return 0;
    }

    // @ai backend [gemini|ollama]
    if (rest == "backend") {
        return handle_backend("");
    }
    if (rest.size() > 8 && rest.substr(0, 8) == "backend ") {
        string arg = rest.substr(8);
        while (!arg.empty() && arg.front() == ' ') arg.erase(arg.begin());
        while (!arg.empty() && arg.back() == ' ') arg.pop_back();
        return handle_backend(arg);
    }

    // @ai model [name]
    if (rest == "model") {
        return handle_model("");
    }
    if (rest.size() > 6 && rest.substr(0, 6) == "model ") {
        string arg = rest.substr(6);
        while (!arg.empty() && arg.front() == ' ') arg.erase(arg.begin());
        while (!arg.empty() && arg.back() == ' ') arg.pop_back();
        return handle_model(arg);
    }

    // @ai setup
    if (rest == "setup") {
        ai_run_setup_wizard();
        return 0;
    }

    // @ai on
    if (rest == "on") {
        state.ai_enabled = true;
        ai_print_label();
        write_stdout(AI_CMD "AI enabled." CAT_RESET "\n");
        return 0;
    }

    // @ai off
    if (rest == "off") {
        state.ai_enabled = false;
        ai_print_label();
        write_stdout(CAT_YELLOW "AI disabled." CAT_RESET "\n");
        return 0;
    }

    // @ai status
    if (rest == "status") {
        return handle_status(state);
    }

    // @ai explain
    if (rest == "explain") {
        return handle_explain_error(state);
    }

    // @ai what does <command>
    if (rest.size() > 9 && rest.substr(0, 9) == "what does") {
        string cmd_text = rest.substr(9);
        while (!cmd_text.empty() && cmd_text.front() == ' ') cmd_text.erase(cmd_text.begin());
        if (cmd_text.empty()) {
            ai_print_error("usage: @ai what does <command>");
            return 1;
        }
        return handle_explain_cmd(cmd_text, state);
    }

    // @ai what <command> (shorthand)
    if (rest.size() > 5 && rest.substr(0, 5) == "what ") {
        string cmd_text = rest.substr(5);
        while (!cmd_text.empty() && cmd_text.front() == ' ') cmd_text.erase(cmd_text.begin());
        if (cmd_text.empty()) {
            ai_print_error("usage: @ai what <command>");
            return 1;
        }
        return handle_explain_cmd(cmd_text, state);
    }

    // @ai script "..."
    if (rest.size() > 6 && rest.substr(0, 6) == "script") {
        string query = extract_quoted_query(rest, 6);
        if (query.empty()) {
            ai_print_error("usage: @ai script \"task description\"");
            return 1;
        }
        return handle_script(query, state);
    }

    // @ai help "..."
    if (rest.size() > 4 && rest.substr(0, 4) == "help") {
        string query = extract_quoted_query(rest, 4);
        if (query.empty()) {
            ai_print_error("usage: @ai help \"topic\"");
            return 1;
        }
        return handle_help(query, state);
    }

    // Default: natural language to command
    string query = extract_quoted_query(trimmed, 3);
    if (query.empty()) {
        ai_print_error("usage: @ai \"your question\"");
        return 1;
    }

    return handle_nl_to_cmd(query, state);
}

#endif // TASH_AI_ENABLED
