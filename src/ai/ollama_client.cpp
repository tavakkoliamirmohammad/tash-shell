#ifdef TASH_AI_ENABLED

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include "tash/ai.h"
#include <string>

using namespace std;

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
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

static string build_ollama_json(const string &model,
                                 const string &system_prompt,
                                 const string &user_prompt) {
    return "{"
        "\"model\":\"" + json_escape(model) + "\","
        "\"system\":\"" + json_escape(system_prompt) + "\","
        "\"prompt\":\"" + json_escape(user_prompt) + "\","
        "\"stream\":false"
        "}";
}

// Extract the value of a top-level string field like "response":"..."
// Handles JSON escape sequences in the value.
static string extract_string_field(const string &json, const string &field) {
    string marker = "\"" + field + "\"";
    size_t pos = json.find(marker);
    if (pos == string::npos) return "";

    pos = json.find(':', pos + marker.size());
    if (pos == string::npos) return "";
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos >= json.size() || json[pos] != '"') return "";
    pos++;

    string result;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            pos++;
            switch (json[pos]) {
                case 'n':  result += '\n'; break;
                case 't':  result += '\t'; break;
                case 'r':  result += '\r'; break;
                case '"':  result += '"';  break;
                case '\\': result += '\\'; break;
                case '/':  result += '/';  break;
                default:   result += json[pos]; break;
            }
        } else {
            result += json[pos];
        }
        pos++;
    }
    return result;
}

GeminiResponse ollama_generate(const string &system_prompt,
                                const string &user_prompt) {
    GeminiResponse resp;
    resp.success = false;
    resp.http_status = 0;

    string url = ai_get_ollama_url();
    string model = ai_get_ollama_model();

    httplib::Client cli(url.c_str());
    cli.set_connection_timeout(5);
    // Cold model loads can take a while; 120s is enough for most local
    // models while still failing in reasonable time if something is stuck.
    cli.set_read_timeout(120);

    string body = build_ollama_json(model, system_prompt, user_prompt);
    auto result = cli.Post("/api/generate", body, "application/json");

    if (!result) {
        resp.error_message = "couldn't reach Ollama at " + url +
                             ". Is `ollama serve` running?";
        return resp;
    }

    resp.http_status = result->status;

    if (result->status == 200) {
        string text = extract_string_field(result->body, "response");
        if (text.empty()) {
            string err = extract_string_field(result->body, "error");
            resp.error_message = err.empty()
                ? "unexpected response from Ollama."
                : "Ollama: " + err;
            return resp;
        }
        resp.text = text;
        resp.success = true;
    } else if (result->status == 404) {
        string err = extract_string_field(result->body, "error");
        resp.error_message = err.empty()
            ? "model '" + model + "' not found. Try: ollama pull " + model
            : "Ollama: " + err + ". Try: ollama pull " + model;
    } else {
        string err = extract_string_field(result->body, "error");
        resp.error_message = err.empty()
            ? "Ollama error (HTTP " + to_string(result->status) + ")."
            : "Ollama error: " + err;
    }

    return resp;
}

GeminiResponse ai_generate(const string &system_prompt,
                            const string &user_prompt,
                            const string &api_key) {
    if (ai_get_backend() == AI_BACKEND_OLLAMA) {
        return ollama_generate(system_prompt, user_prompt);
    }
    return gemini_generate(api_key, system_prompt, user_prompt);
}

#endif // TASH_AI_ENABLED
