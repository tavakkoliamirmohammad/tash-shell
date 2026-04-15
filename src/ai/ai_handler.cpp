#ifdef TASH_AI_ENABLED

#include "tash/ai.h"
#include "tash/core.h"
#include "theme.h"
#include <iostream>
#include <fstream>
#include <sys/stat.h>
#include <termios.h>
#include <memory>

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

static AiRateLimiter rate_limiter(15, 60);

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

// ── LLM client helpers ───────────────────────────────────────

static unique_ptr<LLMClient> create_current_client() {
    string provider = ai_get_provider();
    string gemini_key = ai_load_provider_key("gemini");
    string openai_key = ai_load_provider_key("openai");
    string ollama_url = ai_get_ollama_url();

    unique_ptr<LLMClient> client = create_llm_client(provider, gemini_key, openai_key, ollama_url);
    if (!client) return client;

    string model_override = ai_get_model_override();
    if (!model_override.empty()) {
        client->set_model(model_override);
    }
    return client;
}

static unique_ptr<LLMClient> ensure_client(ShellState &state) {
    if (!state.ai_enabled) {
        ai_print_error("AI is disabled. Run @ai on to enable.");
        return unique_ptr<LLMClient>();
    }

    string provider = ai_get_provider();

    // Ollama doesn't need a key
    if (provider == "ollama") {
        return create_current_client();
    }

    string key = ai_load_provider_key(provider);
    if (key.empty()) {
        if (!ai_run_setup_wizard()) return unique_ptr<LLMClient>();
        key = ai_load_provider_key(provider);
        if (key.empty()) return unique_ptr<LLMClient>();
    }
    return create_current_client();
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

static int handle_nl_to_cmd(const string &query, ShellState &state, string *prefill_cmd) {
    if (!rate_limiter.allow()) {
        ai_print_error("rate limit exceeded. Please wait a moment.");
        return 1;
    }

    unique_ptr<LLMClient> client = ensure_client(state);
    if (!client) return 1;

    string system_prompt = build_system_context() + PROMPT_NL_TO_CMD;

    LLMResponse resp;
    if (!conversation_history.empty()) {
        resp = client->generate_with_context(system_prompt, conversation_history, query);
    } else {
        resp = client->generate(system_prompt, query);
    }

    if (!resp.success) {
        ai_print_error(resp.error_message);
        if (resp.http_status == 403) state.ai_enabled = false;
        return 1;
    }

    ai_increment_usage();

    if (resp.success) {
        add_to_conversation("user", query);
        add_to_conversation("assistant", resp.text);
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
        if (prefill_cmd) *prefill_cmd = cmd;
        write_stdout("\n");
        return 0;
    }

    write_stdout("\n");
    return 0;
}

// ── Feature: Error explanation ────────────────────────────────

static int handle_explain_error(ShellState &state) {
    if (state.last_command_text.empty() || state.last_exit_status == 0) {
        ai_print_error("no recent errors to explain.");
        return 1;
    }

    if (!rate_limiter.allow()) {
        ai_print_error("rate limit exceeded. Please wait a moment.");
        return 1;
    }

    unique_ptr<LLMClient> client = ensure_client(state);
    if (!client) return 1;

    string system_prompt = build_system_context() + PROMPT_ERROR_EXPLAIN;

    string user_prompt = "Command: " + state.last_command_text +
                         "\nExit code: " + to_string(state.last_exit_status);
    if (!state.last_stderr_output.empty()) {
        user_prompt += "\nError output: " + state.last_stderr_output;
    }

    ai_print_label();
    write_stdout(AI_ERROR + state.last_command_text + CAT_RESET
                 " exited with " + to_string(state.last_exit_status) + "\n\n");

    auto on_chunk = [](const string &chunk) {
        write_stdout(chunk);
    };
    LLMResponse resp = client->generate_stream(system_prompt, user_prompt, on_chunk);

    if (!resp.success) {
        ai_print_error(resp.error_message);
        if (resp.http_status == 403) state.ai_enabled = false;
        return 1;
    }

    ai_increment_usage();

    // Stream already printed the response text via on_chunk
    write_stdout("\n");
    return 0;
}

// ── Feature: Command explanation ──────────────────────────────

static int handle_explain_cmd(const string &cmd_text, ShellState &state) {
    if (!rate_limiter.allow()) {
        ai_print_error("rate limit exceeded. Please wait a moment.");
        return 1;
    }

    unique_ptr<LLMClient> client = ensure_client(state);
    if (!client) return 1;

    string system_prompt = build_system_context() + PROMPT_CMD_EXPLAIN;

    ai_print_label();
    write_stdout(AI_CMD + cmd_text + CAT_RESET "\n\n");

    auto on_chunk = [](const string &chunk) {
        write_stdout(chunk);
    };
    LLMResponse resp = client->generate_stream(system_prompt, cmd_text, on_chunk);

    if (!resp.success) {
        ai_print_error(resp.error_message);
        if (resp.http_status == 403) state.ai_enabled = false;
        return 1;
    }

    ai_increment_usage();

    // Stream already printed the response text via on_chunk
    write_stdout("\n");
    return 0;
}

// ── Feature: Script generation ────────────────────────────────

static int handle_script(const string &query, ShellState &state) {
    if (!rate_limiter.allow()) {
        ai_print_error("rate limit exceeded. Please wait a moment.");
        return 1;
    }

    unique_ptr<LLMClient> client = ensure_client(state);
    if (!client) return 1;

    string system_prompt = build_system_context() + PROMPT_SCRIPT_GEN;

    LLMResponse resp = client->generate(system_prompt, query);

    if (!resp.success) {
        ai_print_error(resp.error_message);
        if (resp.http_status == 403) state.ai_enabled = false;
        return 1;
    }

    ai_increment_usage();

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

    if (filename.find("..") != string::npos) {
        ai_print_error("invalid path: '..' not allowed in filename.");
        return 1;
    }

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
    if (!rate_limiter.allow()) {
        ai_print_error("rate limit exceeded. Please wait a moment.");
        return 1;
    }

    unique_ptr<LLMClient> client = ensure_client(state);
    if (!client) return 1;

    string system_prompt = build_system_context() + PROMPT_WORKFLOW;

    ai_print_label();
    write_stdout("\n");

    auto on_chunk = [](const string &chunk) {
        write_stdout(chunk);
    };
    LLMResponse resp = client->generate_stream(system_prompt, query, on_chunk);

    if (!resp.success) {
        ai_print_error(resp.error_message);
        if (resp.http_status == 403) state.ai_enabled = false;
        return 1;
    }

    ai_increment_usage();

    // Stream already printed the response text via on_chunk
    write_stdout("\n");
    return 0;
}

// ── Feature: Status ───────────────────────────────────────────

static int handle_status(ShellState &state) {
    static const int DAILY_LIMIT = 500;
    static const int RPM_LIMIT = 15;

    string provider = ai_get_provider();
    string model_override = ai_get_model_override();
    unique_ptr<LLMClient> client = create_current_client();
    string current_model = client ? client->get_model() : "unknown";
    if (!model_override.empty()) current_model = model_override;

    ai_print_label();
    write_stdout("AI Status\n\n");

    string key = ai_load_provider_key(provider);
    bool key_ok = (provider == "ollama") || !key.empty();

    write_stdout("  Provider: " AI_CMD + provider + CAT_RESET "\n");
    write_stdout("  Model:    " AI_CMD + current_model + CAT_RESET "\n");
    write_stdout("  Key:      " + string(key_ok ? AI_CMD "configured" : AI_ERROR "not configured") + CAT_RESET "\n");
    write_stdout("  Status:   " + string(state.ai_enabled ? AI_CMD "enabled" : AI_ERROR "disabled") + CAT_RESET "\n");

    int usage = ai_get_today_usage();
    int remaining = DAILY_LIMIT - usage;
    if (remaining < 0) remaining = 0;

    string usage_color = (remaining > 100) ? AI_CMD : (remaining > 0) ? CAT_YELLOW : AI_ERROR;
    write_stdout("  Today:    " + usage_color + to_string(usage) + " / " + to_string(DAILY_LIMIT) +
                 " requests" CAT_RESET CAT_DIM " (" + to_string(remaining) + " remaining)" CAT_RESET "\n");
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
        write_stdout("Usage:\n");
        write_stdout("  @ai \"question\"           generate a shell command\n");
        write_stdout("  @ai explain              explain last failed command\n");
        write_stdout("  @ai what does <cmd>      explain a command\n");
        write_stdout("  @ai script \"task\"         generate a bash script\n");
        write_stdout("  @ai help \"topic\"          step-by-step guidance\n");
        write_stdout("  @ai status               show AI status\n");
        write_stdout("  @ai config               interactive configuration\n");
        write_stdout("  @ai provider <name>      switch provider (gemini/openai/ollama)\n");
        write_stdout("  @ai model <name>         set model\n");
        write_stdout("  @ai test                 test API connection\n");
        write_stdout("  @ai clear                clear conversation history\n");
        write_stdout("  @ai setup                configure API key\n");
        write_stdout("  @ai on / off             enable or disable AI\n");
        return 0;
    }

    // ── 1. setup, config ──────────────────────────────────────

    if (rest == "setup") {
        ai_run_setup_wizard();
        return 0;
    }

    if (rest == "config") {
        string provider = ai_get_provider();
        string model = ai_get_model_override();
        unique_ptr<LLMClient> client = create_current_client();
        string current_model = client ? client->get_model() : "unknown";
        if (!model.empty()) current_model = model;

        ai_print_label();
        write_stdout("Configuration\n\n");
        write_stdout("  Current: " AI_CMD + provider + CAT_RESET " / " AI_CMD + current_model + CAT_RESET "\n\n");
        write_stdout(AI_STEP_NUM "  1." CAT_RESET " Switch provider (gemini/openai/ollama)\n");
        write_stdout(AI_STEP_NUM "  2." CAT_RESET " Change model\n");
        write_stdout(AI_STEP_NUM "  3." CAT_RESET " Set API key\n");
        write_stdout(AI_STEP_NUM "  4." CAT_RESET " Set Ollama URL\n");
        write_stdout(AI_STEP_NUM "  5." CAT_RESET " Test connection\n\n");
        write_stdout(AI_PROMPT "  Choice" CAT_RESET " [1-5]: ");

        char ch = read_single_char();
        write_stdout(string(1, ch) + "\n\n");

        if (ch == '1') {
            write_stdout("  Provider (gemini/openai/ollama): ");
            string p;
            if (getline(cin, p)) {
                while (!p.empty() && p.back() == ' ') p.pop_back();
                while (!p.empty() && p.front() == ' ') p.erase(p.begin());
                if (p == "gemini" || p == "openai" || p == "ollama") {
                    ai_set_provider(p);
                    ai_set_model_override("");
                    ai_print_label();
                    write_stdout(AI_CMD "Provider set to " + p + "." CAT_RESET "\n");
                } else {
                    ai_print_error("unknown provider.");
                }
            }
        } else if (ch == '2') {
            write_stdout("  Model name: ");
            string m;
            if (getline(cin, m)) {
                while (!m.empty() && m.back() == ' ') m.pop_back();
                while (!m.empty() && m.front() == ' ') m.erase(m.begin());
                if (!m.empty()) {
                    ai_set_model_override(m);
                    ai_print_label();
                    write_stdout(AI_CMD "Model set to " + m + "." CAT_RESET "\n");
                }
            }
        } else if (ch == '3') {
            ai_run_setup_wizard();
        } else if (ch == '4') {
            write_stdout("  Ollama URL [http://localhost:11434]: ");
            string url;
            if (getline(cin, url)) {
                while (!url.empty() && url.back() == ' ') url.pop_back();
                while (!url.empty() && url.front() == ' ') url.erase(url.begin());
                if (url.empty()) url = "http://localhost:11434";
                ai_set_ollama_url(url);
                ai_print_label();
                write_stdout(AI_CMD "Ollama URL set to " + url + "." CAT_RESET "\n");
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
            LLMResponse test_resp = test_client->generate("Reply with exactly: ok", "test");
            if (test_resp.success) {
                ai_print_label();
                write_stdout(AI_CMD "Connection successful!" CAT_RESET "\n");
            } else {
                ai_print_error("connection failed: " + test_resp.error_message);
            }
        }

        return 0;
    }

    // ── 2. on, off ────────────────────────────────────────────

    if (rest == "on") {
        state.ai_enabled = true;
        ai_print_label();
        write_stdout(AI_CMD "AI enabled." CAT_RESET "\n");
        return 0;
    }

    if (rest == "off") {
        state.ai_enabled = false;
        ai_print_label();
        write_stdout(CAT_YELLOW "AI disabled." CAT_RESET "\n");
        return 0;
    }

    // ── 3. status, test, clear ────────────────────────────────

    if (rest == "status") {
        return handle_status(state);
    }

    if (rest == "test") {
        if (!rate_limiter.allow()) {
            ai_print_error("rate limit exceeded. Please wait a moment.");
            return 1;
        }
        unique_ptr<LLMClient> client = create_current_client();
        if (!client) {
            ai_print_error("no API key configured. Run @ai setup or @ai config.");
            return 1;
        }
        ai_print_label();
        write_stdout("testing connection...\n");
        LLMResponse resp = client->generate("Reply with exactly: ok", "test");
        if (resp.success) {
            ai_print_label();
            write_stdout(AI_CMD "Connection successful!" CAT_RESET "\n");
            return 0;
        }
        ai_print_error("test failed: " + resp.error_message);
        return 1;
    }

    if (rest == "clear") {
        conversation_history.clear();
        ai_print_label();
        write_stdout(AI_CMD "Conversation cleared." CAT_RESET "\n");
        return 0;
    }

    // ── 4. provider, model ────────────────────────────────────

    if (rest.size() > 9 && rest.substr(0, 9) == "provider ") {
        string provider = rest.substr(9);
        // trim
        while (!provider.empty() && provider.front() == ' ') provider.erase(provider.begin());
        while (!provider.empty() && provider.back() == ' ') provider.pop_back();

        if (provider != "gemini" && provider != "openai" && provider != "ollama") {
            ai_print_error("unknown provider. Use: gemini, openai, or ollama");
            return 1;
        }
        ai_set_provider(provider);
        ai_set_model_override(""); // reset model on provider change
        ai_print_label();
        write_stdout(AI_CMD "Provider set to " + provider + "." CAT_RESET "\n");
        return 0;
    }

    if (rest.size() > 6 && rest.substr(0, 6) == "model ") {
        string model = rest.substr(6);
        // trim
        while (!model.empty() && model.front() == ' ') model.erase(model.begin());
        while (!model.empty() && model.back() == ' ') model.pop_back();

        if (model.empty()) {
            ai_print_error("usage: @ai model <model-name>");
            return 1;
        }
        ai_set_model_override(model);
        ai_print_label();
        write_stdout(AI_CMD "Model set to " + model + "." CAT_RESET "\n");
        return 0;
    }

    // ── 5. explain ────────────────────────────────────────────

    if (rest == "explain") {
        return handle_explain_error(state);
    }

    // ── 6. what does, what ────────────────────────────────────

    if (rest.size() > 9 && rest.substr(0, 9) == "what does") {
        string cmd_text = rest.substr(9);
        while (!cmd_text.empty() && cmd_text.front() == ' ') cmd_text.erase(cmd_text.begin());
        if (cmd_text.empty()) {
            ai_print_error("usage: @ai what does <command>");
            return 1;
        }
        return handle_explain_cmd(cmd_text, state);
    }

    if (rest.size() > 5 && rest.substr(0, 5) == "what ") {
        string cmd_text = rest.substr(5);
        while (!cmd_text.empty() && cmd_text.front() == ' ') cmd_text.erase(cmd_text.begin());
        if (cmd_text.empty()) {
            ai_print_error("usage: @ai what <command>");
            return 1;
        }
        return handle_explain_cmd(cmd_text, state);
    }

    // ── 7. script ─────────────────────────────────────────────

    if (rest.size() > 6 && rest.substr(0, 6) == "script") {
        string query = extract_quoted_query(rest, 6);
        if (query.empty()) {
            ai_print_error("usage: @ai script \"task description\"");
            return 1;
        }
        return handle_script(query, state);
    }

    // ── 8. help ───────────────────────────────────────────────

    if (rest.size() > 4 && rest.substr(0, 4) == "help") {
        string query = extract_quoted_query(rest, 4);
        if (query.empty()) {
            ai_print_error("usage: @ai help \"topic\"");
            return 1;
        }
        return handle_help(query, state);
    }

    // ── 9. Default: natural language to command ───────────────

    string query = extract_quoted_query(trimmed, 3);
    if (query.empty()) {
        ai_print_error("usage: @ai \"your question\"");
        return 1;
    }

    return handle_nl_to_cmd(query, state, prefill_cmd);
}

#endif // TASH_AI_ENABLED
