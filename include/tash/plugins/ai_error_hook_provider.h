#ifndef TASH_AI_ERROR_HOOK_PROVIDER_H
#define TASH_AI_ERROR_HOOK_PROVIDER_H

#ifdef TASH_AI_ENABLED

#include "tash/plugin.h"
#include "tash/llm_client.h"
#include <string>
#include <ctime>

// ── Parsed error recovery response ────────────────────────────

struct ErrorRecoveryResponse {
    std::string explanation;
    std::string fix;
    bool valid;

    ErrorRecoveryResponse() : valid(false) {}
};

// ── AI Error Hook Provider ────────────────────────────────────
//
// Watches for failed commands and offers AI-powered explanations
// and fix suggestions. Integrates with the plugin hook system.

class AiErrorHookProvider : public IHookProvider {
public:
    explicit AiErrorHookProvider(LLMClient *client);

    std::string name() const override;

    void on_before_command(const std::string &command,
                           ShellState &state) override;

    void on_after_command(const std::string &command,
                          int exit_code,
                          const std::string &stderr_output,
                          ShellState &state) override;

    // Exposed for testing -- check if trigger conditions are met
    bool should_trigger(int exit_code,
                        const std::string &stderr_output,
                        const ShellState &state) const;

    // Exposed for testing -- build the context JSON sent to the LLM
    std::string build_context_json(const std::string &command,
                                   int exit_code,
                                   const std::string &stderr_output,
                                   const ShellState &state) const;

    // Exposed for testing -- parse the LLM response JSON
    static ErrorRecoveryResponse parse_response(const std::string &raw);

    // Exposed for testing -- get the system prompt
    static const char *system_prompt();

    // Exposed for testing -- number of LLM calls made
    int call_count() const { return call_count_; }

    // Exposed for testing -- reset the rate limiter cooldown
    void reset_cooldown() { last_call_time_ = 0; }

private:
    LLMClient *client_;
    time_t last_call_time_;
    int call_count_;

    static const int COOLDOWN_SECONDS = 5;

    bool rate_limit_allows() const;
};

#endif // TASH_AI_ENABLED
#endif // TASH_AI_ERROR_HOOK_PROVIDER_H
