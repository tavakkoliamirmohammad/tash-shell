#include "tash/ai/llm_diagnostics.h"
#include "tash/util/io.h"

#include <string>

namespace tash::ai::diag {

const char *http_reason_phrase(int status) {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 408: return "Request Timeout";
        case 413: return "Payload Too Large";
        case 422: return "Unprocessable Entity";
        case 429: return "Too Many Requests";
        case 500: return "Server Error";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        default:  return "HTTP error";
    }
}

std::string truncate_for_debug(const std::string &body, std::size_t max_chars) {
    if (body.size() <= max_chars) return body;
    return body.substr(0, max_chars) + "... [truncated]";
}

static void emit_failure(bool final, const std::string &msg) {
    if (final) {
        tash::io::error(msg);
    } else {
        tash::io::warning(msg);
    }
}

void log_http_failure(const std::string &provider,
                      int status,
                      int attempt,
                      int max_attempts,
                      bool final,
                      const std::string &response_body) {
    std::string msg = provider + ": HTTP " + std::to_string(status) + " "
                    + http_reason_phrase(status)
                    + " (attempt " + std::to_string(attempt)
                    + "/" + std::to_string(max_attempts) + ")";
    if (final) msg += " - giving up";
    emit_failure(final, msg);
    if (!response_body.empty()) {
        tash::io::debug(provider + ": response body: "
                        + truncate_for_debug(response_body));
    }
}

void log_curl_failure(const std::string &provider,
                      const std::string &curl_message,
                      int attempt,
                      int max_attempts,
                      bool final) {
    std::string msg = provider + ": curl error - " + curl_message
                    + " (attempt " + std::to_string(attempt)
                    + "/" + std::to_string(max_attempts) + ")";
    if (final) msg += " - giving up";
    emit_failure(final, msg);
}

void log_request_debug(const std::string &provider,
                       const std::string &model,
                       std::size_t body_bytes) {
    if (tash::io::current_log_level() > tash::io::Level::Debug) return;
    tash::io::debug(provider + ": POST " + model
                    + " (body " + std::to_string(body_bytes) + " bytes)");
}

void log_response_debug(const std::string &provider,
                        int status,
                        std::size_t body_bytes,
                        long long elapsed_ms) {
    if (tash::io::current_log_level() > tash::io::Level::Debug) return;
    tash::io::debug(provider + ": HTTP " + std::to_string(status)
                    + " (" + std::to_string(body_bytes) + " bytes, "
                    + std::to_string(elapsed_ms) + "ms)");
}

} // namespace tash::ai::diag
