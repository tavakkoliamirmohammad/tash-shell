#ifndef TASH_LLM_CLIENT_H
#define TASH_LLM_CLIENT_H

#ifdef TASH_AI_ENABLED

#include <string>
#include <vector>
#include <functional>
#include <memory>

struct [[nodiscard]] LLMResponse {
    bool success;
    std::string text;
    int http_status;
    std::string error_message;
};

struct ConversationTurn {
    std::string role; // "user" or "assistant"
    std::string text;
};

class LLMClient {
public:
    virtual ~LLMClient() = default;
    virtual LLMResponse generate(const std::string &system_prompt, const std::string &user_prompt) = 0;
    virtual LLMResponse generate_stream(const std::string &system_prompt, const std::string &user_prompt,
                                         std::function<void(const std::string &chunk)> on_chunk) = 0;
    virtual LLMResponse generate_with_context(const std::string &system_prompt,
                                               const std::vector<ConversationTurn> &history,
                                               const std::string &user_prompt) = 0;
    virtual LLMResponse generate_structured(const std::string &system_prompt,
                                             const std::string &user_prompt) = 0;
    virtual LLMResponse generate_structured_with_context(
        const std::string &system_prompt,
        const std::vector<ConversationTurn> &history,
        const std::string &user_prompt) = 0;
    virtual void set_model(const std::string &model) = 0;
    virtual std::string get_model() const = 0;
    virtual std::string get_provider_name() const = 0;

    static bool is_retryable(const LLMResponse &resp);
};

class GeminiClient : public LLMClient {
public:
    explicit GeminiClient(const std::string &api_key);
    LLMResponse generate(const std::string &system_prompt, const std::string &user_prompt) override;
    LLMResponse generate_stream(const std::string &system_prompt, const std::string &user_prompt,
                                 std::function<void(const std::string &chunk)> on_chunk) override;
    LLMResponse generate_with_context(const std::string &system_prompt,
                                       const std::vector<ConversationTurn> &history,
                                       const std::string &user_prompt) override;
    LLMResponse generate_structured(const std::string &system_prompt,
                                     const std::string &user_prompt) override;
    LLMResponse generate_structured_with_context(
        const std::string &system_prompt,
        const std::vector<ConversationTurn> &history,
        const std::string &user_prompt) override;
    void set_model(const std::string &model) override;
    std::string get_model() const override;
    std::string get_provider_name() const override { return "gemini"; }
private:
    std::string api_key_;
    std::string model_;
    std::vector<std::string> fallback_models_;
    int connect_timeout_;
    int read_timeout_;
    LLMResponse call_model(const std::string &model, const std::string &body);
    LLMResponse call_model_stream(const std::string &model, const std::string &body,
                                   std::function<void(const std::string &chunk)> on_chunk);
};

class OpenAIClient : public LLMClient {
public:
    explicit OpenAIClient(const std::string &api_key);
    LLMResponse generate(const std::string &system_prompt, const std::string &user_prompt) override;
    LLMResponse generate_stream(const std::string &system_prompt, const std::string &user_prompt,
                                 std::function<void(const std::string &chunk)> on_chunk) override;
    LLMResponse generate_with_context(const std::string &system_prompt,
                                       const std::vector<ConversationTurn> &history,
                                       const std::string &user_prompt) override;
    LLMResponse generate_structured(const std::string &system_prompt,
                                     const std::string &user_prompt) override;
    LLMResponse generate_structured_with_context(
        const std::string &system_prompt,
        const std::vector<ConversationTurn> &history,
        const std::string &user_prompt) override;
    void set_model(const std::string &model) override;
    std::string get_model() const override;
    std::string get_provider_name() const override { return "openai"; }
private:
    std::string api_key_;
    std::string model_;
    int connect_timeout_;
    int read_timeout_;
};

class OllamaClient : public LLMClient {
public:
    explicit OllamaClient(const std::string &endpoint_url);
    LLMResponse generate(const std::string &system_prompt, const std::string &user_prompt) override;
    LLMResponse generate_stream(const std::string &system_prompt, const std::string &user_prompt,
                                 std::function<void(const std::string &chunk)> on_chunk) override;
    LLMResponse generate_with_context(const std::string &system_prompt,
                                       const std::vector<ConversationTurn> &history,
                                       const std::string &user_prompt) override;
    LLMResponse generate_structured(const std::string &system_prompt,
                                     const std::string &user_prompt) override;
    LLMResponse generate_structured_with_context(
        const std::string &system_prompt,
        const std::vector<ConversationTurn> &history,
        const std::string &user_prompt) override;
    void set_model(const std::string &model) override;
    std::string get_model() const override;
    std::string get_provider_name() const override { return "ollama"; }
private:
    std::string host_;
    int port_;
    std::string model_;
    int connect_timeout_;
    int read_timeout_;
};

std::unique_ptr<LLMClient> create_llm_client(const std::string &provider,
                                               const std::string &gemini_key,
                                               const std::string &openai_key,
                                               const std::string &ollama_url);

// JSON helpers exposed for testing
std::string build_gemini_request_json(const std::string &system_prompt, const std::string &user_prompt);
std::string build_gemini_context_json(const std::string &system_prompt,
                                       const std::vector<ConversationTurn> &history,
                                       const std::string &user_prompt);
std::string extract_gemini_text(const std::string &json_body);
std::string extract_gemini_error(const std::string &json_body);

std::string build_openai_request_json(const std::string &model, const std::string &system_prompt,
                                       const std::string &user_prompt, bool stream);
std::string build_openai_context_json(const std::string &model, const std::string &system_prompt,
                                       const std::vector<ConversationTurn> &history,
                                       const std::string &user_prompt, bool stream);
std::string extract_openai_text(const std::string &json_body);
std::string extract_openai_error(const std::string &json_body);

std::string build_ollama_request_json(const std::string &model, const std::string &system_prompt,
                                       const std::string &user_prompt, bool stream);
std::string build_ollama_context_json(const std::string &model, const std::string &system_prompt,
                                       const std::vector<ConversationTurn> &history,
                                       const std::string &user_prompt, bool stream);
std::string extract_ollama_text(const std::string &json_body);

// Structured output JSON builders (exposed for testing)
std::string build_gemini_structured_json(const std::string &system_prompt,
                                          const std::string &user_prompt);
std::string build_gemini_structured_context_json(const std::string &system_prompt,
                                                  const std::vector<ConversationTurn> &history,
                                                  const std::string &user_prompt);
std::string build_openai_structured_json(const std::string &model,
                                          const std::string &system_prompt,
                                          const std::string &user_prompt);
std::string build_openai_structured_context_json(const std::string &model,
                                                  const std::string &system_prompt,
                                                  const std::vector<ConversationTurn> &history,
                                                  const std::string &user_prompt);
std::string build_ollama_structured_json(const std::string &model,
                                          const std::string &system_prompt,
                                          const std::string &user_prompt);
std::string build_ollama_structured_context_json(const std::string &model,
                                                  const std::string &system_prompt,
                                                  const std::vector<ConversationTurn> &history,
                                                  const std::string &user_prompt);

#endif
#endif
