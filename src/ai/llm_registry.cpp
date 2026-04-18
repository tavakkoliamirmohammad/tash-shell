// LLM provider factory registry — see include/tash/ai/llm_registry.h
// for the overall design rationale.

#ifdef TASH_AI_ENABLED

#include "tash/ai/llm_registry.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace tash::ai {

namespace {

// Meyers singleton — avoids static-init-order fiascos across TUs.
std::unordered_map<std::string, LLMFactory> &registry() {
    static std::unordered_map<std::string, LLMFactory> r;
    return r;
}

// Guard concurrent registration and lookup. Registration normally
// happens once at init, but we still want create_llm_client() from
// multiple threads to be well-defined.
std::mutex &registry_mutex() {
    static std::mutex m;
    return m;
}

} // namespace

void register_llm_provider(const std::string &name, LLMFactory factory) {
    std::lock_guard<std::mutex> lock(registry_mutex());
    registry()[name] = std::move(factory);
}

std::unique_ptr<LLMClient> create_llm_client(const std::string &name,
                                               const std::string &api_key) {
    LLMFactory factory;
    {
        std::lock_guard<std::mutex> lock(registry_mutex());
        auto it = registry().find(name);
        if (it == registry().end()) return nullptr;
        factory = it->second;
    }
    // Call the factory outside the lock — the factory may be slow
    // (allocations, network setup later) and we don't want to block
    // other lookups.
    return factory(api_key);
}

std::vector<std::string> registered_llm_providers() {
    std::vector<std::string> names;
    {
        std::lock_guard<std::mutex> lock(registry_mutex());
        names.reserve(registry().size());
        for (const auto &entry : registry()) {
            names.push_back(entry.first);
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

void register_builtin_llm_providers() {
    register_llm_provider("gemini", [](const std::string &key) -> std::unique_ptr<LLMClient> {
        return std::make_unique<GeminiClient>(key);
    });
    register_llm_provider("openai", [](const std::string &key) -> std::unique_ptr<LLMClient> {
        return std::make_unique<OpenAIClient>(key);
    });
    register_llm_provider("ollama", [](const std::string &url) -> std::unique_ptr<LLMClient> {
        return std::make_unique<OllamaClient>(url.empty() ? "http://localhost:11434" : url);
    });
}

} // namespace tash::ai

#endif // TASH_AI_ENABLED
