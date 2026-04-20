#ifndef TASH_LLM_CLIENT_H
#define TASH_LLM_CLIENT_H


#include <string>
#include <vector>
#include <functional>
#include <memory>

// Transport-layer failure classification. TransportOk means the request
// reached the server (http_status is authoritative); anything else means
// the failure happened below HTTP. Callers use this to produce distinct,
// actionable messages instead of "couldn't reach API".
enum class TransportStatus {
    Ok,
    Aborted,         // user Ctrl+C
    Timeout,         // connect or read timeout
    DnsFailure,
    ConnectFailed,
    TlsFailure,
    Overflow,        // response size cap hit
    Other,
};

struct [[nodiscard]] LLMResponse {
    bool success;
    std::string text;
    int http_status;
    std::string error_message;
    // Populated only on !success. TransportStatus::Ok means "server
    // responded but status wasn't 2xx" (map_*_error owns the message).
    TransportStatus transport = TransportStatus::Ok;
};

struct ConversationTurn {
    std::string role; // "user" or "assistant"
    std::string text;
};

class LLMClient {
public:
    virtual ~LLMClient() = default;
    [[nodiscard]] virtual LLMResponse generate(const std::string &system_prompt, const std::string &user_prompt) = 0;
    [[nodiscard]] virtual LLMResponse generate_stream(const std::string &system_prompt, const std::string &user_prompt,
                                         std::function<void(const std::string &chunk)> on_chunk) = 0;
    [[nodiscard]] virtual LLMResponse generate_with_context(const std::string &system_prompt,
                                               const std::vector<ConversationTurn> &history,
                                               const std::string &user_prompt) = 0;
    [[nodiscard]] virtual LLMResponse generate_structured(const std::string &system_prompt,
                                             const std::string &user_prompt) = 0;
    [[nodiscard]] virtual LLMResponse generate_structured_with_context(
        const std::string &system_prompt,
        const std::vector<ConversationTurn> &history,
        const std::string &user_prompt) = 0;

    // Structured output + server-side streaming. The API-level schema (not
    // the prompt) forces the model to produce the tash response envelope
    // {response_type, content, filename?, steps?}. Chunks arrive as raw
    // provider SSE/NDJSON bytes accumulated as the JSON token stream; the
    // `on_chunk` callback receives the extracted text delta for the
    // provider (partial JSON), and the handler is responsible for
    // incrementally peeling `content` out of that JSON for live display.
    // Using the schema instead of a prompt-level tag protocol means even a
    // poorly-instructed model cannot emit code fences, preambles, or
    // malformed output — the server rejects it before we see it.
    [[nodiscard]] virtual LLMResponse generate_structured_stream(
        const std::string &system_prompt,
        const std::string &user_prompt,
        std::function<void(const std::string &chunk)> on_chunk) = 0;
    [[nodiscard]] virtual LLMResponse generate_structured_stream_with_context(
        const std::string &system_prompt,
        const std::vector<ConversationTurn> &history,
        const std::string &user_prompt,
        std::function<void(const std::string &chunk)> on_chunk) = 0;

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
    LLMResponse generate_structured_stream(
        const std::string &system_prompt,
        const std::string &user_prompt,
        std::function<void(const std::string &chunk)> on_chunk) override;
    LLMResponse generate_structured_stream_with_context(
        const std::string &system_prompt,
        const std::vector<ConversationTurn> &history,
        const std::string &user_prompt,
        std::function<void(const std::string &chunk)> on_chunk) override;
    void set_model(const std::string &model) override;
    std::string get_model() const override;
    std::string get_provider_name() const override { return "gemini"; }
private:
    std::string api_key_;
    std::string model_;
    std::vector<std::string> fallback_models_;
    int connect_timeout_;
    int read_timeout_;
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
    LLMResponse generate_structured_stream(
        const std::string &system_prompt,
        const std::string &user_prompt,
        std::function<void(const std::string &chunk)> on_chunk) override;
    LLMResponse generate_structured_stream_with_context(
        const std::string &system_prompt,
        const std::vector<ConversationTurn> &history,
        const std::string &user_prompt,
        std::function<void(const std::string &chunk)> on_chunk) override;
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
    LLMResponse generate_structured_stream(
        const std::string &system_prompt,
        const std::string &user_prompt,
        std::function<void(const std::string &chunk)> on_chunk) override;
    LLMResponse generate_structured_stream_with_context(
        const std::string &system_prompt,
        const std::vector<ConversationTurn> &history,
        const std::string &user_prompt,
        std::function<void(const std::string &chunk)> on_chunk) override;
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

// Structured output JSON builders (exposed for testing). The `stream`
// flag toggles provider-level SSE/NDJSON streaming for OpenAI/Ollama;
// Gemini uses a distinct URL for streaming so its body is unchanged.
std::string build_gemini_structured_json(const std::string &system_prompt,
                                          const std::string &user_prompt);
std::string build_gemini_structured_context_json(const std::string &system_prompt,
                                                  const std::vector<ConversationTurn> &history,
                                                  const std::string &user_prompt);
std::string build_openai_structured_json(const std::string &model,
                                          const std::string &system_prompt,
                                          const std::string &user_prompt,
                                          bool stream = false);
std::string build_openai_structured_context_json(const std::string &model,
                                                  const std::string &system_prompt,
                                                  const std::vector<ConversationTurn> &history,
                                                  const std::string &user_prompt,
                                                  bool stream = false);
std::string build_ollama_structured_json(const std::string &model,
                                          const std::string &system_prompt,
                                          const std::string &user_prompt,
                                          bool stream = false);
std::string build_ollama_structured_context_json(const std::string &model,
                                                  const std::string &system_prompt,
                                                  const std::vector<ConversationTurn> &history,
                                                  const std::string &user_prompt,
                                                  bool stream = false);

// ── Incremental JSON content-field extractor ──────────────────
//
// LLM streaming APIs deliver the structured response one token at a time
// — which means the JSON `content` string's bytes trickle in piece by
// piece. This class feeds streamed deltas in and emits each decoded
// character of the top-level `content` value as soon as it arrives,
// letting the handler render the model's output progressively.
//
// Implementation: a light JSON-aware state machine. It tracks string/
// object nesting well enough to recognise the `"content"` key in the
// root object, then decodes escaped characters (\n, \t, \", \\, \uXXXX)
// as they arrive and forwards each decoded byte to a callback. Ignores
// `content` keys inside nested objects (only the root-level one counts),
// so `steps[].command` / `steps[].description` don't false-trigger.
class JsonContentStreamer {
public:
    using Emit = std::function<void(const std::string &piece)>;
    explicit JsonContentStreamer(Emit emit);

    // Feed more JSON bytes (partial or whole). Safe to call repeatedly.
    void feed(const std::string &chunk);

    // After the full JSON response has been fed, return the complete
    // content string the streamer saw. Useful for error cases where
    // callbacks weren't accumulated elsewhere.
    const std::string& content() const { return content_; }

private:
    void handle_char(char c);

    Emit emit_;
    std::string content_;           // decoded chars emitted so far
    int object_depth_ = 0;          // running top-level object depth
    int array_depth_ = 0;           // tracks array nesting
    bool in_string_ = false;        // inside any JSON string literal
    bool in_key_ = false;           // current string is a map key
    bool escape_next_ = false;      // prev char was `\` inside string
    int unicode_pending_ = 0;       // chars left in a \uXXXX sequence
    std::string unicode_buf_;       // accumulates \uXXXX hex digits
    std::string pending_key_;       // most recently completed key
    enum class Mode { Idle, EmittingContent } mode_ = Mode::Idle;
};

#endif
