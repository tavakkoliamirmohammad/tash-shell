#ifndef TASH_AI_AI_ABORT_H
#define TASH_AI_AI_ABORT_H

// Async-signal-safe abort flag for in-flight LLM requests.
//
// The SIGINT handler (src/core/signals.cpp) raises this flag unconditionally.
// curl's XFERINFOFUNCTION progress callback polls it and returns non-zero to
// abort the in-flight transfer, which is how we honor Ctrl+C during long AI
// calls without having to wait out the read timeout.
//
// Lifetime: the handler sets begin_ai_request() before each call and clears
// it with end_ai_request() after the call returns. That way, Ctrl+C outside
// an AI call does NOT arm the flag.

#include <atomic>

namespace tash::ai::abort_flag {

// True iff an AI request is currently in flight. Set/cleared by begin/end.
// Read by the SIGINT handler to decide whether to arm abort_requested.
extern std::atomic<bool> request_active;

// True iff an active request has been asked to stop (e.g. via Ctrl+C).
// Polled by curl progress callbacks. Cleared by begin_ai_request().
extern std::atomic<bool> abort_requested;

// Called by ai_handler / ai_error_hook before issuing an LLM call. Clears
// the abort flag and marks a request as active so SIGINT will arm it.
inline void begin_request() {
    abort_requested.store(false, std::memory_order_release);
    request_active.store(true, std::memory_order_release);
}

// Called after the LLM call returns. Unmarks the request so a subsequent
// out-of-request SIGINT does not set the flag for the next call.
inline void end_request() {
    request_active.store(false, std::memory_order_release);
}

// Poll — used by the curl progress callback. Cheap relaxed read.
inline bool should_abort() {
    return abort_requested.load(std::memory_order_acquire);
}

// Called from the SIGINT handler. Arms the flag only if a request is
// active; no-op otherwise. Async-signal-safe (atomic<bool> is lock-free).
inline void sigint_raise() {
    if (request_active.load(std::memory_order_acquire)) {
        abort_requested.store(true, std::memory_order_release);
    }
}

} // namespace tash::ai::abort_flag

static_assert(std::atomic<bool>::is_always_lock_free,
              "abort flag must be lock-free for async-signal-safety");

#endif // TASH_AI_AI_ABORT_H
