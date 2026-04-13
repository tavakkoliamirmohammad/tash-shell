#ifdef TASH_AI_ENABLED

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include "tash/ai.h"
#include <string>

using namespace std;

// ── Minimal JSON helpers ──────────────────────────────────────

static string json_escape(const string &s) {
    string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c;
        }
    }
    return out;
}

static string build_request_json(const string &system_prompt, const string &user_prompt) {
    return "{"
        "\"system_instruction\":{\"parts\":[{\"text\":\"" + json_escape(system_prompt) + "\"}]},"
        "\"contents\":[{\"parts\":[{\"text\":\"" + json_escape(user_prompt) + "\"}]}]"
        "}";
}

static string extract_text_from_response(const string &json) {
    // Extract "text": "..." from Gemini response JSON
    string marker = "\"text\"";
    size_t pos = json.find(marker);
    if (pos == string::npos) return "";

    pos = json.find(':', pos);
    if (pos == string::npos) return "";
    pos = json.find('"', pos + 1);
    if (pos == string::npos) return "";
    pos++;

    string result;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            pos++;
            switch (json[pos]) {
                case 'n': result += '\n'; break;
                case 't': result += '\t'; break;
                case 'r': result += '\r'; break;
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                default: result += json[pos]; break;
            }
        } else {
            result += json[pos];
        }
        pos++;
    }
    return result;
}

// Extract error message from Gemini API error response
static string extract_text_from_error(const string &json) {
    // Look for "message": "..." in error responses
    string marker = "\"message\"";
    size_t pos = json.find(marker);
    if (pos == string::npos) return "";

    pos = json.find(':', pos);
    if (pos == string::npos) return "";
    pos = json.find('"', pos + 1);
    if (pos == string::npos) return "";
    pos++;

    string result;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            pos++;
            result += json[pos];
        } else {
            result += json[pos];
        }
        pos++;
    }
    return result;
}

// ── Models ────────────────────────────────────────────────────

static const char *MODELS[] = {
    "gemini-3.1-flash-lite-preview",
    "gemini-3-flash-preview",
};
static const int NUM_MODELS = 2;

// ── API call ──────────────────────────────────────────────────

static GeminiResponse call_gemini(const string &api_key,
                                   const string &model,
                                   const string &system_prompt,
                                   const string &user_prompt) {
    GeminiResponse resp;
    resp.success = false;
    resp.http_status = 0;

    httplib::Client cli("https://generativelanguage.googleapis.com");
    cli.set_connection_timeout(10);
    cli.set_read_timeout(30);

    string path = "/v1beta/models/" + string(model) + ":generateContent?key=" + api_key;
    string body = build_request_json(system_prompt, user_prompt);

    auto result = cli.Post(path, body, "application/json");

    if (!result) {
        resp.error_message = "couldn't reach Gemini API. Check your connection.";
        return resp;
    }

    resp.http_status = result->status;

    if (result->status == 200) {
        resp.text = extract_text_from_response(result->body);
        if (!resp.text.empty()) {
            resp.success = true;
        } else {
            resp.error_message = "unexpected response. Try again.";
        }
    } else if (result->status == 401) {
        resp.error_message = "invalid API key. Run @ai setup to fix.";
    } else if (result->status == 429) {
        resp.error_message = "rate limit reached. Try again in a moment.";
    } else if (result->status == 403) {
        resp.error_message = "daily quota reached. Try again tomorrow.";
    } else if (result->status == 404) {
        resp.error_message = "model_not_found";
    } else if (result->status == 400) {
        // 400 can mean bad model name or bad request — extract error message
        string api_msg = extract_text_from_error(result->body);
        if (api_msg.find("not found") != string::npos ||
            api_msg.find("is not supported") != string::npos) {
            resp.error_message = "model_not_found";
        } else {
            resp.error_message = "API error: " + (api_msg.empty() ? "bad request" : api_msg);
        }
    } else {
        resp.error_message = "API error (HTTP " + to_string(result->status) + "). Try again.";
    }

    return resp;
}

// ── Public API with model fallback ────────────────────────────

GeminiResponse gemini_generate(const string &api_key,
                                const string &system_prompt,
                                const string &user_prompt) {
    for (int i = 0; i < NUM_MODELS; i++) {
        GeminiResponse resp = call_gemini(api_key, MODELS[i], system_prompt, user_prompt);
        if (resp.success || resp.error_message != "model_not_found") {
            return resp;
        }
    }

    GeminiResponse resp;
    resp.success = false;
    resp.http_status = 404;
    resp.error_message = "AI model unavailable.";
    return resp;
}

#endif // TASH_AI_ENABLED
