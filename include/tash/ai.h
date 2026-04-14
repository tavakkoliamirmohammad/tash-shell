#ifndef TASH_AI_H
#define TASH_AI_H

#ifdef TASH_AI_ENABLED

#include "tash/shell.h"
#include <string>
#include <vector>
#include <unordered_map>

// ── AI Setup ──────────────────────────────────────────────────

std::string ai_get_key_path();
std::string ai_load_key();
bool ai_save_key(const std::string &key);
bool ai_run_setup_wizard();
bool ai_validate_key(const std::string &key);

// ── AI Usage Tracking ─────────────────────────────────────────

std::string ai_get_usage_path();
int ai_get_today_usage();
void ai_increment_usage();

// ── AI Backend Selection ──────────────────────────────────────

enum AIBackend {
    AI_BACKEND_GEMINI,
    AI_BACKEND_OLLAMA,
};

AIBackend ai_get_backend();
bool ai_set_backend(AIBackend backend);
const char *ai_backend_name(AIBackend backend);

std::string ai_get_ollama_url();
std::string ai_get_ollama_model();
bool ai_set_ollama_model(const std::string &model);

// ── AI Clients ────────────────────────────────────────────────

struct GeminiResponse {
    bool success;
    std::string text;
    int http_status;
    std::string error_message;
};

GeminiResponse gemini_generate(const std::string &api_key,
                                const std::string &system_prompt,
                                const std::string &user_prompt);

GeminiResponse ollama_generate(const std::string &system_prompt,
                                const std::string &user_prompt);

// Dispatches to the currently selected backend. api_key is ignored when
// the active backend is Ollama.
GeminiResponse ai_generate(const std::string &system_prompt,
                            const std::string &user_prompt,
                            const std::string &api_key);

// ── AI Handler ────────────────────────────────────────────────

bool is_ai_command(const std::string &input);
int handle_ai_command(const std::string &input, ShellState &state);

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

#endif // TASH_AI_ENABLED
#endif // TASH_AI_H
