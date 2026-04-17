#ifdef TASH_AI_ENABLED

#include "tash/llm_client.h"
#include "tash/ai.h"
#include <nlohmann/json.hpp>
#include <string>
#include <sstream>
#include <memory>
#include <cstdlib>
#include <cstddef>
#include <cstdio>

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
                                     const string &user_prompt) {
    json req;
    req["model"] = model;
    req["messages"] = json::array({
        {{"role", "system"}, {"content", system_prompt}},
        {{"role", "user"}, {"content", user_prompt}}
    });

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
                                             const string &user_prompt) {
    json req;
    req["model"] = model;

    json messages = json::array();
    messages.push_back({{"role", "system"}, {"content", system_prompt}});
    for (size_t i = 0; i < history.size(); i++) {
        messages.push_back({{"role", history[i].role}, {"content", history[i].text}});
    }
    messages.push_back({{"role", "user"}, {"content", user_prompt}});

    req["messages"] = messages;

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
                                     const string &user_prompt) {
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
    req["stream"] = false;
    req["format"] = "json";
    return req.dump();
}

string build_ollama_structured_context_json(const string &model,
                                             const string &system_prompt,
                                             const vector<ConversationTurn> &history,
                                             const string &user_prompt) {
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
    req["stream"] = false;
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

struct CurlPostResult {
    bool reached_server;   // false on connection failure
    int http_status;       // HTTP status when reached_server is true
    string body;           // response body when reached_server is true
    string error_message;  // set when reached_server is false
};

// Apply the security-critical + common transport options every HTTP call
// in this file needs. Returns true if every option was set successfully;
// returns false if an option failed — callers MUST fail closed because a
// missing TLS verification option on a libcurl built without TLS support
// would silently expose the shell to MitM.
static bool configure_common_curl_opts(CURL *curl,
                                       const std::string &url,
                                       const std::string &body,
                                       curl_slist *headers,
                                       long connect_timeout,
                                       long read_timeout) {
    auto check = [&](const char *name, CURLcode rc) -> bool {
        if (rc != CURLE_OK) {
            std::string msg = std::string("tash: curl_easy_setopt(") + name
                            + ") failed: " + curl_easy_strerror(rc) + "\n";
            std::fputs(msg.c_str(), stderr);
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
    // hosts via 3xx responses (Gemini key is in the URL query, OpenAI
    // bearer token is in the Authorization header).
    if (!check("CURLOPT_FOLLOWLOCATION",
               curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L))) return false;
    if (!check("CURLOPT_PROTOCOLS",
               curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS))) return false;
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
    return true;
}

static CurlPostResult curl_post(
    const string &url,
    const string &body,
    const vector<string> &extra_headers,
    int connect_timeout,
    int read_timeout)
{
    CurlPostResult out;
    out.reached_server = false;
    out.http_status = 0;

    CURL *curl = curl_easy_init();
    if (!curl) {
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
                                    static_cast<long>(read_timeout))) {
        out.error_message = "tls configuration failed";
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return out;
    }
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, tash_curl_buffer_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        if (ctx.overflowed) {
            out.error_message = "response exceeded TASH_AI_MAX_RESPONSE_BYTES ("
                              + std::to_string(tash_max_response_bytes())
                              + " bytes); aborted";
        } else {
            out.error_message = string("connection failed: ") + curl_easy_strerror(res);
        }
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

static LLMResponse curl_streaming_post(
    const string &url,
    const string &body,
    const vector<string> &extra_headers,
    function<void(const string &chunk)> on_chunk,
    function<string(const string &line)> parse_line,
    int connect_timeout,
    int read_timeout)
{
    LLMResponse resp;
    resp.success = false;
    resp.http_status = 0;

    CURL *curl = curl_easy_init();
    if (!curl) {
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
                                    static_cast<long>(read_timeout))) {
        resp.error_message = "tls configuration failed";
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return resp;
    }
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, tash_curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        if (ctx.overflowed) {
            resp.error_message = "response exceeded TASH_AI_MAX_RESPONSE_BYTES ("
                               + std::to_string(tash_max_response_bytes())
                               + " bytes); aborted";
        } else {
            resp.error_message = string("connection failed: ") + curl_easy_strerror(res);
        }
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
// Gemini error mapping
// ═════════════════════════════════════════════════════════════════

static string map_gemini_error(int status, const string &body) {
    switch (status) {
        case 401:
            return "invalid API key. Run @ai setup to fix.";
        case 429:
            return "rate limit reached. Try again in a moment.";
        case 403:
            return "daily quota reached. Try again tomorrow.";
        case 404:
            return "model_not_found";
        case 400: {
            string api_msg = extract_gemini_error(body);
            if (api_msg.find("not found") != string::npos ||
                api_msg.find("is not supported") != string::npos) {
                return "model_not_found";
            }
            return "API error: " + (api_msg.empty() ? "bad request" : api_msg);
        }
        default:
            return "API error (HTTP " + to_string(status) + "). Try again.";
    }
}

// ═════════════════════════════════════════════════════════════════
// GeminiClient implementation
// ═════════════════════════════════════════════════════════════════

GeminiClient::GeminiClient(const string &api_key)
    : api_key_(api_key),
      model_("gemini-3-flash-preview"),
      connect_timeout_(10),
      read_timeout_(30) {
    fallback_models_.push_back("gemini-2.5-flash");
}

void GeminiClient::set_model(const string &model) { model_ = model; }
string GeminiClient::get_model() const { return model_; }

LLMResponse GeminiClient::call_model(const string &model, const string &body) {
    LLMResponse resp;
    resp.success = false;
    resp.http_status = 0;

    string url = "https://generativelanguage.googleapis.com/v1beta/models/" +
                 model + ":generateContent?key=" + api_key_;

    auto result = curl_post(url, body, {}, connect_timeout_, read_timeout_);

    if (!result.reached_server) {
        resp.error_message = "couldn't reach Gemini API. Check your connection.";
        return resp;
    }

    resp.http_status = result.http_status;

    if (result.http_status == 200) {
        resp.text = extract_gemini_text(result.body);
        if (!resp.text.empty()) {
            resp.success = true;
        } else {
            resp.error_message = "unexpected response. Try again.";
        }
    } else {
        resp.error_message = map_gemini_error(result.http_status, result.body);
    }

    return resp;
}

// NOTE: streaming is real-time via libcurl's CURLOPT_WRITEFUNCTION
// which fires as data arrives from the server.

LLMResponse GeminiClient::call_model_stream(const string &model, const string &body,
                                             std::function<void(const string &chunk)> on_chunk) {
    string url = "https://generativelanguage.googleapis.com/v1beta/models/"
                 + model + ":streamGenerateContent?alt=sse&key=" + api_key_;

    auto parse_line = [](const string &line) -> string {
        if (line.size() > 6 && line.substr(0, 6) == "data: ") {
            return extract_gemini_text(line.substr(6));
        }
        return "";
    };

    LLMResponse resp = curl_streaming_post(url, body, {}, on_chunk, parse_line,
                                            connect_timeout_, read_timeout_);

    // Map errors using Gemini-specific error mapping
    if (!resp.success && resp.http_status != 0 && resp.http_status != 200) {
        resp.error_message = map_gemini_error(resp.http_status, "");
    }
    return resp;
}

LLMResponse GeminiClient::generate(const string &system_prompt, const string &user_prompt) {
    string body = build_gemini_request_json(system_prompt, user_prompt);

    for (int attempt = 0; attempt < 2; attempt++) {
        if (attempt > 0) retry_sleep();

        // Try primary model
        LLMResponse resp = call_model(model_, body);
        if (resp.success || resp.error_message != "model_not_found") {
            if (resp.success || !is_retryable(resp)) return resp;
            continue;
        }

        // Try fallback models
        for (size_t i = 0; i < fallback_models_.size(); i++) {
            resp = call_model(fallback_models_[i], body);
            if (resp.success || resp.error_message != "model_not_found") {
                if (resp.success || !is_retryable(resp)) return resp;
                break;
            }
        }
        if (resp.success || !is_retryable(resp)) return resp;
    }

    LLMResponse resp;
    resp.success = false;
    resp.http_status = 404;
    resp.error_message = "AI model unavailable.";
    return resp;
}

LLMResponse GeminiClient::generate_stream(const string &system_prompt, const string &user_prompt,
                                           std::function<void(const string &chunk)> on_chunk) {
    string body = build_gemini_request_json(system_prompt, user_prompt);

    LLMResponse resp = call_model_stream(model_, body, on_chunk);
    if (resp.success || resp.error_message != "model_not_found") {
        return resp;
    }

    for (size_t i = 0; i < fallback_models_.size(); i++) {
        resp = call_model_stream(fallback_models_[i], body, on_chunk);
        if (resp.success || resp.error_message != "model_not_found") {
            return resp;
        }
    }

    resp.success = false;
    resp.http_status = 404;
    resp.error_message = "AI model unavailable.";
    return resp;
}

LLMResponse GeminiClient::generate_with_context(const string &system_prompt,
                                                  const vector<ConversationTurn> &history,
                                                  const string &user_prompt) {
    string body = build_gemini_context_json(system_prompt, history, user_prompt);

    LLMResponse resp = call_model(model_, body);
    if (resp.success || resp.error_message != "model_not_found") {
        return resp;
    }

    for (size_t i = 0; i < fallback_models_.size(); i++) {
        resp = call_model(fallback_models_[i], body);
        if (resp.success || resp.error_message != "model_not_found") {
            return resp;
        }
    }

    resp.success = false;
    resp.http_status = 404;
    resp.error_message = "AI model unavailable.";
    return resp;
}

LLMResponse GeminiClient::generate_structured(const string &system_prompt,
                                                const string &user_prompt) {
    string body = build_gemini_structured_json(system_prompt, user_prompt);

    for (int attempt = 0; attempt < 2; attempt++) {
        if (attempt > 0) retry_sleep();

        LLMResponse resp = call_model(model_, body);
        if (resp.success || resp.error_message != "model_not_found") {
            if (resp.success || !is_retryable(resp)) return resp;
            continue;
        }

        for (size_t i = 0; i < fallback_models_.size(); i++) {
            resp = call_model(fallback_models_[i], body);
            if (resp.success || resp.error_message != "model_not_found") {
                if (resp.success || !is_retryable(resp)) return resp;
                break;
            }
        }
        if (resp.success || !is_retryable(resp)) return resp;
    }

    LLMResponse resp;
    resp.success = false;
    resp.http_status = 404;
    resp.error_message = "AI model unavailable.";
    return resp;
}

LLMResponse GeminiClient::generate_structured_with_context(
    const string &system_prompt,
    const vector<ConversationTurn> &history,
    const string &user_prompt) {
    string body = build_gemini_structured_context_json(system_prompt, history, user_prompt);

    LLMResponse resp = call_model(model_, body);
    if (resp.success || resp.error_message != "model_not_found") {
        return resp;
    }

    for (size_t i = 0; i < fallback_models_.size(); i++) {
        resp = call_model(fallback_models_[i], body);
        if (resp.success || resp.error_message != "model_not_found") {
            return resp;
        }
    }

    resp.success = false;
    resp.http_status = 404;
    resp.error_message = "AI model unavailable.";
    return resp;
}

// ═════════════════════════════════════════════════════════════════
// OpenAI error mapping
// ═════════════════════════════════════════════════════════════════

static string map_openai_error(int status, const string &body) {
    switch (status) {
        case 401:
            return "invalid API key. Run @ai setup to fix.";
        case 429:
            return "rate limit reached. Try again in a moment.";
        case 403:
            return "quota exceeded. Check your OpenAI billing.";
        case 404:
            return "model not found. Check your model name.";
        default: {
            string api_msg = extract_openai_error(body);
            if (!api_msg.empty()) {
                return "API error: " + api_msg;
            }
            return "API error (HTTP " + to_string(status) + "). Try again.";
        }
    }
}

// ═════════════════════════════════════════════════════════════════
// OpenAIClient implementation
// ═════════════════════════════════════════════════════════════════

OpenAIClient::OpenAIClient(const string &api_key)
    : api_key_(api_key),
      model_("gpt-4.1-nano"),
      connect_timeout_(10),
      read_timeout_(60) {}

void OpenAIClient::set_model(const string &model) { model_ = model; }
string OpenAIClient::get_model() const { return model_; }

LLMResponse OpenAIClient::generate(const string &system_prompt, const string &user_prompt) {
    LLMResponse resp;
    resp.success = false;
    resp.http_status = 0;

    string body = build_openai_request_json(model_, system_prompt, user_prompt, false);

    for (int attempt = 0; attempt < 2; attempt++) {
        if (attempt > 0) retry_sleep();

        vector<string> hdrs = {"Authorization: Bearer " + api_key_};
        auto result = curl_post(
            "https://api.openai.com/v1/chat/completions",
            body, hdrs, connect_timeout_, read_timeout_);

        if (!result.reached_server) {
            resp.error_message = "couldn't reach OpenAI API. Check your connection.";
            resp.http_status = 0;
            continue;
        }

        resp.http_status = result.http_status;

        if (result.http_status == 200) {
            resp.text = extract_openai_text(result.body);
            if (!resp.text.empty()) {
                resp.success = true;
            } else {
                resp.error_message = "unexpected response. Try again.";
            }
            return resp;
        }

        resp.error_message = map_openai_error(result.http_status, result.body);
        if (!is_retryable(resp)) return resp;
    }

    return resp;
}

LLMResponse OpenAIClient::generate_stream(const string &system_prompt, const string &user_prompt,
                                           std::function<void(const string &chunk)> on_chunk) {
    string url = "https://api.openai.com/v1/chat/completions";
    vector<string> hdrs = {"Authorization: Bearer " + api_key_};

    string body = build_openai_request_json(model_, system_prompt, user_prompt, true);

    auto parse_line = [](const string &line) -> string {
        if (line == "data: [DONE]") return "";
        if (line.size() > 6 && line.substr(0, 6) == "data: ") {
            try {
                json j = json::parse(line.substr(6));
                if (j.contains("choices") && j["choices"].is_array() &&
                    !j["choices"].empty() && j["choices"][0].contains("delta") &&
                    j["choices"][0]["delta"].contains("content")) {
                    return j["choices"][0]["delta"]["content"].get<string>();
                }
            } catch (const json::exception &) {}
        }
        return "";
    };

    return curl_streaming_post(url, body, hdrs, on_chunk, parse_line,
                                connect_timeout_, read_timeout_);
}

LLMResponse OpenAIClient::generate_with_context(const string &system_prompt,
                                                  const vector<ConversationTurn> &history,
                                                  const string &user_prompt) {
    LLMResponse resp;
    resp.success = false;
    resp.http_status = 0;

    string body = build_openai_context_json(model_, system_prompt, history, user_prompt, false);

    vector<string> hdrs = {"Authorization: Bearer " + api_key_};
    auto result = curl_post(
        "https://api.openai.com/v1/chat/completions",
        body, hdrs, connect_timeout_, read_timeout_);

    if (!result.reached_server) {
        resp.error_message = "couldn't reach OpenAI API. Check your connection.";
        return resp;
    }

    resp.http_status = result.http_status;

    if (result.http_status == 200) {
        resp.text = extract_openai_text(result.body);
        if (!resp.text.empty()) {
            resp.success = true;
        } else {
            resp.error_message = "unexpected response. Try again.";
        }
    } else {
        resp.error_message = map_openai_error(result.http_status, result.body);
    }

    return resp;
}

LLMResponse OpenAIClient::generate_structured(const string &system_prompt,
                                                const string &user_prompt) {
    LLMResponse resp;
    resp.success = false;
    resp.http_status = 0;

    string body = build_openai_structured_json(model_, system_prompt, user_prompt);

    for (int attempt = 0; attempt < 2; attempt++) {
        if (attempt > 0) retry_sleep();

        vector<string> hdrs = {"Authorization: Bearer " + api_key_};
        auto result = curl_post(
            "https://api.openai.com/v1/chat/completions",
            body, hdrs, connect_timeout_, read_timeout_);

        if (!result.reached_server) {
            resp.error_message = "couldn't reach OpenAI API. Check your connection.";
            resp.http_status = 0;
            continue;
        }

        resp.http_status = result.http_status;

        if (result.http_status == 200) {
            resp.text = extract_openai_text(result.body);
            if (!resp.text.empty()) {
                resp.success = true;
            } else {
                resp.error_message = "unexpected response. Try again.";
            }
            return resp;
        }

        resp.error_message = map_openai_error(result.http_status, result.body);
        if (!is_retryable(resp)) return resp;
    }

    return resp;
}

LLMResponse OpenAIClient::generate_structured_with_context(
    const string &system_prompt,
    const vector<ConversationTurn> &history,
    const string &user_prompt) {
    LLMResponse resp;
    resp.success = false;
    resp.http_status = 0;

    string body = build_openai_structured_context_json(model_, system_prompt, history, user_prompt);

    vector<string> hdrs = {"Authorization: Bearer " + api_key_};
    auto result = curl_post(
        "https://api.openai.com/v1/chat/completions",
        body, hdrs, connect_timeout_, read_timeout_);

    if (!result.reached_server) {
        resp.error_message = "couldn't reach OpenAI API. Check your connection.";
        return resp;
    }

    resp.http_status = result.http_status;

    if (result.http_status == 200) {
        resp.text = extract_openai_text(result.body);
        if (!resp.text.empty()) {
            resp.success = true;
        } else {
            resp.error_message = "unexpected response. Try again.";
        }
    } else {
        resp.error_message = map_openai_error(result.http_status, result.body);
    }

    return resp;
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

LLMResponse OllamaClient::generate(const string &system_prompt, const string &user_prompt) {
    LLMResponse resp;
    resp.success = false;
    resp.http_status = 0;

    string client_url = host_ + ":" + to_string(port_);
    string body = build_ollama_request_json(model_, system_prompt, user_prompt, false);

    for (int attempt = 0; attempt < 2; attempt++) {
        if (attempt > 0) retry_sleep();

        auto result = curl_post(client_url + "/api/chat", body, {},
                                 connect_timeout_, read_timeout_);

        if (!result.reached_server) {
            resp.error_message = "couldn't reach Ollama. Is it running? (" + client_url + ")";
            resp.http_status = 0;
            continue;
        }

        resp.http_status = result.http_status;

        if (result.http_status == 200) {
            resp.text = extract_ollama_text(result.body);
            if (!resp.text.empty()) {
                resp.success = true;
            } else {
                resp.error_message = "unexpected response from Ollama. Try again.";
            }
            return resp;
        }

        resp.error_message = "Ollama error (HTTP " + to_string(result.http_status) + "). Try again.";
        if (!is_retryable(resp)) return resp;
    }

    return resp;
}

LLMResponse OllamaClient::generate_stream(const string &system_prompt, const string &user_prompt,
                                            std::function<void(const string &chunk)> on_chunk) {
    string client_url = host_ + ":" + to_string(port_);
    string url = client_url + "/api/chat";

    string body = build_ollama_request_json(model_, system_prompt, user_prompt, true);

    auto parse_line = [](const string &line) -> string {
        if (line.empty()) return "";
        try {
            json j = json::parse(line);
            if (j.contains("message") && j["message"].contains("content")) {
                return j["message"]["content"].get<string>();
            }
        } catch (...) {}
        return "";
    };

    LLMResponse resp = curl_streaming_post(url, body, {}, on_chunk, parse_line,
                                            connect_timeout_, read_timeout_);
    if (!resp.success && resp.http_status == 0) {
        resp.error_message = "couldn't reach Ollama. Is it running? (" + client_url + ")";
    }
    return resp;
}

LLMResponse OllamaClient::generate_with_context(const string &system_prompt,
                                                  const vector<ConversationTurn> &history,
                                                  const string &user_prompt) {
    LLMResponse resp;
    resp.success = false;
    resp.http_status = 0;

    string client_url = host_ + ":" + to_string(port_);
    string body = build_ollama_context_json(model_, system_prompt, history, user_prompt, false);

    auto result = curl_post(client_url + "/api/chat", body, {},
                             connect_timeout_, read_timeout_);

    if (!result.reached_server) {
        resp.error_message = "couldn't reach Ollama. Is it running? (" + client_url + ")";
        return resp;
    }

    resp.http_status = result.http_status;

    if (result.http_status == 200) {
        resp.text = extract_ollama_text(result.body);
        if (!resp.text.empty()) {
            resp.success = true;
        } else {
            resp.error_message = "unexpected response from Ollama. Try again.";
        }
    } else {
        resp.error_message = "Ollama error (HTTP " + to_string(result.http_status) + "). Try again.";
    }

    return resp;
}

LLMResponse OllamaClient::generate_structured(const string &system_prompt,
                                                const string &user_prompt) {
    LLMResponse resp;
    resp.success = false;
    resp.http_status = 0;

    string client_url = host_ + ":" + to_string(port_);
    string body = build_ollama_structured_json(model_, system_prompt, user_prompt);

    for (int attempt = 0; attempt < 2; attempt++) {
        if (attempt > 0) retry_sleep();

        auto result = curl_post(client_url + "/api/chat", body, {},
                                 connect_timeout_, read_timeout_);

        if (!result.reached_server) {
            resp.error_message = "couldn't reach Ollama. Is it running? (" + client_url + ")";
            resp.http_status = 0;
            continue;
        }

        resp.http_status = result.http_status;

        if (result.http_status == 200) {
            resp.text = extract_ollama_text(result.body);
            if (!resp.text.empty()) {
                resp.success = true;
            } else {
                resp.error_message = "unexpected response from Ollama. Try again.";
            }
            return resp;
        }

        resp.error_message = "Ollama error (HTTP " + to_string(result.http_status) + "). Try again.";
        if (!is_retryable(resp)) return resp;
    }

    return resp;
}

LLMResponse OllamaClient::generate_structured_with_context(
    const string &system_prompt,
    const vector<ConversationTurn> &history,
    const string &user_prompt) {
    LLMResponse resp;
    resp.success = false;
    resp.http_status = 0;

    string client_url = host_ + ":" + to_string(port_);
    string body = build_ollama_structured_context_json(model_, system_prompt, history, user_prompt);

    auto result = curl_post(client_url + "/api/chat", body, {},
                             connect_timeout_, read_timeout_);

    if (!result.reached_server) {
        resp.error_message = "couldn't reach Ollama. Is it running? (" + client_url + ")";
        return resp;
    }

    resp.http_status = result.http_status;

    if (result.http_status == 200) {
        resp.text = extract_ollama_text(result.body);
        if (!resp.text.empty()) {
            resp.success = true;
        } else {
            resp.error_message = "unexpected response from Ollama. Try again.";
        }
    } else {
        resp.error_message = "Ollama error (HTTP " + to_string(result.http_status) + "). Try again.";
    }

    return resp;
}

// ═════════════════════════════════════════════════════════════════
// Factory function
// ═════════════════════════════════════════════════════════════════

std::unique_ptr<LLMClient> create_llm_client(const string &provider,
                                               const string &gemini_key,
                                               const string &openai_key,
                                               const string &ollama_url) {
    if (provider == "gemini") return std::make_unique<GeminiClient>(gemini_key);
    if (provider == "openai") return std::make_unique<OpenAIClient>(openai_key);
    if (provider == "ollama") return std::make_unique<OllamaClient>(ollama_url.empty() ? "http://localhost:11434" : ollama_url);
    return nullptr;
}

#endif // TASH_AI_ENABLED
