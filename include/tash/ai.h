#ifndef TASH_AI_H
#define TASH_AI_H


#include "tash/llm_client.h"
#include "tash/shell.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>
#include <ctime>

// Build an LLMClient from the user's current AI configuration (provider,
// model, API key). Returns nullptr when the config is incomplete or the
// selected provider isn't supported. Used by AiErrorHookProvider for
// lazy activation.
std::unique_ptr<LLMClient> ai_create_client();

// ── AI Setup ──────────────────────────────────────────────────

bool ai_run_setup_wizard();
bool ai_validate_key(const std::string &key);

// ── AI Usage Tracking ─────────────────────────────────────────

std::string ai_get_usage_path();
int ai_get_today_usage();
void ai_increment_usage();

// ── AI Response Parsing ──────────────────────────────────────

enum ResponseType { RESP_COMMAND, RESP_SCRIPT, RESP_STEPS, RESP_ANSWER };

struct StepInfo {
    std::string description;
    std::string command;
};

struct ParsedResponse {
    ResponseType type;
    std::string content;
    std::string script_filename;
    std::vector<StepInfo> steps; // for RESP_STEPS
};

ParsedResponse parse_ai_response(const std::string &raw);

// ── AI Handler ────────────────────────────────────────────────

bool is_ai_command(const std::string &input);
int handle_ai_command(const std::string &input, ShellState &state, std::string *prefill_cmd = nullptr);

// ── Context-Aware Suggestions ─────────────────────────────────

struct TransitionMap {
    // key: command prefix (first word or first two words)
    // value: map of successor command → count
    std::unordered_map<std::string, std::unordered_map<std::string, int>> transitions;
};

void build_transition_map(const std::string &history_path, TransitionMap &tmap);
std::string context_suggest(const std::string &last_command, const TransitionMap &tmap);

// ── Global transition map ─────────────────────────────────────

TransitionMap& get_transition_map();

// ── Rate Limiter ─────────────────────────────────────────────

class AiRateLimiter {
public:
    AiRateLimiter(int max_requests, int window_seconds);
    bool allow();
private:
    int max_requests_;
    int window_seconds_;
    std::vector<time_t> timestamps_;
};

// Process-wide rate limiter shared between @ai and the auto-error-recovery
// hook so they cannot together exceed the provider's RPM quota. Configured
// once at first access (default: 10 requests / 60s, matching Gemini free
// tier).
//
// NOT thread-safe. `allow()` mutates the internal timestamp vector
// without locking; every current caller is on the main REPL thread, so
// the race is latent. If a future caller fires the limiter from a
// background thread, add a std::mutex around the vector access.
AiRateLimiter& global_ai_rate_limiter();

// ── Provider Config ──────────────────────────────────────────

std::string ai_get_config_dir();
std::string ai_get_provider();
void ai_set_provider(const std::string &provider);
std::optional<std::string> ai_get_model_override();
void ai_set_model_override(const std::string &model);

// Result of a key-file read. Callers used to see `nullopt` for both
// "never configured" and "file unreadable" — distinct statuses let
// @ai status diagnose which one the user is actually hitting.
enum class KeyStatus {
    Ok,          // file present, non-empty content returned
    Absent,      // no file at the expected path
    Unreadable,  // file present but could not be opened/read
    Empty,       // file present but empty after trim
};
struct KeyLoadResult {
    KeyStatus status;
    std::string value;           // populated iff status == Ok
    std::string diagnostic;      // populated on Unreadable (errno text)
};
KeyLoadResult ai_load_provider_key_ex(const std::string &provider);

std::optional<std::string> ai_load_provider_key(const std::string &provider);
bool ai_save_provider_key(const std::string &provider, const std::string &key);
std::string ai_get_ollama_url();
void ai_set_ollama_url(const std::string &url);

// ── Privacy ──────────────────────────────────────────────────
//
// When true, the shell includes captured stderr in AI prompts for
// "explain/fix/error" queries and in the auto-error-recovery hook
// context JSON. When false, those paths send only the command and
// exit code. Default: true (opt-out).
bool ai_get_send_stderr();
void ai_set_send_stderr(bool on);

// Returns true once on first run so the handler can print a one-time
// banner explaining the privacy implications of AI features. After
// the first call returns true, subsequent calls return false.
bool ai_privacy_banner_pending();
void ai_privacy_banner_mark_shown();

// ── Conversation persistence ─────────────────────────────────
//
// The @ai subsystem keeps the last N turns as context. Pre-#PR the
// vector was in-process only, so exiting the shell wiped the memory
// and the next session started fresh — surprising users who expected
// "@ai now delete those files" to work after reopening the terminal.
// These helpers persist the turn list to $TASH_DATA_HOME/ai/conversation.json
// so context survives a Ctrl+D.
std::string ai_get_conversation_path();
std::vector<ConversationTurn> ai_load_conversation();
void ai_save_conversation(const std::vector<ConversationTurn> &turns);
void ai_clear_conversation_file();

#endif // TASH_AI_H
