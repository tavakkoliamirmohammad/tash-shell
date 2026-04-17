#ifndef TASH_AI_H
#define TASH_AI_H

#ifdef TASH_AI_ENABLED

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

std::string ai_get_key_path();
std::optional<std::string> ai_load_key();
bool ai_save_key(const std::string &key);
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

// ── Provider Config ──────────────────────────────────────────

std::string ai_get_config_dir();
std::string ai_get_provider();
void ai_set_provider(const std::string &provider);
std::optional<std::string> ai_get_model_override();
void ai_set_model_override(const std::string &model);
std::optional<std::string> ai_load_provider_key(const std::string &provider);
bool ai_save_provider_key(const std::string &provider, const std::string &key);
std::string ai_get_ollama_url();
void ai_set_ollama_url(const std::string &url);

#endif // TASH_AI_ENABLED
#endif // TASH_AI_H
