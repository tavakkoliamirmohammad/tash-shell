// Tests for the tash::ai::diag helpers that format LLM failure messages.
//
// These tests capture the stderr bytes written by tash::io:: to verify the
// exact diagnostic lines an end-user sees when an LLM call fails. The
// helpers are deliberately decoupled from libcurl so we can exercise the
// "HTTP 429" path without standing up a TLS server in CI.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <unistd.h>

#include "tash/ai/llm_diagnostics.h"
#include "tash/util/io.h"

namespace {

// RAII stderr redirector: dup2's a temp file over STDERR_FILENO for the
// lifetime of the scope, then returns the captured bytes. We use write(2)
// on STDERR_FILENO directly in tash::io::emit(), so gtest's own
// testing::internal::CaptureStderr() (which only redirects std::cerr)
// is not sufficient.
class StderrCapture {
public:
    StderrCapture() {
        char tmpl[] = "/tmp/tash_io_capture_XXXXXX";
        fd_ = ::mkstemp(tmpl);
        if (fd_ < 0) std::abort();
        path_ = tmpl;
        saved_ = ::dup(STDERR_FILENO);
        ::dup2(fd_, STDERR_FILENO);
    }
    ~StderrCapture() {
        if (saved_ >= 0) ::dup2(saved_, STDERR_FILENO);
        if (saved_ >= 0) ::close(saved_);
        if (fd_ >= 0) ::close(fd_);
        ::unlink(path_.c_str());
    }
    std::string read_all() {
        // Flush anything pending before reading back.
        ::fsync(fd_);
        ::lseek(fd_, 0, SEEK_SET);
        std::string out;
        char buf[4096];
        ssize_t n;
        while ((n = ::read(fd_, buf, sizeof(buf))) > 0) {
            out.append(buf, static_cast<size_t>(n));
        }
        return out;
    }
private:
    int fd_ = -1;
    int saved_ = -1;
    std::string path_;
};

// Scoped log-level guard: restores the previous level on destruction so
// one test's TASH_LOG_LEVEL=debug mode doesn't leak into the next.
class LogLevelGuard {
public:
    explicit LogLevelGuard(tash::io::Level l)
        : saved_(tash::io::current_log_level()) {
        tash::io::set_log_level(l);
    }
    ~LogLevelGuard() { tash::io::set_log_level(saved_); }
private:
    tash::io::Level saved_;
};

} // namespace

// ── log_http_failure ──────────────────────────────────────────────

TEST(LlmDiagnostics, Http429FinalContainsProviderAndStatus) {
    LogLevelGuard lvl(tash::io::Level::Info);
    StderrCapture cap;
    tash::ai::diag::log_http_failure("gemini", 429, 3, 3,
                                     /*final=*/true,
                                     /*response_body=*/"");
    std::string out = cap.read_all();

    EXPECT_NE(out.find("gemini:"), std::string::npos) << out;
    EXPECT_NE(out.find("HTTP 429"), std::string::npos) << out;
    EXPECT_NE(out.find("Too Many Requests"), std::string::npos) << out;
    EXPECT_NE(out.find("attempt 3/3"), std::string::npos) << out;
    EXPECT_NE(out.find("giving up"), std::string::npos) << out;
    EXPECT_NE(out.find("error"), std::string::npos)
        << "final failures should be emitted at error severity: " << out;
}

TEST(LlmDiagnostics, Http429RetryableIsWarningNotError) {
    LogLevelGuard lvl(tash::io::Level::Info);
    StderrCapture cap;
    tash::ai::diag::log_http_failure("openai", 429, 1, 3,
                                     /*final=*/false,
                                     /*response_body=*/"");
    std::string out = cap.read_all();

    EXPECT_NE(out.find("openai:"), std::string::npos) << out;
    EXPECT_NE(out.find("HTTP 429"), std::string::npos) << out;
    EXPECT_NE(out.find("attempt 1/3"), std::string::npos) << out;
    EXPECT_EQ(out.find("giving up"), std::string::npos)
        << "non-final attempts must not say 'giving up': " << out;
    EXPECT_NE(out.find("warning"), std::string::npos)
        << "transient retries should be warning severity: " << out;
}

TEST(LlmDiagnostics, ResponseBodyDumpGatedByDebugLevel) {
    const std::string body = R"({"error":{"message":"quota exceeded"}})";

    // At Info level the body line must NOT appear on stderr.
    {
        LogLevelGuard lvl(tash::io::Level::Info);
        StderrCapture cap;
        tash::ai::diag::log_http_failure("gemini", 403, 1, 1,
                                         /*final=*/true, body);
        std::string out = cap.read_all();
        EXPECT_EQ(out.find("quota exceeded"), std::string::npos)
            << "response body must be hidden at Info level: " << out;
    }

    // At Debug level it must appear, tagged with the provider name.
    {
        LogLevelGuard lvl(tash::io::Level::Debug);
        StderrCapture cap;
        tash::ai::diag::log_http_failure("gemini", 403, 1, 1,
                                         /*final=*/true, body);
        std::string out = cap.read_all();
        EXPECT_NE(out.find("quota exceeded"), std::string::npos)
            << "response body must be visible at Debug level: " << out;
        EXPECT_NE(out.find("gemini: response body:"), std::string::npos)
            << out;
    }
}

// ── log_curl_failure ──────────────────────────────────────────────

TEST(LlmDiagnostics, CurlFailureIncludesProviderAndUpstreamMessage) {
    LogLevelGuard lvl(tash::io::Level::Info);
    StderrCapture cap;
    tash::ai::diag::log_curl_failure("ollama",
                                     "Could not resolve host: localhost",
                                     2, 3, /*final=*/false);
    std::string out = cap.read_all();

    EXPECT_NE(out.find("ollama:"), std::string::npos) << out;
    EXPECT_NE(out.find("curl error"), std::string::npos) << out;
    EXPECT_NE(out.find("Could not resolve host"), std::string::npos) << out;
    EXPECT_NE(out.find("attempt 2/3"), std::string::npos) << out;
}

// ── debug traces ──────────────────────────────────────────────────

TEST(LlmDiagnostics, RequestDebugSilentWhenLogLevelAboveDebug) {
    LogLevelGuard lvl(tash::io::Level::Info);
    StderrCapture cap;
    tash::ai::diag::log_request_debug("gemini", "gemini-3-flash-preview", 1234);
    tash::ai::diag::log_response_debug("gemini", 200, 5678, 42);
    std::string out = cap.read_all();

    EXPECT_TRUE(out.empty())
        << "no diagnostic noise on success when not in debug mode, got: " << out;
}

TEST(LlmDiagnostics, RequestDebugEmittedAtDebugLevel) {
    LogLevelGuard lvl(tash::io::Level::Debug);
    StderrCapture cap;
    tash::ai::diag::log_request_debug("gemini", "gemini-3-flash-preview", 1234);
    tash::ai::diag::log_response_debug("gemini", 200, 5678, 42);
    std::string out = cap.read_all();

    EXPECT_NE(out.find("gemini: POST gemini-3-flash-preview"), std::string::npos)
        << out;
    EXPECT_NE(out.find("body 1234 bytes"), std::string::npos) << out;
    EXPECT_NE(out.find("HTTP 200"), std::string::npos) << out;
    EXPECT_NE(out.find("5678 bytes"), std::string::npos) << out;
    EXPECT_NE(out.find("42ms"), std::string::npos) << out;
}

// ── truncate_for_debug ────────────────────────────────────────────

TEST(LlmDiagnostics, TruncateForDebugLeavesShortBodiesUntouched) {
    EXPECT_EQ(tash::ai::diag::truncate_for_debug("hi", 500), "hi");
}

TEST(LlmDiagnostics, TruncateForDebugClipsLongBodies) {
    std::string body(800, 'x');
    std::string out = tash::ai::diag::truncate_for_debug(body, 500);
    EXPECT_EQ(out.size(), 500u + std::string("... [truncated]").size());
    EXPECT_NE(out.find("... [truncated]"), std::string::npos);
}

// ── http_reason_phrase ────────────────────────────────────────────

TEST(LlmDiagnostics, HttpReasonPhraseCoversCommonLlmStatuses) {
    EXPECT_STREQ(tash::ai::diag::http_reason_phrase(429), "Too Many Requests");
    EXPECT_STREQ(tash::ai::diag::http_reason_phrase(401), "Unauthorized");
    EXPECT_STREQ(tash::ai::diag::http_reason_phrase(500), "Server Error");
    EXPECT_STREQ(tash::ai::diag::http_reason_phrase(999), "HTTP error")
        << "unknown statuses should fall back to the generic phrase";
}
