#ifdef TASH_AI_ENABLED

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include "tash/llm_client.h"
#include "tash/ai.h"
#include <nlohmann/json.hpp>
#include <string>
#include <sstream>
#include <memory>

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
    } catch (...) {
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
    } catch (...) {
    }
    return "";
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
    } catch (...) {
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
    } catch (...) {
    }
    return "";
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
    } catch (...) {
    }
    return "";
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
      model_("gemini-2.5-flash"),
      connect_timeout_(10),
      read_timeout_(30) {
    fallback_models_.push_back("gemini-2.0-flash");
}

void GeminiClient::set_model(const string &model) { model_ = model; }
string GeminiClient::get_model() const { return model_; }

LLMResponse GeminiClient::call_model(const string &model, const string &body) {
    LLMResponse resp;
    resp.success = false;
    resp.http_status = 0;

    httplib::Client cli("https://generativelanguage.googleapis.com");
    cli.set_connection_timeout(connect_timeout_);
    cli.set_read_timeout(read_timeout_);

    string path = "/v1beta/models/" + model + ":generateContent?key=" + api_key_;

    auto result = cli.Post(path, body, "application/json");

    if (!result) {
        resp.error_message = "couldn't reach Gemini API. Check your connection.";
        return resp;
    }

    resp.http_status = result->status;

    if (result->status == 200) {
        resp.text = extract_gemini_text(result->body);
        if (!resp.text.empty()) {
            resp.success = true;
        } else {
            resp.error_message = "unexpected response. Try again.";
        }
    } else {
        resp.error_message = map_gemini_error(result->status, result->body);
    }

    return resp;
}

// NOTE: httplib does not support response content receivers on POST requests,
// so streaming responses are collected in full and then SSE-processed.
// The on_chunk callbacks fire during post-processing, not in real time.
// To get true token-by-token streaming, httplib would need to be replaced
// with a library that supports chunked response reading on POST (e.g. libcurl).

// Helper to process SSE data from a response body for Gemini streaming
static string process_gemini_sse(const string &body,
                                  std::function<void(const string &chunk)> on_chunk) {
    string accumulated_text;
    istringstream stream(body);
    string line;
    while (getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.size() > 6 && line.substr(0, 6) == "data: ") {
            string json_str = line.substr(6);
            string chunk_text = extract_gemini_text(json_str);
            if (!chunk_text.empty()) {
                accumulated_text += chunk_text;
                if (on_chunk) {
                    on_chunk(chunk_text);
                }
            }
        }
    }
    return accumulated_text;
}

LLMResponse GeminiClient::call_model_stream(const string &model, const string &body,
                                             std::function<void(const string &chunk)> on_chunk) {
    LLMResponse resp;
    resp.success = false;
    resp.http_status = 0;

    httplib::Client cli("https://generativelanguage.googleapis.com");
    cli.set_connection_timeout(connect_timeout_);
    cli.set_read_timeout(read_timeout_);

    string path = "/v1beta/models/" + model + ":streamGenerateContent?alt=sse&key=" + api_key_;

    auto result = cli.Post(path, body, "application/json");

    if (!result) {
        resp.error_message = "couldn't reach Gemini API. Check your connection.";
        return resp;
    }

    resp.http_status = result->status;

    if (result->status == 200) {
        string accumulated = process_gemini_sse(result->body, on_chunk);
        resp.text = accumulated;
        resp.success = !accumulated.empty();
        if (!resp.success) {
            resp.error_message = "unexpected response. Try again.";
        }
    } else {
        resp.error_message = map_gemini_error(result->status, result->body);
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
      model_("gpt-4o-mini"),
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

        httplib::Client cli("https://api.openai.com");
        cli.set_connection_timeout(connect_timeout_);
        cli.set_read_timeout(read_timeout_);

        httplib::Headers headers = {
            {"Authorization", "Bearer " + api_key_}
        };

        auto result = cli.Post("/v1/chat/completions", headers, body, "application/json");

        if (!result) {
            resp.error_message = "couldn't reach OpenAI API. Check your connection.";
            resp.http_status = 0;
            continue;
        }

        resp.http_status = result->status;

        if (result->status == 200) {
            resp.text = extract_openai_text(result->body);
            if (!resp.text.empty()) {
                resp.success = true;
            } else {
                resp.error_message = "unexpected response. Try again.";
            }
            return resp;
        }

        resp.error_message = map_openai_error(result->status, result->body);
        if (!is_retryable(resp)) return resp;
    }

    return resp;
}

// Helper to process SSE data from a response body for OpenAI streaming
static string process_openai_sse(const string &body,
                                  std::function<void(const string &chunk)> on_chunk) {
    string accumulated_text;
    istringstream stream(body);
    string line;
    while (getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line == "data: [DONE]") {
            break;
        }
        if (line.size() > 6 && line.substr(0, 6) == "data: ") {
            string json_str = line.substr(6);
            try {
                json j = json::parse(json_str);
                if (j.contains("choices") &&
                    j["choices"].is_array() &&
                    !j["choices"].empty() &&
                    j["choices"][0].contains("delta") &&
                    j["choices"][0]["delta"].contains("content")) {
                    string chunk_text = j["choices"][0]["delta"]["content"].get<string>();
                    if (!chunk_text.empty()) {
                        accumulated_text += chunk_text;
                        if (on_chunk) {
                            on_chunk(chunk_text);
                        }
                    }
                }
            } catch (...) {
            }
        }
    }
    return accumulated_text;
}

LLMResponse OpenAIClient::generate_stream(const string &system_prompt, const string &user_prompt,
                                           std::function<void(const string &chunk)> on_chunk) {
    LLMResponse resp;
    resp.success = false;
    resp.http_status = 0;

    httplib::Client cli("https://api.openai.com");
    cli.set_connection_timeout(connect_timeout_);
    cli.set_read_timeout(read_timeout_);

    httplib::Headers headers = {
        {"Authorization", "Bearer " + api_key_}
    };

    string body = build_openai_request_json(model_, system_prompt, user_prompt, true);

    auto result = cli.Post("/v1/chat/completions", headers, body, "application/json");

    if (!result) {
        resp.error_message = "couldn't reach OpenAI API. Check your connection.";
        return resp;
    }

    resp.http_status = result->status;

    if (result->status == 200) {
        string accumulated = process_openai_sse(result->body, on_chunk);
        resp.text = accumulated;
        resp.success = !accumulated.empty();
        if (!resp.success) {
            resp.error_message = "unexpected response. Try again.";
        }
    } else {
        resp.error_message = map_openai_error(result->status, result->body);
    }

    return resp;
}

LLMResponse OpenAIClient::generate_with_context(const string &system_prompt,
                                                  const vector<ConversationTurn> &history,
                                                  const string &user_prompt) {
    LLMResponse resp;
    resp.success = false;
    resp.http_status = 0;

    httplib::Client cli("https://api.openai.com");
    cli.set_connection_timeout(connect_timeout_);
    cli.set_read_timeout(read_timeout_);

    httplib::Headers headers = {
        {"Authorization", "Bearer " + api_key_}
    };

    string body = build_openai_context_json(model_, system_prompt, history, user_prompt, false);

    auto result = cli.Post("/v1/chat/completions", headers, body, "application/json");

    if (!result) {
        resp.error_message = "couldn't reach OpenAI API. Check your connection.";
        return resp;
    }

    resp.http_status = result->status;

    if (result->status == 200) {
        resp.text = extract_openai_text(result->body);
        if (!resp.text.empty()) {
            resp.success = true;
        } else {
            resp.error_message = "unexpected response. Try again.";
        }
    } else {
        resp.error_message = map_openai_error(result->status, result->body);
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
        } catch (...) {
            host = scheme + work;
        }
    } else {
        host = scheme + work;
    }
}

OllamaClient::OllamaClient(const string &endpoint_url)
    : model_("llama3.2"),
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

        httplib::Client cli(client_url);
        cli.set_connection_timeout(connect_timeout_);
        cli.set_read_timeout(read_timeout_);

        auto result = cli.Post("/api/chat", body, "application/json");

        if (!result) {
            resp.error_message = "couldn't reach Ollama. Is it running? (" + client_url + ")";
            resp.http_status = 0;
            continue;
        }

        resp.http_status = result->status;

        if (result->status == 200) {
            resp.text = extract_ollama_text(result->body);
            if (!resp.text.empty()) {
                resp.success = true;
            } else {
                resp.error_message = "unexpected response from Ollama. Try again.";
            }
            return resp;
        }

        resp.error_message = "Ollama error (HTTP " + to_string(result->status) + "). Try again.";
        if (!is_retryable(resp)) return resp;
    }

    return resp;
}

// Helper to process newline-delimited JSON from Ollama streaming response
static string process_ollama_ndjson(const string &body,
                                     std::function<void(const string &chunk)> on_chunk) {
    string accumulated_text;
    istringstream stream(body);
    string line;
    while (getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) continue;
        try {
            json j = json::parse(line);
            if (j.contains("message") &&
                j["message"].contains("content")) {
                string chunk_text = j["message"]["content"].get<string>();
                if (!chunk_text.empty()) {
                    accumulated_text += chunk_text;
                    if (on_chunk) {
                        on_chunk(chunk_text);
                    }
                }
            }
        } catch (...) {
        }
    }
    return accumulated_text;
}

LLMResponse OllamaClient::generate_stream(const string &system_prompt, const string &user_prompt,
                                            std::function<void(const string &chunk)> on_chunk) {
    LLMResponse resp;
    resp.success = false;
    resp.http_status = 0;

    string client_url = host_ + ":" + to_string(port_);
    httplib::Client cli(client_url);
    cli.set_connection_timeout(connect_timeout_);
    cli.set_read_timeout(read_timeout_);

    string body = build_ollama_request_json(model_, system_prompt, user_prompt, true);

    auto result = cli.Post("/api/chat", body, "application/json");

    if (!result) {
        resp.error_message = "couldn't reach Ollama. Is it running? (" + client_url + ")";
        return resp;
    }

    resp.http_status = result->status;

    if (result->status == 200) {
        string accumulated = process_ollama_ndjson(result->body, on_chunk);
        resp.text = accumulated;
        resp.success = !accumulated.empty();
        if (!resp.success) {
            resp.error_message = "unexpected response from Ollama. Try again.";
        }
    } else {
        resp.error_message = "Ollama error (HTTP " + to_string(result->status) + "). Try again.";
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
    httplib::Client cli(client_url);
    cli.set_connection_timeout(connect_timeout_);
    cli.set_read_timeout(read_timeout_);

    string body = build_ollama_context_json(model_, system_prompt, history, user_prompt, false);

    auto result = cli.Post("/api/chat", body, "application/json");

    if (!result) {
        resp.error_message = "couldn't reach Ollama. Is it running? (" + client_url + ")";
        return resp;
    }

    resp.http_status = result->status;

    if (result->status == 200) {
        resp.text = extract_ollama_text(result->body);
        if (!resp.text.empty()) {
            resp.success = true;
        } else {
            resp.error_message = "unexpected response from Ollama. Try again.";
        }
    } else {
        resp.error_message = "Ollama error (HTTP " + to_string(result->status) + "). Try again.";
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

// ═════════════════════════════════════════════════════════════════
// Backward compatibility shim
// ═════════════════════════════════════════════════════════════════

LLMResponse gemini_generate(const string &api_key,
                             const string &system_prompt,
                             const string &user_prompt) {
    GeminiClient client(api_key);
    return client.generate(system_prompt, user_prompt);
}

#endif // TASH_AI_ENABLED
