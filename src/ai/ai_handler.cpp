#ifdef TASH_AI_ENABLED

#include "tash/ai.h"
#include "tash/core.h"
#include "theme.h"
#include <iostream>
#include <fstream>
#include <sys/stat.h>

using namespace std;

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

// ── Ensure API key is available ───────────────────────────────

static string ensure_api_key(ShellState &state) {
    if (!state.ai_enabled) {
        ai_print_error("AI is disabled. Run @ai on to enable.");
        return "";
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

    GeminiResponse resp = gemini_generate(key, PROMPT_NL_TO_CMD, query);
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

    string choice;
    if (!getline(cin, choice) || choice.empty()) choice = "n";

    if (choice[0] == 'y' || choice[0] == 'Y') {
        write_stdout("\n");
        return execute_single_command(cmd, state);
    } else if (choice[0] == 'e' || choice[0] == 'E') {
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

    GeminiResponse resp = gemini_generate(key, PROMPT_ERROR_EXPLAIN, user_prompt);
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

    GeminiResponse resp = gemini_generate(key, PROMPT_CMD_EXPLAIN, cmd_text);
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

    GeminiResponse resp = gemini_generate(key, PROMPT_SCRIPT_GEN, query);
    ai_increment_usage();

    if (!resp.success) {
        ai_print_error(resp.error_message);
        if (resp.http_status == 403) state.ai_enabled = false;
        return 1;
    }

    ai_print_label();
    write_stdout("\n" + resp.text + "\n\n");
    write_stdout(AI_PROMPT "Save to?" CAT_RESET " [filename/n] ");

    string filename;
    if (!getline(cin, filename) || filename.empty() || filename[0] == 'n' || filename[0] == 'N') {
        write_stdout("\n");
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

    GeminiResponse resp = gemini_generate(key, PROMPT_WORKFLOW, query);
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
    // Gemini free tier known limits
    static const int DAILY_LIMIT = 1500;
    static const int RPM_LIMIT = 15;

    ai_print_label();
    write_stdout("AI Status\n\n");

    string key = ai_load_key();
    write_stdout("  Key:      " + string(key.empty() ? AI_ERROR "not configured" : AI_CMD "configured") + CAT_RESET "\n");
    write_stdout("  Status:   " + string(state.ai_enabled ? AI_CMD "enabled" : AI_ERROR "disabled") + CAT_RESET "\n");

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
        write_stdout("  @ai setup                configure API key\n");
        write_stdout("  @ai on / off             enable or disable AI\n");
        return 0;
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
