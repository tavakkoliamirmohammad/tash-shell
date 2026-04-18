#ifndef TASH_AI_LLM_DIAGNOSTICS_H
#define TASH_AI_LLM_DIAGNOSTICS_H

// Rich error diagnostics for LLM failures (O7.2).
//
// Every HTTP / transport failure in src/ai/llm_client.cpp funnels through
// this namespace so the output format stays consistent across Gemini,
// OpenAI and Ollama. The functions emit via the shared tash::io facility:
//   * transient, retryable failures use tash::io::warning;
//   * final failures (after retries exhausted, or non-retryable statuses)
//     use tash::io::error;
//   * request/response traces are gated behind TASH_LOG_LEVEL=debug.
//
// The helpers are declared here rather than kept static so unit tests
// can exercise the exact output format without having to stand up a TLS
// server. They are deliberately NOT part of the public LLMClient API.

#include <cstddef>
#include <string>

namespace tash::ai::diag {

// Human-readable reason phrase for the handful of HTTP statuses an LLM
// backend is likely to return. Unmapped statuses fall back to "HTTP error".
const char *http_reason_phrase(int status);

// Truncate `body` to at most `max_chars` characters, appending a
// "... [truncated]" marker when clipped. Exposed so the debug-mode
// body dump can be reproduced in tests.
std::string truncate_for_debug(const std::string &body, std::size_t max_chars = 500);

// HTTP-level failure: server reached, responded with non-2xx.
//   "<provider>: HTTP <status> <reason> (attempt <n>/<max>)[ - giving up]"
// On `final=true` the message is emitted at error severity; otherwise
// at warning, so transient retries don't drown out the final verdict.
// When `response_body` is non-empty it is also dumped at debug severity.
void log_http_failure(const std::string &provider,
                      int status,
                      int attempt,
                      int max_attempts,
                      bool final,
                      const std::string &response_body);

// Transport-level failure: connection refused, DNS failure, TLS error,
// body-size cap overflow, etc. The upstream curl message is forwarded
// verbatim. Same severity rules as log_http_failure.
void log_curl_failure(const std::string &provider,
                      const std::string &curl_message,
                      int attempt,
                      int max_attempts,
                      bool final);

// Debug-only: request pre-flight trace. Cheap no-op when the log level
// is above Debug; the message is built lazily only when it would be
// emitted.
void log_request_debug(const std::string &provider,
                       const std::string &model,
                       std::size_t body_bytes);

// Debug-only: success trace. Pairs with log_request_debug so operators
// can see total round-trip time and response size.
void log_response_debug(const std::string &provider,
                        int status,
                        std::size_t body_bytes,
                        long long elapsed_ms);

} // namespace tash::ai::diag

#endif // TASH_AI_LLM_DIAGNOSTICS_H
