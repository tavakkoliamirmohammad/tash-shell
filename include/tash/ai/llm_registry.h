#ifndef TASH_AI_LLM_REGISTRY_H
#define TASH_AI_LLM_REGISTRY_H

#ifdef TASH_AI_ENABLED

// LLM provider factory registry.
//
// Historically, create_llm_client() used a hard-coded if/else ladder to
// dispatch to GeminiClient / OpenAIClient / OllamaClient. Adding a new
// provider meant editing that ladder. The registry inverts the control:
// each provider registers a factory at AI-subsystem init via
// register_llm_provider(name, factory), and create_llm_client() becomes
// a thin wrapper around a lookup.
//
// The registry is stored in a function-local static map (Meyers
// singleton) so there's no static-init-order problem across translation
// units.

#include "tash/llm_client.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tash::ai {

// Factory: given a single string argument (API key for gemini/openai,
// endpoint URL for ollama), returns a concrete LLMClient.
using LLMFactory = std::function<std::unique_ptr<LLMClient>(const std::string &api_key)>;

// Register a provider factory under `name`. Overwrites any prior
// registration with the same name.
void register_llm_provider(const std::string &name, LLMFactory factory);

// Look up `name` in the registry and build a client. Returns nullptr if
// the provider isn't registered.
std::unique_ptr<LLMClient> create_llm_client(const std::string &name,
                                               const std::string &api_key);

// Returns the names of all registered providers, sorted alphabetically
// so callers can rely on a stable order (e.g. for help text / tests).
std::vector<std::string> registered_llm_providers();

// Registers gemini/openai/ollama. Safe to call more than once; a repeat
// call just re-installs the same factories. Invoked from ai_bootstrap
// at AI-subsystem init, and lazily from the legacy 4-arg
// ::create_llm_client() wrapper so unit tests that skip bootstrap still
// work.
void register_builtin_llm_providers();

} // namespace tash::ai

#endif // TASH_AI_ENABLED
#endif // TASH_AI_LLM_REGISTRY_H
