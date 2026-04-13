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

// ── Gemini Client ─────────────────────────────────────────────

struct GeminiResponse {
    bool success;
    std::string text;
    int http_status;
    std::string error_message;
};

GeminiResponse gemini_generate(const std::string &api_key,
                                const std::string &system_prompt,
                                const std::string &user_prompt);

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
