#ifdef TASH_AI_ENABLED

#include "tash/plugins/ai_error_hook_provider.h"
#include "tash/core.h"
#include "theme.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <termios.h>
#include <unistd.h>

using json = nlohmann::json;
using namespace std;

// ── System prompt for error recovery ─────────────────────────

static const char *ERROR_SYSTEM_PROMPT =
    "You are a shell error explainer. Given a failed command and stderr, "
    "respond with JSON: {\"explanation\":\"one sentence\",\"fix\":\"exact "
    "command or empty string\"}. Be concise.";

// ── Read a single character without waiting for Enter ─────────

static char error_hook_read_char() {
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

// ── Constructor ───────────────────────────────────────────────

AiErrorHookProvider::AiErrorHookProvider(LLMClient *client)
    : client_(client)
    , last_call_time_(0)
    , call_count_(0) {}

// ── IHookProvider interface ───────────────────────────────────

string AiErrorHookProvider::name() const {
    return "ai-error-recovery";
}

void AiErrorHookProvider::on_before_command(
    const string &/*command*/, ShellState &/*state*/) {
    // No-op
}

void AiErrorHookProvider::on_after_command(
    const string &command, int exit_code,
    const string &stderr_output, ShellState &state) {

    if (!should_trigger(exit_code, stderr_output, state)) {
        return;
    }

    if (!rate_limit_allows()) {
        return;
    }

    // Update rate limiter
    last_call_time_ = time(nullptr);
    call_count_++;

    // Build context and query LLM
    string context = build_context_json(command, exit_code, stderr_output, state);
    LLMResponse resp = client_->generate(system_prompt(), context);

    if (!resp.success) {
        return;
    }

    // Parse the response
    ErrorRecoveryResponse recovery = parse_response(resp.text);
    if (!recovery.valid) {
        return;
    }

    // Display explanation in yellow
    write_stdout("\n" + CAT_YELLOW + "  " + recovery.explanation + CAT_RESET "\n");

    // Display fix command in green if present
    if (!recovery.fix.empty()) {
        write_stdout(CAT_GREEN + "  $ " + recovery.fix + CAT_RESET "\n\n");
        write_stdout(CAT_DIM "  [Enter] run fix  [Esc] dismiss" CAT_RESET "\n");

        if (!isatty(STDIN_FILENO)) {
            return;
        }

        char ch = error_hook_read_char();
        if (ch == '\n' || ch == '\r') {
            write_stdout("\n");
            execute_single_command(recovery.fix, state);
        } else {
            write_stdout("\n");
        }
    }
}

// ── Trigger condition check ───────────────────────────────────

bool AiErrorHookProvider::should_trigger(
    int exit_code,
    const string &stderr_output,
    const ShellState &state) const {

    if (exit_code == 0) return false;
    if (exit_code == 130) return false;   // Ctrl+C
    if (exit_code == 127) return false;   // command not found
    if (stderr_output.empty()) return false;
    if (!state.ai_enabled) return false;

    return true;
}

// ── Rate limiter ──────────────────────────────────────────────

bool AiErrorHookProvider::rate_limit_allows() const {
    if (last_call_time_ == 0) return true;
    time_t now = time(nullptr);
    return (now - last_call_time_) >= COOLDOWN_SECONDS;
}

// ── Build context JSON ────────────────────────────────────────

string AiErrorHookProvider::build_context_json(
    const string &command, int exit_code,
    const string &stderr_output,
    const ShellState &state) const {

    json ctx;
    ctx["command"] = command;
    ctx["exit_code"] = exit_code;
    ctx["stderr"] = stderr_output;

    // Current directory
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd))) {
        ctx["directory"] = string(cwd);
    } else {
        ctx["directory"] = "";
    }

    // Recent commands: last 3 from state
    // ShellState doesn't have a command history vector, so we reconstruct
    // from what's available: last_command_text and last_executed_cmd
    json recent = json::array();
    if (!state.last_executed_cmd.empty() &&
        state.last_executed_cmd != command) {
        recent.push_back(state.last_executed_cmd);
    }
    if (!state.last_command_text.empty() &&
        state.last_command_text != command &&
        state.last_command_text != state.last_executed_cmd) {
        recent.push_back(state.last_command_text);
    }
    ctx["recent_commands"] = recent;

    return ctx.dump();
}

// ── Parse LLM response ───────────────────────────────────────

ErrorRecoveryResponse AiErrorHookProvider::parse_response(
    const string &raw) {

    ErrorRecoveryResponse result;

    try {
        json j = json::parse(raw);

        if (j.count("explanation") && j["explanation"].is_string()) {
            result.explanation = j["explanation"].get<string>();
        }
        if (j.count("fix") && j["fix"].is_string()) {
            result.fix = j["fix"].get<string>();
        }

        // Valid if we at least got an explanation
        result.valid = !result.explanation.empty();

    } catch (const json::exception &) {
        result.valid = false;
    }

    return result;
}

// ── System prompt accessor ────────────────────────────────────

const char *AiErrorHookProvider::system_prompt() {
    return ERROR_SYSTEM_PROMPT;
}

#endif // TASH_AI_ENABLED
