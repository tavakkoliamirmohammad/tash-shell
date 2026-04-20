
#include "tash/llm_client.h"
#include "tash/ai.h"
#include "tash/ai/ai_abort.h"
#include "tash/ai/llm_diagnostics.h"
#include "tash/ai/model_defaults.h"
#include "tash/util/io.h"
#include <nlohmann/json.hpp>
#include <string>
#include <sstream>
#include <memory>
#include <cstdlib>
#include <cstddef>
#include <cstdio>
#include <chrono>

#include <curl/curl.h>

using namespace std;
using json = nlohmann::json;

// ── Retry helper ─────────────────────────────────────────────

bool LLMClient::is_retryable(const LLMResponse &resp) {
    if (resp.success) return false;
    // Retry on: connection failure (0), server errors (500+), rate limit (429)
    return resp.http_status == 0 || resp.http_status >= 500 || resp.http_status == 429;
}

static void retry_sleep() {
    // 2-second backoff before retry
    struct timespec ts;
    ts.tv_sec = 2;
    ts.tv_nsec = 0;
    nanosleep(&ts, NULL);
}

// ═════════════════════════════════════════════════════════════════
// Diagnostic helpers for O7.2 — rich error output on AI failures.
// The actual formatting + severity routing lives in src/ai/llm_diagnostics.cpp
// (exposed via tash::ai::diag) so unit tests can exercise the exact
// output format without mocking HTTPS. Aliased into file scope for
// brevity; every failure site below funnels through these.
// ═════════════════════════════════════════════════════════════════

using tash::ai::diag::log_http_failure;
using tash::ai::diag::log_curl_failure;
using tash::ai::diag::log_request_debug;
using tash::ai::diag::log_response_debug;
using tash::ai::diag::truncate_for_debug;

// ═════════════════════════════════════════════════════════════════
// Gemini JSON helpers (exposed for testing)
// ═════════════════════════════════════════════════════════════════

string build_gemini_request_json(const string &system_prompt,
                                  const string &user_prompt) {
    json req;
    req["system_instruction"]["parts"] = json::array({{{"text", system_prompt}}});
    req["contents"] = json::array({{{"role", "user"}, {"parts", json::array({{{"text", user_prompt}}})}}});
    return req.dump();
}

string build_gemini_context_json(const string &system_prompt,
                                  const vector<ConversationTurn> &history,
                                  const string &user_prompt) {
    json req;
    req["system_instruction"]["parts"] = json::array({{{"text", system_prompt}}});

    json contents = json::array();
    for (size_t i = 0; i < history.size(); i++) {
        string role = (history[i].role == "assistant") ? "model" : "user";
        json entry;
        entry["role"] = role;
        entry["parts"] = json::array({{{"text", history[i].text}}});
        contents.push_back(entry);
    }
    // Add the new user prompt
    json new_entry;
    new_entry["role"] = "user";
    new_entry["parts"] = json::array({{{"text", user_prompt}}});
    contents.push_back(new_entry);

    req["contents"] = contents;
    return req.dump();
}

string extract_gemini_text(const string &json_body) {
    try {
        json j = json::parse(json_body);
        if (j.contains("candidates") &&
            j["candidates"].is_array() &&
            !j["candidates"].empty() &&
            j["candidates"][0].contains("content") &&
            j["candidates"][0]["content"].contains("parts") &&
            j["candidates"][0]["content"]["parts"].is_array() &&
            !j["candidates"][0]["content"]["parts"].empty() &&
            j["candidates"][0]["content"]["parts"][0].contains("text")) {
            return j["candidates"][0]["content"]["parts"][0]["text"].get<string>();
        }
    } catch (const json::exception &) {
    }
    return "";
}

string extract_gemini_error(const string &json_body) {
    try {
        json j = json::parse(json_body);
        if (j.contains("error") &&
            j["error"].contains("message")) {
            return j["error"]["message"].get<string>();
        }
    } catch (const json::exception &) {
    }
    return "";
}

// ═════════════════════════════════════════════════════════════════
// Gemini structured output JSON builders
// ═════════════════════════════════════════════════════════════════

string build_gemini_structured_json(const string &system_prompt,
                                     const string &user_prompt) {
    json req;
    req["system_instruction"]["parts"] = json::array({{{"text", system_prompt}}});
    req["contents"] = json::array({{{"role", "user"}, {"parts", json::array({{{"text", user_prompt}}})}}});

    // Structured output schema
    req["generationConfig"]["responseMimeType"] = "application/json";
    req["generationConfig"]["responseSchema"]["type"] = "OBJECT";
    req["generationConfig"]["responseSchema"]["properties"]["response_type"]["type"] = "STRING";
    req["generationConfig"]["responseSchema"]["properties"]["response_type"]["enum"] = json::array({"command", "script", "steps", "answer"});
    req["generationConfig"]["responseSchema"]["properties"]["content"]["type"] = "STRING";
    req["generationConfig"]["responseSchema"]["properties"]["filename"]["type"] = "STRING";
    json step_item;
    step_item["type"] = "OBJECT";
    step_item["properties"]["description"]["type"] = "STRING";
    step_item["properties"]["command"]["type"] = "STRING";
    step_item["required"] = json::array({"description", "command"});
    req["generationConfig"]["responseSchema"]["properties"]["steps"]["type"] = "ARRAY";
    req["generationConfig"]["responseSchema"]["properties"]["steps"]["items"] = step_item;
    req["generationConfig"]["responseSchema"]["required"] = json::array({"response_type", "content"});

    return req.dump();
}

string build_gemini_structured_context_json(const string &system_prompt,
                                             const vector<ConversationTurn> &history,
                                             const string &user_prompt) {
    json req;
    req["system_instruction"]["parts"] = json::array({{{"text", system_prompt}}});

    json contents = json::array();
    for (size_t i = 0; i < history.size(); i++) {
        string role = (history[i].role == "assistant") ? "model" : "user";
        json entry;
        entry["role"] = role;
        entry["parts"] = json::array({{{"text", history[i].text}}});
        contents.push_back(entry);
    }
    json new_entry;
    new_entry["role"] = "user";
    new_entry["parts"] = json::array({{{"text", user_prompt}}});
    contents.push_back(new_entry);

    req["contents"] = contents;

    // Structured output schema
    req["generationConfig"]["responseMimeType"] = "application/json";
    req["generationConfig"]["responseSchema"]["type"] = "OBJECT";
    req["generationConfig"]["responseSchema"]["properties"]["response_type"]["type"] = "STRING";
    req["generationConfig"]["responseSchema"]["properties"]["response_type"]["enum"] = json::array({"command", "script", "steps", "answer"});
    req["generationConfig"]["responseSchema"]["properties"]["content"]["type"] = "STRING";
    req["generationConfig"]["responseSchema"]["properties"]["filename"]["type"] = "STRING";
    json step_item2;
    step_item2["type"] = "OBJECT";
    step_item2["properties"]["description"]["type"] = "STRING";
    step_item2["properties"]["command"]["type"] = "STRING";
    step_item2["required"] = json::array({"description", "command"});
    req["generationConfig"]["responseSchema"]["properties"]["steps"]["type"] = "ARRAY";
    req["generationConfig"]["responseSchema"]["properties"]["steps"]["items"] = step_item2;
    req["generationConfig"]["responseSchema"]["required"] = json::array({"response_type", "content"});

    return req.dump();
}

// ═════════════════════════════════════════════════════════════════
// OpenAI JSON helpers (exposed for testing)
// ═════════════════════════════════════════════════════════════════

string build_openai_request_json(const string &model, const string &system_prompt,
                                  const string &user_prompt, bool stream) {
    json req;
    req["model"] = model;
    req["messages"] = json::array({
        {{"role", "system"}, {"content", system_prompt}},
        {{"role", "user"}, {"content", user_prompt}}
    });
    req["stream"] = stream;
    return req.dump();
}

string build_openai_context_json(const string &model, const string &system_prompt,
                                  const vector<ConversationTurn> &history,
                                  const string &user_prompt, bool stream) {
    json req;
    req["model"] = model;

    json messages = json::array();
    messages.push_back({{"role", "system"}, {"content", system_prompt}});
    for (size_t i = 0; i < history.size(); i++) {
        messages.push_back({{"role", history[i].role}, {"content", history[i].text}});
    }
    messages.push_back({{"role", "user"}, {"content", user_prompt}});

    req["messages"] = messages;
    req["stream"] = stream;
    return req.dump();
}

string extract_openai_text(const string &json_body) {
    try {
        json j = json::parse(json_body);
        if (j.contains("choices") &&
            j["choices"].is_array() &&
            !j["choices"].empty() &&
            j["choices"][0].contains("message") &&
            j["choices"][0]["message"].contains("content")) {
            return j["choices"][0]["message"]["content"].get<string>();
        }
    } catch (const json::exception &) {
    }
    return "";
}

string extract_openai_error(const string &json_body) {
    try {
        json j = json::parse(json_body);
        if (j.contains("error") &&
            j["error"].contains("message")) {
            return j["error"]["message"].get<string>();
        }
    } catch (const json::exception &) {
    }
    return "";
}

// ═════════════════════════════════════════════════════════════════
// OpenAI structured output JSON builders
// ═════════════════════════════════════════════════════════════════

string build_openai_structured_json(const string &model,
                                     const string &system_prompt,
                                     const string &user_prompt,
                                     bool stream) {
    json req;
    req["model"] = model;
    req["messages"] = json::array({
        {{"role", "system"}, {"content", system_prompt}},
        {{"role", "user"}, {"content", user_prompt}}
    });
    if (stream) req["stream"] = true;

    // Structured output
    json schema;
    schema["type"] = "object";
    schema["properties"]["response_type"]["type"] = "string";
    schema["properties"]["response_type"]["enum"] = json::array({"command", "script", "steps", "answer"});
    schema["properties"]["content"]["type"] = "string";
    schema["properties"]["filename"]["type"] = "string";
    json step_obj;
    step_obj["type"] = "object";
    step_obj["properties"]["description"]["type"] = "string";
    step_obj["properties"]["command"]["type"] = "string";
    step_obj["required"] = json::array({"description", "command"});
    step_obj["additionalProperties"] = false;
    schema["properties"]["steps"]["type"] = "array";
    schema["properties"]["steps"]["items"] = step_obj;
    schema["required"] = json::array({"response_type", "content"});
    schema["additionalProperties"] = false;

    req["response_format"]["type"] = "json_schema";
    req["response_format"]["json_schema"]["name"] = "ai_response";
    req["response_format"]["json_schema"]["schema"] = schema;
    req["response_format"]["json_schema"]["strict"] = true;

    return req.dump();
}

string build_openai_structured_context_json(const string &model,
                                             const string &system_prompt,
                                             const vector<ConversationTurn> &history,
                                             const string &user_prompt,
                                             bool stream) {
    json req;
    req["model"] = model;

    json messages = json::array();
    messages.push_back({{"role", "system"}, {"content", system_prompt}});
    for (size_t i = 0; i < history.size(); i++) {
        messages.push_back({{"role", history[i].role}, {"content", history[i].text}});
    }
    messages.push_back({{"role", "user"}, {"content", user_prompt}});

    req["messages"] = messages;
    if (stream) req["stream"] = true;

    // Structured output
    json schema;
    schema["type"] = "object";
    schema["properties"]["response_type"]["type"] = "string";
    schema["properties"]["response_type"]["enum"] = json::array({"command", "script", "steps", "answer"});
    schema["properties"]["content"]["type"] = "string";
    schema["properties"]["filename"]["type"] = "string";
    json step_obj;
    step_obj["type"] = "object";
    step_obj["properties"]["description"]["type"] = "string";
    step_obj["properties"]["command"]["type"] = "string";
    step_obj["required"] = json::array({"description", "command"});
    step_obj["additionalProperties"] = false;
    schema["properties"]["steps"]["type"] = "array";
    schema["properties"]["steps"]["items"] = step_obj;
    schema["required"] = json::array({"response_type", "content"});
    schema["additionalProperties"] = false;

    req["response_format"]["type"] = "json_schema";
    req["response_format"]["json_schema"]["name"] = "ai_response";
    req["response_format"]["json_schema"]["schema"] = schema;
    req["response_format"]["json_schema"]["strict"] = true;

    return req.dump();
}

// ═════════════════════════════════════════════════════════════════
// Ollama JSON helpers (exposed for testing)
// ═════════════════════════════════════════════════════════════════

string build_ollama_request_json(const string &model, const string &system_prompt,
                                  const string &user_prompt, bool stream) {
    json req;
    req["model"] = model;
    req["messages"] = json::array({
        {{"role", "system"}, {"content", system_prompt}},
        {{"role", "user"}, {"content", user_prompt}}
    });
    req["stream"] = stream;
    return req.dump();
}

string build_ollama_context_json(const string &model, const string &system_prompt,
                                  const vector<ConversationTurn> &history,
                                  const string &user_prompt, bool stream) {
    json req;
    req["model"] = model;

    json messages = json::array();
    messages.push_back({{"role", "system"}, {"content", system_prompt}});
    for (size_t i = 0; i < history.size(); i++) {
        messages.push_back({{"role", history[i].role}, {"content", history[i].text}});
    }
    messages.push_back({{"role", "user"}, {"content", user_prompt}});

    req["messages"] = messages;
    req["stream"] = stream;
    return req.dump();
}

string extract_ollama_text(const string &json_body) {
    try {
        json j = json::parse(json_body);
        if (j.contains("message") &&
            j["message"].contains("content")) {
            return j["message"]["content"].get<string>();
        }
    } catch (const json::exception &) {
    }
    return "";
}

// ═════════════════════════════════════════════════════════════════
// Ollama structured output JSON builders
// ═════════════════════════════════════════════════════════════════

string build_ollama_structured_json(const string &model,
                                     const string &system_prompt,
                                     const string &user_prompt,
                                     bool stream) {
    // Ollama needs schema instructions in the prompt since it only has JSON mode
    string enhanced_prompt = system_prompt +
        "\n\nYou MUST respond with valid JSON matching this exact schema:\n"
        "{\"response_type\": \"command\"|\"script\"|\"steps\"|\"answer\", \"content\": \"...\", \"filename\": \"...\", \"steps\": [{\"description\": \"...\", \"command\": \"...\"}]}\n"
        "response_type is required. content is required. filename is only needed for scripts.";

    json req;
    req["model"] = model;
    req["messages"] = json::array({
        {{"role", "system"}, {"content", enhanced_prompt}},
        {{"role", "user"}, {"content", user_prompt}}
    });
    req["stream"] = stream;
    req["format"] = "json";
    return req.dump();
}

string build_ollama_structured_context_json(const string &model,
                                             const string &system_prompt,
                                             const vector<ConversationTurn> &history,
                                             const string &user_prompt,
                                             bool stream) {
    string enhanced_prompt = system_prompt +
        "\n\nYou MUST respond with valid JSON matching this exact schema:\n"
        "{\"response_type\": \"command\"|\"script\"|\"steps\"|\"answer\", \"content\": \"...\", \"filename\": \"...\", \"steps\": [{\"description\": \"...\", \"command\": \"...\"}]}\n"
        "response_type is required. content is required. filename is only needed for scripts.";

    json req;
    req["model"] = model;

    json messages = json::array();
    messages.push_back({{"role", "system"}, {"content", enhanced_prompt}});
    for (size_t i = 0; i < history.size(); i++) {
        messages.push_back({{"role", history[i].role}, {"content", history[i].text}});
    }
    messages.push_back({{"role", "user"}, {"content", user_prompt}});

    req["messages"] = messages;
    req["stream"] = stream;
    req["format"] = "json";
    return req.dump();
}

// ═════════════════════════════════════════════════════════════════
// Shared curl streaming helper
// ═════════════════════════════════════════════════════════════════

struct CurlStreamContext {
    string buffer;
    string accumulated;
    function<void(const string &chunk)> on_chunk;
    function<string(const string &line)> parse_line; // returns extracted text or ""
    std::size_t total_bytes = 0;
    bool overflowed = false;
};

static constexpr size_t kDefaultMaxResponseBytes = 10ull * 1024 * 1024;  // 10 MiB

static std::size_t tash_max_response_bytes() {
    static const std::size_t cached = []() -> std::size_t {
        const char *env = std::getenv("TASH_AI_MAX_RESPONSE_BYTES");
        if (env && *env) {
            // strtoull accepts leading sign characters and returns a wrapped huge
            // value for negative input — reject anything not starting with a digit.
            if (*env >= '0' && *env <= '9') {
                char *end = nullptr;
                unsigned long long v = std::strtoull(env, &end, 10);
                if (end != env && *end == '\0' && v > 0) {
                    return static_cast<std::size_t>(v);
                }
            }
        }
        return kDefaultMaxResponseBytes;
    }();
    return cached;
}

static size_t tash_curl_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total = size * nmemb;
    CurlStreamContext *ctx = static_cast<CurlStreamContext*>(userdata);
    ctx->total_bytes += total;
    if (ctx->total_bytes > tash_max_response_bytes()) {
        ctx->overflowed = true;
        return 0;  // aborts transfer with CURLE_WRITE_ERROR
    }
    ctx->buffer.append(ptr, total);

    size_t pos;
    while ((pos = ctx->buffer.find('\n')) != string::npos) {
        string line = ctx->buffer.substr(0, pos);
        ctx->buffer.erase(0, pos + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();

        string chunk_text = ctx->parse_line(line);
        if (!chunk_text.empty()) {
            ctx->accumulated += chunk_text;
            if (ctx->on_chunk) ctx->on_chunk(chunk_text);
        }
    }
    return total;
}

// ═════════════════════════════════════════════════════════════════
// Non-streaming POST — mirrors curl_streaming_post but buffers the
// whole response body into a string instead of streaming line-by-line.
// This replaces cpp-httplib, which required OpenSSL 3 and pulled in a
// second HTTP stack alongside libcurl. libcurl covers both paths now.
// ═════════════════════════════════════════════════════════════════

struct CurlBufferContext {
    string body;
    bool overflowed = false;
};

static size_t tash_curl_buffer_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total = size * nmemb;
    auto *ctx = static_cast<CurlBufferContext*>(userdata);
    if (ctx->body.size() + total > tash_max_response_bytes()) {
        ctx->overflowed = true;
        return 0;  // aborts transfer with CURLE_WRITE_ERROR
    }
    ctx->body.append(ptr, total);
    return total;
}

// Error classification for curl/HTTP failures. Lets the top-level error
// mapper turn "couldn't reach X API" into something a user can act on
// without scraping curl's free-form strerror text.
enum class TransportError {
    None,           // success (reached_server=true)
    Aborted,        // user Ctrl+C during request
    Timeout,        // connect or read timeout
    DnsFailure,     // could not resolve host
    ConnectFailed,  // refused / network unreachable
    TlsFailure,     // TLS handshake / cert / protocol error
    Overflow,       // response exceeded TASH_AI_MAX_RESPONSE_BYTES
    CurlInit,       // curl_easy_init returned null (OOM / library broken)
    Other,          // anything else (see curl_message)
};

struct [[nodiscard]] CurlPostResult {
    bool reached_server;   // false on connection failure
    int http_status;       // HTTP status when reached_server is true
    string body;           // response body when reached_server is true
    string error_message;  // set when reached_server is false — already user-friendly
    TransportError error_kind = TransportError::None;
    string curl_message;   // raw curl_easy_strerror for logging/debug
};

// Curl progress callback. Polled several times per second during a
// transfer; returning non-zero aborts the transfer with CURLE_ABORTED_BY_CALLBACK.
// We use it as the Ctrl+C bail-out path — the SIGINT handler arms the
// flag (see src/core/signals.cpp), we notice it here, and the caller
// surfaces "request cancelled" to the user without waiting for timeout.
static int tash_curl_abort_cb(void *, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return tash::ai::abort_flag::should_abort() ? 1 : 0;
}

// Apply the security-critical + common transport options every HTTP call
// in this file needs. Returns true if every option was set successfully;
// returns false if an option failed — callers MUST fail closed because a
// missing TLS verification option on a libcurl built without TLS support
// would silently expose the shell to MitM.
//
// `allow_plain_http` is true only for the Ollama local-endpoint path.
// Gemini/OpenAI always refuse non-TLS. Redirects remain HTTPS-only on
// every path — we never want a 3xx to downgrade to plain HTTP.
static bool configure_common_curl_opts(CURL *curl,
                                       const std::string &url,
                                       const std::string &body,
                                       curl_slist *headers,
                                       long connect_timeout,
                                       long read_timeout,
                                       bool allow_plain_http) {
    auto check = [&](const char *name, CURLcode rc) -> bool {
        if (rc != CURLE_OK) {
            tash::io::error(std::string("curl_easy_setopt(") + name
                            + ") failed: " + curl_easy_strerror(rc));
            return false;
        }
        return true;
    };

    if (!check("CURLOPT_URL",
               curl_easy_setopt(curl, CURLOPT_URL, url.c_str()))) return false;
    // TLS verification — fail closed if libcurl was built without TLS.
    if (!check("CURLOPT_SSL_VERIFYPEER",
               curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L))) return false;
    if (!check("CURLOPT_SSL_VERIFYHOST",
               curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L))) return false;
    // No redirect following: prevents API-key leak to attacker-controlled
    // hosts via 3xx responses (OpenAI bearer is in Authorization, Gemini
    // key is in x-goog-api-key header after the 2026-04 rework).
    if (!check("CURLOPT_FOLLOWLOCATION",
               curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L))) return false;
    const long allowed = allow_plain_http
                             ? (CURLPROTO_HTTPS | CURLPROTO_HTTP)
                             : CURLPROTO_HTTPS;
    if (!check("CURLOPT_PROTOCOLS",
               curl_easy_setopt(curl, CURLOPT_PROTOCOLS, allowed))) return false;
    // Redirects are never allowed to downgrade to plain HTTP, even when
    // the initial request was plain (Ollama case).
    if (!check("CURLOPT_REDIR_PROTOCOLS",
               curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS))) return false;

    // POST body — explicit size prevents strlen-truncation on embedded NUL.
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));

    // Non-security transport settings.
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, connect_timeout);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, read_timeout);

    // Arm the progress callback so Ctrl+C interrupts the transfer
    // instead of waiting for the read timeout.
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, tash_curl_abort_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, nullptr);
    return true;
}

// Classify a curl failure from the CURLcode + overflow flag. The resulting
// error_message is already user-facing; callers may wrap it with provider
// context ("couldn't reach Gemini: ...") but they should not mutate it.
static void classify_curl_error(CURLcode res, bool overflowed,
                                 CurlPostResult &out) {
    out.curl_message = curl_easy_strerror(res);
    if (overflowed) {
        out.error_kind = TransportError::Overflow;
        out.error_message = "response exceeded TASH_AI_MAX_RESPONSE_BYTES ("
                          + std::to_string(tash_max_response_bytes())
                          + " bytes); aborted";
        return;
    }
    switch (res) {
        case CURLE_ABORTED_BY_CALLBACK:
            out.error_kind = TransportError::Aborted;
            out.error_message = "cancelled";
            return;
        case CURLE_OPERATION_TIMEDOUT:
            out.error_kind = TransportError::Timeout;
            out.error_message = "timed out after the configured read timeout";
            return;
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_RESOLVE_PROXY:
            out.error_kind = TransportError::DnsFailure;
            out.error_message = "could not resolve host (no DNS or bad URL)";
            return;
        case CURLE_COULDNT_CONNECT:
        case CURLE_INTERFACE_FAILED:
            out.error_kind = TransportError::ConnectFailed;
            out.error_message = "connection refused or network unreachable";
            return;
        case CURLE_SSL_CONNECT_ERROR:
        case CURLE_PEER_FAILED_VERIFICATION:
        case CURLE_SSL_CERTPROBLEM:
        case CURLE_SSL_CIPHER:
        case CURLE_SSL_CACERT_BADFILE:
        case CURLE_USE_SSL_FAILED:
            out.error_kind = TransportError::TlsFailure;
            out.error_message = "TLS error: " + out.curl_message;
            return;
        default:
            out.error_kind = TransportError::Other;
            out.error_message = "connection failed: " + out.curl_message;
            return;
    }
}

static CurlPostResult curl_post(
    const string &url,
    const string &body,
    const vector<string> &extra_headers,
    int connect_timeout,
    int read_timeout,
    bool allow_plain_http = false)
{
    CurlPostResult out;
    out.reached_server = false;
    out.http_status = 0;

    CURL *curl = curl_easy_init();
    if (!curl) {
        out.error_kind = TransportError::CurlInit;
        out.error_message = "failed to initialize curl.";
        return out;
    }

    CurlBufferContext ctx;
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    for (size_t i = 0; i < extra_headers.size(); i++) {
        headers = curl_slist_append(headers, extra_headers[i].c_str());
    }

    if (!configure_common_curl_opts(curl, url, body, headers,
                                    static_cast<long>(connect_timeout),
                                    static_cast<long>(read_timeout),
                                    allow_plain_http)) {
        out.error_kind = TransportError::TlsFailure;
        out.error_message = "tls configuration failed";
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return out;
    }
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, tash_curl_buffer_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        classify_curl_error(res, ctx.overflowed, out);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return out;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    out.reached_server = true;
    out.http_status = (int)http_code;
    out.body = std::move(ctx.body);
    return out;
}

// Map a CurlPostResult's error_kind to the public-facing TransportStatus.
static TransportStatus public_transport_status(TransportError kind) {
    switch (kind) {
        case TransportError::None:          return TransportStatus::Ok;
        case TransportError::Aborted:       return TransportStatus::Aborted;
        case TransportError::Timeout:       return TransportStatus::Timeout;
        case TransportError::DnsFailure:    return TransportStatus::DnsFailure;
        case TransportError::ConnectFailed: return TransportStatus::ConnectFailed;
        case TransportError::TlsFailure:    return TransportStatus::TlsFailure;
        case TransportError::Overflow:      return TransportStatus::Overflow;
        case TransportError::CurlInit:
        case TransportError::Other:
        default:                            return TransportStatus::Other;
    }
}

static LLMResponse curl_streaming_post(
    const string &url,
    const string &body,
    const vector<string> &extra_headers,
    function<void(const string &chunk)> on_chunk,
    function<string(const string &line)> parse_line,
    int connect_timeout,
    int read_timeout,
    bool allow_plain_http = false)
{
    LLMResponse resp;
    resp.success = false;
    resp.http_status = 0;

    CURL *curl = curl_easy_init();
    if (!curl) {
        resp.transport = TransportStatus::Other;
        resp.error_message = "failed to initialize curl.";
        return resp;
    }

    CurlStreamContext ctx;
    ctx.on_chunk = on_chunk;
    ctx.parse_line = parse_line;

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    for (size_t i = 0; i < extra_headers.size(); i++) {
        headers = curl_slist_append(headers, extra_headers[i].c_str());
    }

    if (!configure_common_curl_opts(curl, url, body, headers,
                                    static_cast<long>(connect_timeout),
                                    static_cast<long>(read_timeout),
                                    allow_plain_http)) {
        resp.transport = TransportStatus::TlsFailure;
        resp.error_message = "tls configuration failed";
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return resp;
    }
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, tash_curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        // Preserve any text that DID arrive before the failure so the
        // caller can surface partial output rather than discarding it.
        resp.partial_text = ctx.accumulated;
        CurlPostResult tmp;
        classify_curl_error(res, ctx.overflowed, tmp);
        resp.transport = public_transport_status(tmp.error_kind);
        resp.error_message = tmp.error_message;
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return resp;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    resp.http_status = (int)http_code;

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (http_code == 200) {
        resp.text = ctx.accumulated;
        resp.success = !resp.text.empty();
        if (!resp.success) {
            resp.error_message = "unexpected response. Try again.";
        }
    } else {
        resp.error_message = "API error (HTTP " + to_string(http_code) + "). Try again.";
    }

    return resp;
}

// ═════════════════════════════════════════════════════════════════
// Provider error mapping — HTTP-level ("reached server, non-2xx")
// ═════════════════════════════════════════════════════════════════

static string map_gemini_error(int status, const string &body) {
    switch (status) {
        case 400: {
            string api_msg = extract_gemini_error(body);
            if (api_msg.find("not found") != string::npos ||
                api_msg.find("is not supported") != string::npos) {
                return "model_not_found";
            }
            return "Gemini rejected the request: "
                 + (api_msg.empty() ? "bad request" : api_msg);
        }
        case 401:
            return "invalid Gemini API key. Run @ai config to update.";
        case 403:
            // Could be quota OR an API-level permission problem. Use the
            // error body to pick the better message when available.
            {
                string api_msg = extract_gemini_error(body);
                if (api_msg.find("quota") != string::npos) {
                    return "Gemini daily quota reached. Try again tomorrow or "
                           "switch provider with @ai config.";
                }
                if (!api_msg.empty()) return "Gemini: " + api_msg;
                return "Gemini quota exhausted or request forbidden.";
            }
        case 404:
            return "model_not_found";
        case 408:
            return "Gemini timed out. Retry the request.";
        case 413:
            return "Gemini: request too large — try a shorter prompt.";
        case 429:
            return "Gemini rate limit hit — please wait a few seconds and retry.";
        case 500: case 502: case 503: case 504:
            return "Gemini is having trouble (HTTP " + to_string(status)
                 + "). Retrying usually works.";
        default: {
            string api_msg = extract_gemini_error(body);
            if (!api_msg.empty()) return "Gemini: " + api_msg;
            return "Gemini returned HTTP " + to_string(status) + ".";
        }
    }
}

// Turn a transport-layer failure into a user-facing message. Keeps the
// provider name consistent ("Gemini", "OpenAI", "Ollama") so the user can
// tell which endpoint is actually broken. Takes the public TransportStatus
// so callers that already carry one on an LLMResponse don't have to
// map it back to the internal enum.
static string format_transport_error(const string &provider,
                                     TransportStatus kind,
                                     const string &curl_message,
                                     const string &raw_message) {
    switch (kind) {
        case TransportStatus::Aborted:
            return provider + " request cancelled.";
        case TransportStatus::Timeout:
            return provider + " timed out. Check your connection or retry.";
        case TransportStatus::DnsFailure:
            return "couldn't resolve " + provider + " hostname. Check your DNS/network.";
        case TransportStatus::ConnectFailed:
            return "couldn't connect to " + provider
                 + ". The service may be down or blocked.";
        case TransportStatus::TlsFailure:
            return provider + " TLS handshake failed: "
                 + (curl_message.empty() ? raw_message : curl_message);
        case TransportStatus::Overflow:
            return provider + " response too large. "
                   "Adjust TASH_AI_MAX_RESPONSE_BYTES if you trust the server.";
        case TransportStatus::Ok:
        default:
            return "couldn't reach " + provider + ": "
                 + (curl_message.empty() ? raw_message : curl_message);
    }
}

// ═════════════════════════════════════════════════════════════════
// ProviderAdapter + shared invoke helpers
//
// Each concrete provider (Gemini/OpenAI/Ollama) lives in its own class
// for virtual dispatch, but 95% of every `generate*` method was the
// same retry/error/logging skeleton with per-provider knobs. That
// skeleton is now the two `invoke_*` helpers below; provider classes
// reduce to "build body, pick URL, hand off to invoke_*".
//
// Keeping the adapter file-local (no header export) because the
// LLMClient public interface is unchanged — this is purely an
// implementation-side dedup.
// ═════════════════════════════════════════════════════════════════

using ExtractTextFn = std::function<std::string(const std::string &body)>;
using MapHttpErrorFn = std::function<std::string(int status, const std::string &body)>;
using ParseStreamLineFn = std::function<std::string(const std::string &line)>;
// Optional custom transport-error formatter for providers with hints
// the generic formatter can't give (Ollama says "is `ollama serve`
// running?" on ConnectFailed). Return empty string to fall through to
// the generic format_transport_error.
using TransportMsgFn = std::function<std::string(TransportStatus,
                                                   const std::string &curl_message,
                                                   const std::string &raw_message)>;

struct ProviderAdapter {
    const char *display_name = "";   // "Gemini", "OpenAI", "Ollama"
    vector<string> auth_headers;     // per-instance, built at ctor
    ExtractTextFn extract_text;
    MapHttpErrorFn map_http_error;
    ParseStreamLineFn parse_stream_line;   // only used by streaming paths
    TransportMsgFn custom_transport_msg;   // optional; empty by default
    bool allow_plain_http = false;
    int connect_timeout = 10;
    int read_timeout = 30;
};

static std::string transport_message(const ProviderAdapter &a,
                                      TransportStatus status,
                                      const std::string &curl_message,
                                      const std::string &raw_message) {
    if (a.custom_transport_msg) {
        std::string m = a.custom_transport_msg(status, curl_message, raw_message);
        if (!m.empty()) return m;
    }
    return format_transport_error(a.display_name, status,
                                   curl_message, raw_message);
}

// Fill an LLMResponse's transport-error fields from a buffered CurlPostResult.
static void fill_transport_error(LLMResponse &resp,
                                  const ProviderAdapter &a,
                                  const CurlPostResult &result) {
    resp.transport = public_transport_status(result.error_kind);
    resp.error_message = transport_message(a, resp.transport,
                                             result.curl_message,
                                             result.error_message);
    resp.http_status = 0;
}

// One buffered round-trip, pre-classified. No retry, no fallback.
// Callers that need retry wrap this in a loop; callers that need model
// fallback do the same.
static LLMResponse invoke_once_buffered(const ProviderAdapter &a,
                                          const std::string &url,
                                          const std::string &body,
                                          const std::string &model_for_debug,
                                          int attempt, int max_attempts) {
    LLMResponse resp;
    resp.success = false;
    resp.http_status = 0;

    log_request_debug(a.display_name, model_for_debug, body.size());
    auto t0 = std::chrono::steady_clock::now();
    auto result = curl_post(url, body, a.auth_headers,
                             a.connect_timeout, a.read_timeout,
                             a.allow_plain_http);
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    if (!result.reached_server) {
        log_curl_failure(a.display_name, result.curl_message.empty()
                            ? result.error_message : result.curl_message,
                         attempt, max_attempts,
                         /*final=*/attempt >= max_attempts);
        fill_transport_error(resp, a, result);
        return resp;
    }

    resp.http_status = result.http_status;

    if (result.http_status == 200) {
        log_response_debug(a.display_name, result.http_status,
                           result.body.size(), elapsed_ms);
        resp.text = a.extract_text(result.body);
        if (!resp.text.empty()) {
            resp.success = true;
        } else {
            resp.error_message = "unexpected response. Try again.";
            tash::io::error(std::string(a.display_name)
                            + ": HTTP 200 but response body was not parsable");
            tash::io::debug(std::string(a.display_name) + ": response body: "
                            + truncate_for_debug(result.body));
        }
        return resp;
    }

    bool will_retry = attempt < max_attempts
                      && (result.http_status == 429 || result.http_status >= 500);
    log_http_failure(a.display_name, result.http_status,
                     attempt, max_attempts, /*final=*/!will_retry,
                     result.body);
    resp.error_message = a.map_http_error(result.http_status, result.body);
    return resp;
}

// Buffered call with retry on transient (429/5xx/transport). Stops early
// on Aborted (user Ctrl+C) — we never re-issue an explicitly-cancelled
// request.
static LLMResponse invoke_buffered(const ProviderAdapter &a,
                                     const std::string &url,
                                     const std::string &body,
                                     const std::string &model_for_debug,
                                     int max_attempts = 2) {
    LLMResponse resp;
    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        if (attempt > 1) retry_sleep();
        resp = invoke_once_buffered(a, url, body, model_for_debug,
                                      attempt, max_attempts);
        if (resp.success) return resp;
        if (resp.transport == TransportStatus::Aborted) return resp;
        if (!LLMClient::is_retryable(resp)) return resp;
    }
    return resp;
}

// One streaming round-trip, pre-classified. No retry.
static LLMResponse invoke_once_streaming(const ProviderAdapter &a,
                                           const std::string &url,
                                           const std::string &body,
                                           const std::string &model_for_debug,
                                           std::function<void(const std::string &)> on_chunk,
                                           int attempt, int max_attempts) {
    log_request_debug(a.display_name, model_for_debug, body.size());
    LLMResponse resp = curl_streaming_post(url, body, a.auth_headers,
                                             on_chunk, a.parse_stream_line,
                                             a.connect_timeout, a.read_timeout,
                                             a.allow_plain_http);
    if (resp.success) return resp;

    if (resp.http_status == 0) {
        log_curl_failure(a.display_name, resp.error_message,
                         attempt, max_attempts,
                         /*final=*/attempt >= max_attempts);
        resp.error_message = transport_message(
            a, resp.transport, /*curl_message=*/"", resp.error_message);
    } else if (resp.http_status != 200) {
        bool will_retry = attempt < max_attempts
                          && (resp.http_status == 429 || resp.http_status >= 500);
        log_http_failure(a.display_name, resp.http_status,
                         attempt, max_attempts, /*final=*/!will_retry,
                         /*response_body=*/"");
        resp.error_message = a.map_http_error(resp.http_status, "");
    }
    return resp;
}

// Streaming with retry. Retries only when no user-visible bytes have
// been emitted yet — retrying after partial output would duplicate
// what's already on the terminal.
static LLMResponse invoke_streaming(
    const ProviderAdapter &a,
    const std::string &url,
    const std::string &body,
    const std::string &model_for_debug,
    std::function<void(const std::string &)> on_chunk,
    int max_attempts = 2) {
    LLMResponse resp;
    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        if (attempt > 1) retry_sleep();
        bool emitted_any = false;
        auto wrap = [&](const std::string &c) {
            if (!c.empty()) emitted_any = true;
            if (on_chunk) on_chunk(c);
        };
        resp = invoke_once_streaming(a, url, body, model_for_debug,
                                       wrap, attempt, max_attempts);
        if (resp.success) return resp;
        if (resp.transport == TransportStatus::Aborted) return resp;
        if (emitted_any) return resp;
        if (!LLMClient::is_retryable(resp)) return resp;
    }
    return resp;
}

// ═════════════════════════════════════════════════════════════════
// GeminiClient implementation
// ═════════════════════════════════════════════════════════════════

GeminiClient::GeminiClient(const string &api_key)
    : api_key_(api_key),
      // Authoritative source: data/ai_models.json. The registry
      // returns a compiled-in default if the file is missing, so
      // construction never fails for lack of config.
      model_(tash::ai::default_model_for("gemini")),
      connect_timeout_(10),
      read_timeout_(30) {
    for (const auto &m : tash::ai::fallback_models_for("gemini")) {
        fallback_models_.push_back(m);
    }
}

void GeminiClient::set_model(const string &model) { model_ = model; }
string GeminiClient::get_model() const { return model_; }

// Build the Gemini endpoint URL. The API key lives in the `x-goog-api-key`
// header (Google's documented alternative to the `?key=...` query-string
// form) so it cannot leak through `CURLOPT_VERBOSE`, `~/.curlrc` or
// `/proc/<pid>/cmdline`-style process-info leaks.
static string gemini_endpoint_url(const string &model, bool stream) {
    const string method = stream ? "streamGenerateContent?alt=sse"
                                  : "generateContent";
    return "https://generativelanguage.googleapis.com/v1beta/models/"
         + model + ":" + method;
}

static ProviderAdapter gemini_adapter(const string &api_key,
                                        int connect_timeout, int read_timeout,
                                        bool streaming) {
    ProviderAdapter a;
    a.display_name = "Gemini";
    a.auth_headers = {"x-goog-api-key: " + api_key};
    a.extract_text = extract_gemini_text;
    a.map_http_error = map_gemini_error;
    a.connect_timeout = connect_timeout;
    a.read_timeout = read_timeout;
    a.allow_plain_http = false;
    if (streaming) {
        a.parse_stream_line = [](const string &line) -> string {
            if (line.size() > 6 && line.substr(0, 6) == "data: ") {
                return extract_gemini_text(line.substr(6));
            }
            return "";
        };
    }
    return a;
}

// Gemini's model-fallback pattern: try the primary; if HTTP says the
// model doesn't exist (`model_not_found` from map_gemini_error), try
// each fallback in order. Both buffered and streaming variants below
// wrap invoke_* with this chain. The `max_attempts` applies per-model.
static LLMResponse gemini_buffered_chain(const ProviderAdapter &a,
                                           const string &body,
                                           const string &primary_model,
                                           const vector<string> &fallbacks,
                                           int max_attempts) {
    auto try_model = [&](const string &model) {
        string url = gemini_endpoint_url(model, /*stream=*/false);
        return invoke_buffered(a, url, body, model, max_attempts);
    };
    LLMResponse resp = try_model(primary_model);
    if (resp.success || resp.error_message != "model_not_found") return resp;
    for (const auto &fb : fallbacks) {
        resp = try_model(fb);
        if (resp.success || resp.error_message != "model_not_found") return resp;
    }
    resp.success = false;
    resp.http_status = 404;
    resp.error_message = "AI model unavailable.";
    return resp;
}

static LLMResponse gemini_streaming_chain(const ProviderAdapter &a,
                                            const string &body,
                                            const string &primary_model,
                                            const vector<string> &fallbacks,
                                            std::function<void(const string &)> on_chunk,
                                            int max_attempts) {
    auto try_model = [&](const string &model) {
        string url = gemini_endpoint_url(model, /*stream=*/true);
        return invoke_streaming(a, url, body, model, on_chunk, max_attempts);
    };
    LLMResponse resp = try_model(primary_model);
    if (resp.success || resp.error_message != "model_not_found") return resp;
    for (const auto &fb : fallbacks) {
        resp = try_model(fb);
        if (resp.success || resp.error_message != "model_not_found") return resp;
    }
    resp.success = false;
    resp.http_status = 404;
    resp.error_message = "AI model unavailable.";
    return resp;
}

LLMResponse GeminiClient::generate(const string &system_prompt, const string &user_prompt) {
    auto a = gemini_adapter(api_key_, connect_timeout_, read_timeout_, /*streaming=*/false);
    string body = build_gemini_request_json(system_prompt, user_prompt);
    return gemini_buffered_chain(a, body, model_, fallback_models_, /*max_attempts=*/2);
}

LLMResponse GeminiClient::generate_stream(const string &system_prompt, const string &user_prompt,
                                           std::function<void(const string &chunk)> on_chunk) {
    auto a = gemini_adapter(api_key_, connect_timeout_, read_timeout_, /*streaming=*/true);
    string body = build_gemini_request_json(system_prompt, user_prompt);
    return gemini_streaming_chain(a, body, model_, fallback_models_, on_chunk,
                                   /*max_attempts=*/1);
}

LLMResponse GeminiClient::generate_with_context(const string &system_prompt,
                                                  const vector<ConversationTurn> &history,
                                                  const string &user_prompt) {
    auto a = gemini_adapter(api_key_, connect_timeout_, read_timeout_, /*streaming=*/false);
    string body = build_gemini_context_json(system_prompt, history, user_prompt);
    return gemini_buffered_chain(a, body, model_, fallback_models_, /*max_attempts=*/1);
}

LLMResponse GeminiClient::generate_structured(const string &system_prompt,
                                                const string &user_prompt) {
    auto a = gemini_adapter(api_key_, connect_timeout_, read_timeout_, /*streaming=*/false);
    string body = build_gemini_structured_json(system_prompt, user_prompt);
    return gemini_buffered_chain(a, body, model_, fallback_models_, /*max_attempts=*/2);
}

LLMResponse GeminiClient::generate_structured_with_context(
    const string &system_prompt,
    const vector<ConversationTurn> &history,
    const string &user_prompt) {
    auto a = gemini_adapter(api_key_, connect_timeout_, read_timeout_, /*streaming=*/false);
    string body = build_gemini_structured_context_json(system_prompt, history, user_prompt);
    return gemini_buffered_chain(a, body, model_, fallback_models_, /*max_attempts=*/1);
}

// ═════════════════════════════════════════════════════════════════
// OpenAI error mapping
// ═════════════════════════════════════════════════════════════════

static string map_openai_error(int status, const string &body) {
    string api_msg = extract_openai_error(body);
    switch (status) {
        case 400:
            return "OpenAI rejected the request: "
                 + (api_msg.empty() ? "bad request" : api_msg);
        case 401:
            return "invalid OpenAI API key. Run @ai config to update.";
        case 403:
            return "OpenAI: this key doesn't have access to that model or region.";
        case 404:
            return "OpenAI model not found. Check your model name with @ai config.";
        case 408:
            return "OpenAI timed out. Retry the request.";
        case 413:
            return "OpenAI: request too large — try a shorter prompt.";
        case 429:
            // OpenAI collapses rate-limit and quota-exhausted into 429;
            // the body's "code" disambiguates.
            if (api_msg.find("quota") != string::npos ||
                api_msg.find("insufficient") != string::npos ||
                body.find("insufficient_quota") != string::npos) {
                return "OpenAI quota exhausted. Check your billing dashboard.";
            }
            return "OpenAI rate limit — wait a few seconds and retry.";
        case 500: case 502: case 503: case 504:
            return "OpenAI is having trouble (HTTP " + to_string(status)
                 + "). Retrying usually works.";
        default:
            if (!api_msg.empty()) return "OpenAI: " + api_msg;
            return "OpenAI returned HTTP " + to_string(status) + ".";
    }
}

// ═════════════════════════════════════════════════════════════════
// Ollama error mapping — matches Gemini/OpenAI, but geared toward the
// common local-setup failure modes (model not pulled, daemon not up,
// disk full mid-download).
// ═════════════════════════════════════════════════════════════════

static string map_ollama_error(int status, const string &body) {
    switch (status) {
        case 400:
            return "Ollama rejected the request: "
                 + (body.empty() ? "bad request" : body);
        case 404:
            // Most common: the model hasn't been pulled yet. Point the
            // user at the exact command they need instead of a generic
            // "not found" message.
            return "Ollama: model not found. Pull it with `ollama pull <model>`.";
        case 413:
            return "Ollama: prompt too large for this model's context.";
        case 500:
            return "Ollama hit an internal error — check `ollama logs` for details.";
        case 503:
            return "Ollama server not ready — is `ollama serve` still starting?";
        default:
            if (!body.empty()) return "Ollama: " + body;
            return "Ollama returned HTTP " + to_string(status) + ".";
    }
}

// ═════════════════════════════════════════════════════════════════
// OpenAIClient implementation
// ═════════════════════════════════════════════════════════════════

static const char *kOpenAIChatUrl = "https://api.openai.com/v1/chat/completions";

static ProviderAdapter openai_adapter(const string &api_key,
                                        int connect_timeout, int read_timeout,
                                        bool streaming) {
    ProviderAdapter a;
    a.display_name = "OpenAI";
    a.auth_headers = {"Authorization: Bearer " + api_key};
    a.extract_text = extract_openai_text;
    a.map_http_error = map_openai_error;
    a.connect_timeout = connect_timeout;
    a.read_timeout = read_timeout;
    a.allow_plain_http = false;
    if (streaming) {
        a.parse_stream_line = [](const string &line) -> string {
            if (line == "data: [DONE]") return "";
            if (line.size() > 6 && line.substr(0, 6) == "data: ") {
                try {
                    json j = json::parse(line.substr(6));
                    if (j.contains("choices") && j["choices"].is_array() &&
                        !j["choices"].empty() &&
                        j["choices"][0].contains("delta") &&
                        j["choices"][0]["delta"].contains("content")) {
                        return j["choices"][0]["delta"]["content"].get<string>();
                    }
                } catch (const json::exception &) {}
            }
            return "";
        };
    }
    return a;
}

OpenAIClient::OpenAIClient(const string &api_key)
    : api_key_(api_key),
      model_(tash::ai::default_model_for("openai")),
      connect_timeout_(10),
      read_timeout_(60) {}

void OpenAIClient::set_model(const string &model) { model_ = model; }
string OpenAIClient::get_model() const { return model_; }

LLMResponse OpenAIClient::generate(const string &system_prompt, const string &user_prompt) {
    auto a = openai_adapter(api_key_, connect_timeout_, read_timeout_, /*streaming=*/false);
    string body = build_openai_request_json(model_, system_prompt, user_prompt, false);
    return invoke_buffered(a, kOpenAIChatUrl, body, model_);
}

LLMResponse OpenAIClient::generate_stream(const string &system_prompt, const string &user_prompt,
                                           std::function<void(const string &chunk)> on_chunk) {
    auto a = openai_adapter(api_key_, connect_timeout_, read_timeout_, /*streaming=*/true);
    string body = build_openai_request_json(model_, system_prompt, user_prompt, /*stream=*/true);
    return invoke_streaming(a, kOpenAIChatUrl, body, model_, on_chunk,
                              /*max_attempts=*/1);
}

LLMResponse OpenAIClient::generate_with_context(const string &system_prompt,
                                                  const vector<ConversationTurn> &history,
                                                  const string &user_prompt) {
    auto a = openai_adapter(api_key_, connect_timeout_, read_timeout_, /*streaming=*/false);
    string body = build_openai_context_json(model_, system_prompt, history, user_prompt, false);
    return invoke_buffered(a, kOpenAIChatUrl, body, model_, /*max_attempts=*/1);
}

LLMResponse OpenAIClient::generate_structured(const string &system_prompt,
                                                const string &user_prompt) {
    auto a = openai_adapter(api_key_, connect_timeout_, read_timeout_, /*streaming=*/false);
    string body = build_openai_structured_json(model_, system_prompt, user_prompt);
    return invoke_buffered(a, kOpenAIChatUrl, body, model_);
}

LLMResponse OpenAIClient::generate_structured_with_context(
    const string &system_prompt,
    const vector<ConversationTurn> &history,
    const string &user_prompt) {
    auto a = openai_adapter(api_key_, connect_timeout_, read_timeout_, /*streaming=*/false);
    string body = build_openai_structured_context_json(model_, system_prompt, history, user_prompt);
    return invoke_buffered(a, kOpenAIChatUrl, body, model_, /*max_attempts=*/1);
}

// ═════════════════════════════════════════════════════════════════
// OllamaClient implementation
// ═════════════════════════════════════════════════════════════════

static void parse_ollama_url(const string &url, string &host, int &port) {
    // Default
    host = "http://localhost";
    port = 11434;

    if (url.empty()) return;

    // URL format: http://host:port or just host:port
    string work = url;

    // Strip trailing slash
    while (!work.empty() && work.back() == '/') {
        work.pop_back();
    }

    // Check for scheme
    string scheme = "http://";
    size_t scheme_end = work.find("://");
    if (scheme_end != string::npos) {
        scheme = work.substr(0, scheme_end + 3);
        work = work.substr(scheme_end + 3);
    }

    // Split host:port
    size_t colon = work.rfind(':');
    if (colon != string::npos) {
        string port_str = work.substr(colon + 1);
        try {
            port = stoi(port_str);
            host = scheme + work.substr(0, colon);
        } catch (const std::exception &) {
            host = scheme + work;
        }
    } else {
        host = scheme + work;
    }
}

OllamaClient::OllamaClient(const string &endpoint_url)
    : model_("qwen3.5:0.8b"),
      connect_timeout_(5),
      read_timeout_(120) {
    parse_ollama_url(endpoint_url, host_, port_);
}

void OllamaClient::set_model(const string &model) { model_ = model; }
string OllamaClient::get_model() const { return model_; }

static ProviderAdapter ollama_adapter(const string &client_url,
                                        int connect_timeout, int read_timeout,
                                        bool streaming) {
    ProviderAdapter a;
    a.display_name = "Ollama";
    a.extract_text = extract_ollama_text;
    a.map_http_error = [](int status, const string &body) {
        return map_ollama_error(status, body);
    };
    a.connect_timeout = connect_timeout;
    a.read_timeout = read_timeout;
    a.allow_plain_http = true;
    // Ollama deserves the extra hint that it's probably not running —
    // users hit ConnectFailed/DnsFailure on `localhost:11434` far more
    // often than they hit the generic fall-through message.
    a.custom_transport_msg = [client_url](TransportStatus s,
                                           const string &,
                                           const string &) -> string {
        if (s == TransportStatus::ConnectFailed ||
            s == TransportStatus::DnsFailure) {
            return "couldn't reach Ollama at " + client_url
                 + " — is `ollama serve` running?";
        }
        return {};  // fall through to format_transport_error
    };
    if (streaming) {
        a.parse_stream_line = [](const string &line) -> string {
            if (line.empty()) return "";
            try {
                json j = json::parse(line);
                if (j.contains("message") && j["message"].contains("content")) {
                    return j["message"]["content"].get<string>();
                }
            } catch (...) {}
            return "";
        };
    }
    return a;
}

LLMResponse OllamaClient::generate(const string &system_prompt, const string &user_prompt) {
    string client_url = host_ + ":" + to_string(port_);
    string body = build_ollama_request_json(model_, system_prompt, user_prompt, false);
    auto a = ollama_adapter(client_url, connect_timeout_, read_timeout_, /*streaming=*/false);
    return invoke_buffered(a, client_url + "/api/chat", body, model_);
}

LLMResponse OllamaClient::generate_stream(const string &system_prompt, const string &user_prompt,
                                            std::function<void(const string &chunk)> on_chunk) {
    string client_url = host_ + ":" + to_string(port_);
    string body = build_ollama_request_json(model_, system_prompt, user_prompt, true);
    auto a = ollama_adapter(client_url, connect_timeout_, read_timeout_, /*streaming=*/true);
    return invoke_streaming(a, client_url + "/api/chat", body, model_, on_chunk,
                              /*max_attempts=*/1);
}

LLMResponse OllamaClient::generate_with_context(const string &system_prompt,
                                                  const vector<ConversationTurn> &history,
                                                  const string &user_prompt) {
    string client_url = host_ + ":" + to_string(port_);
    string body = build_ollama_context_json(model_, system_prompt, history, user_prompt, false);
    auto a = ollama_adapter(client_url, connect_timeout_, read_timeout_, /*streaming=*/false);
    return invoke_buffered(a, client_url + "/api/chat", body, model_, /*max_attempts=*/1);
}

LLMResponse OllamaClient::generate_structured(const string &system_prompt,
                                                const string &user_prompt) {
    string client_url = host_ + ":" + to_string(port_);
    string body = build_ollama_structured_json(model_, system_prompt, user_prompt);
    auto a = ollama_adapter(client_url, connect_timeout_, read_timeout_, /*streaming=*/false);
    return invoke_buffered(a, client_url + "/api/chat", body, model_);
}

LLMResponse OllamaClient::generate_structured_with_context(
    const string &system_prompt,
    const vector<ConversationTurn> &history,
    const string &user_prompt) {
    string client_url = host_ + ":" + to_string(port_);
    string body = build_ollama_structured_context_json(model_, system_prompt, history, user_prompt);
    auto a = ollama_adapter(client_url, connect_timeout_, read_timeout_, /*streaming=*/false);
    return invoke_buffered(a, client_url + "/api/chat", body, model_, /*max_attempts=*/1);
}

// ═════════════════════════════════════════════════════════════════
// JsonContentStreamer — extracts the root `content` string from a JSON
// response as its bytes stream in, decoding escapes and forwarding each
// piece to the emit callback. See tash/llm_client.h for rationale.
// ═════════════════════════════════════════════════════════════════

JsonContentStreamer::JsonContentStreamer(Emit emit)
    : emit_(std::move(emit)) {}

void JsonContentStreamer::feed(const string &chunk) {
    for (char c : chunk) handle_char(c);
}

void JsonContentStreamer::handle_char(char c) {
    // Unicode escape (\uXXXX) — collect four hex digits, decode as UTF-8.
    if (unicode_pending_ > 0) {
        unicode_buf_.push_back(c);
        if (--unicode_pending_ == 0) {
            unsigned cp = 0;
            try {
                cp = static_cast<unsigned>(std::stoi(unicode_buf_, nullptr, 16));
            } catch (...) { cp = 0; }
            unicode_buf_.clear();
            if (mode_ == Mode::EmittingContent) {
                string utf8;
                if (cp < 0x80) {
                    utf8.push_back(static_cast<char>(cp));
                } else if (cp < 0x800) {
                    utf8.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
                    utf8.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                } else {
                    utf8.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
                    utf8.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    utf8.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
                content_ += utf8;
                if (emit_) emit_(utf8);
            }
        }
        return;
    }

    if (escape_next_) {
        escape_next_ = false;
        // Only process inside the content string. Elsewhere we just
        // swallow the escape to keep the state-machine simple.
        if (mode_ == Mode::EmittingContent) {
            string out;
            switch (c) {
                case '"':  out = "\""; break;
                case '\\': out = "\\"; break;
                case '/':  out = "/";  break;
                case 'n':  out = "\n"; break;
                case 't':  out = "\t"; break;
                case 'r':  out = "\r"; break;
                case 'b':  out = "\b"; break;
                case 'f':  out = "\f"; break;
                case 'u':
                    unicode_pending_ = 4;
                    unicode_buf_.clear();
                    return;
                default:   out.push_back(c); break;
            }
            content_ += out;
            if (emit_) emit_(out);
        }
        return;
    }

    if (in_string_) {
        if (c == '\\') { escape_next_ = true; return; }
        if (c == '"') {
            // End of a string. Three cases:
            //   1. We were streaming the content value — stop emitting.
            //   2. We just completed a key string — remember it for the
            //      next `:` so we know when content's value starts.
            //   3. Any other string (values, fallbacks).
            if (mode_ == Mode::EmittingContent) {
                mode_ = Mode::Idle;
            } else if (in_key_) {
                // pending_key_ was being built in the string; finalize it.
                // We mark in_key_ false below; the value discovery happens
                // when we see the `:`.
            }
            in_string_ = false;
            in_key_ = false;
            return;
        }
        // Inside a string literal.
        if (mode_ == Mode::EmittingContent) {
            string out(1, c);
            content_ += out;
            if (emit_) emit_(out);
        } else if (in_key_) {
            pending_key_.push_back(c);
        }
        return;
    }

    // Outside of any string literal.
    switch (c) {
        case '"':
            // The next string is a KEY iff we're inside an object and the
            // last non-whitespace char was `{` or `,`. Simpler heuristic:
            // at depth ≥ 1 with no pending colon, any opening string is
            // a key candidate. If after the string a `:` follows we know
            // it was a key. Track pending_key_ + see `:` case below.
            if (object_depth_ >= 1 && mode_ != Mode::EmittingContent) {
                in_key_ = true;
                pending_key_.clear();
            }
            in_string_ = true;
            return;
        case '{':
            object_depth_++;
            return;
        case '}':
            if (object_depth_ > 0) object_depth_--;
            return;
        case '[':
            array_depth_++;
            return;
        case ']':
            if (array_depth_ > 0) array_depth_--;
            return;
        case ':':
            // Only the ROOT-level "content" key triggers emission. Nested
            // steps[].command / .description live at object_depth_ >= 2,
            // so we ignore them — avoids false-positive extraction.
            if (object_depth_ == 1 && array_depth_ == 0
                    && pending_key_ == "content") {
                // The next `"..."` string we see is the content value.
                mode_ = Mode::EmittingContent;
            }
            pending_key_.clear();
            return;
        case ',':
            pending_key_.clear();
            return;
        default:
            return;
    }
}

// ═════════════════════════════════════════════════════════════════
// Structured-output streaming — reuses the per-provider streaming
// path but feeds the structured JSON body (for providers where stream
// is body-level) and the schema-enabled URL (for Gemini).
// ═════════════════════════════════════════════════════════════════

// Retry the streaming call on transient failures (429/5xx/connect) ONLY if
// no bytes of user-visible content have streamed yet. Once we've emitted
// anything to the terminal, retrying would duplicate output and confuse
// the user — at that point we hand the partial result up and stop.
// Structured-output streaming methods. All six collapse to "build a
// structured JSON body (with stream=true where applicable), pick the
// URL, call invoke_streaming / gemini_streaming_chain". The older
// retrying_stream_call wrapper and per-provider *_structured_stream_once
// helpers that lived here are gone — the generic invoke_streaming path
// above handles the retry-without-duplicate-output invariant for every
// provider.

LLMResponse GeminiClient::generate_structured_stream(
    const string &system_prompt,
    const string &user_prompt,
    std::function<void(const string &chunk)> on_chunk) {
    auto a = gemini_adapter(api_key_, connect_timeout_, read_timeout_, /*streaming=*/true);
    string body = build_gemini_structured_json(system_prompt, user_prompt);
    return gemini_streaming_chain(a, body, model_, fallback_models_, on_chunk,
                                   /*max_attempts=*/2);
}

LLMResponse GeminiClient::generate_structured_stream_with_context(
    const string &system_prompt,
    const vector<ConversationTurn> &history,
    const string &user_prompt,
    std::function<void(const string &chunk)> on_chunk) {
    auto a = gemini_adapter(api_key_, connect_timeout_, read_timeout_, /*streaming=*/true);
    string body = build_gemini_structured_context_json(system_prompt, history, user_prompt);
    return gemini_streaming_chain(a, body, model_, fallback_models_, on_chunk,
                                   /*max_attempts=*/2);
}

LLMResponse OpenAIClient::generate_structured_stream(
    const string &system_prompt,
    const string &user_prompt,
    std::function<void(const string &chunk)> on_chunk) {
    auto a = openai_adapter(api_key_, connect_timeout_, read_timeout_, /*streaming=*/true);
    string body = build_openai_structured_json(model_, system_prompt, user_prompt,
                                                /*stream=*/true);
    return invoke_streaming(a, kOpenAIChatUrl, body, model_, on_chunk,
                              /*max_attempts=*/2);
}

LLMResponse OpenAIClient::generate_structured_stream_with_context(
    const string &system_prompt,
    const vector<ConversationTurn> &history,
    const string &user_prompt,
    std::function<void(const string &chunk)> on_chunk) {
    auto a = openai_adapter(api_key_, connect_timeout_, read_timeout_, /*streaming=*/true);
    string body = build_openai_structured_context_json(model_, system_prompt, history,
                                                         user_prompt, /*stream=*/true);
    return invoke_streaming(a, kOpenAIChatUrl, body, model_, on_chunk,
                              /*max_attempts=*/2);
}

LLMResponse OllamaClient::generate_structured_stream(
    const string &system_prompt,
    const string &user_prompt,
    std::function<void(const string &chunk)> on_chunk) {
    string client_url = host_ + ":" + to_string(port_);
    auto a = ollama_adapter(client_url, connect_timeout_, read_timeout_, /*streaming=*/true);
    string body = build_ollama_structured_json(model_, system_prompt, user_prompt,
                                                /*stream=*/true);
    return invoke_streaming(a, client_url + "/api/chat", body, model_, on_chunk,
                              /*max_attempts=*/2);
}

LLMResponse OllamaClient::generate_structured_stream_with_context(
    const string &system_prompt,
    const vector<ConversationTurn> &history,
    const string &user_prompt,
    std::function<void(const string &chunk)> on_chunk) {
    string client_url = host_ + ":" + to_string(port_);
    auto a = ollama_adapter(client_url, connect_timeout_, read_timeout_, /*streaming=*/true);
    string body = build_ollama_structured_context_json(model_, system_prompt, history,
                                                         user_prompt, /*stream=*/true);
    return invoke_streaming(a, client_url + "/api/chat", body, model_, on_chunk,
                              /*max_attempts=*/2);
}

