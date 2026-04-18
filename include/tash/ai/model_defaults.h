#ifndef TASH_AI_MODEL_DEFAULTS_H
#define TASH_AI_MODEL_DEFAULTS_H

// Provider default-model registry.
//
// The authoritative source is data/ai_models.json (bundled with the
// build). This header declares the accessors; the implementation in
// src/ai/model_defaults.cpp reads the JSON at first use, caches it
// for the process, and falls back to compiled-in defaults if the
// file is missing or malformed.
//
// Motivation: Google and OpenAI rotate their SKUs a few times a
// year. Keeping the model names out of C++ means a new release can
// ship updated defaults without touching any source file.

#include <string>
#include <vector>

namespace tash::ai {

// Primary model id for `provider` — empty if provider unknown.
const std::string& default_model_for(const std::string& provider);

// Fallback chain for when the primary returns 429 / 503 / 404.
// Empty for providers without a fallback chain (e.g. ollama).
const std::vector<std::string>& fallback_models_for(const std::string& provider);

// Prefixes that identify model IDs belonging to `provider`. Used by
// the @ai config menu's compatibility check to detect stale overrides
// written under a previous provider (e.g. "gpt-4o" with gemini set).
// Empty vector for providers without prefix discipline (ollama hosts
// user-chosen names like "llama3.2:3b").
const std::vector<std::string>& id_prefixes_for(const std::string& provider);

} // namespace tash::ai

#endif // TASH_AI_MODEL_DEFAULTS_H
