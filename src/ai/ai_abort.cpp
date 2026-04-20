// Definitions for the AI abort-flag atomics. The header makes all the
// accessors inline so the SIGINT handler can arm the flag without pulling
// in any non-async-signal-safe machinery.

#include "tash/ai/ai_abort.h"

namespace tash::ai::abort_flag {

std::atomic<bool> request_active{false};
std::atomic<bool> abort_requested{false};

} // namespace tash::ai::abort_flag
