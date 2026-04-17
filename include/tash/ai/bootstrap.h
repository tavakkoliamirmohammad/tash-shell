#ifndef TASH_AI_BOOTSTRAP_H
#define TASH_AI_BOOTSTRAP_H

// All AI-specific startup entry points. When built without
// TASH_AI_ENABLED these become no-op stubs so main.cpp stays free of
// #ifdefs around the call sites.

namespace tash {
namespace ai {

// Build the context-aware suggestion map from the recorded history file.
// Safe to call before the REPL starts; ignored when no history exists.
void build_history_context();

// Offer the AI setup wizard when the user has no provider configured.
// TTY-only; no-op when stdin is a pipe or the user is already set up.
void offer_setup_wizard();

} // namespace ai
} // namespace tash

#endif // TASH_AI_BOOTSTRAP_H
