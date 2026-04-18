#include "tash/ai/model_defaults.h"

#include "tash/util/io.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace tash::ai {

// The embedded JSON lives in a build-time-generated TU produced by
// configure_file() from cmake/ai_models_embedded.cpp.in. Its sole
// job is to hold the bytes of data/ai_models.json as a raw string
// literal. data/ai_models.json is the single source of truth —
// bumping the file regenerates this symbol on the next build.
namespace detail {
extern const char* const kEmbeddedModelsJson;
} // namespace detail

namespace {

struct ProviderDefaults {
    std::string                primary;
    std::vector<std::string>   fallbacks;
    std::vector<std::string>   prefixes;
};

std::string read_env_file() {
    const char* p = std::getenv("TASH_AI_MODELS_JSON");
    if (!p || !*p) return {};
    std::ifstream in(p);
    if (!in.good()) return {};
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

bool parse_into(const std::string& text,
                std::unordered_map<std::string, ProviderDefaults>& out) {
    nlohmann::json j = nlohmann::json::parse(text);
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (!it.value().is_object()) continue;  // skip _comment etc.
        const auto& node = it.value();
        ProviderDefaults pd;
        if (node.contains("default") && node["default"].is_string()) {
            pd.primary = node["default"].get<std::string>();
        }
        if (node.contains("fallbacks") && node["fallbacks"].is_array()) {
            for (const auto& f : node["fallbacks"]) {
                if (f.is_string()) pd.fallbacks.push_back(f.get<std::string>());
            }
        }
        if (node.contains("id_prefixes") && node["id_prefixes"].is_array()) {
            for (const auto& f : node["id_prefixes"]) {
                if (f.is_string()) pd.prefixes.push_back(f.get<std::string>());
            }
        }
        if (!pd.primary.empty()) out[it.key()] = std::move(pd);
    }
    return true;
}

const std::unordered_map<std::string, ProviderDefaults>& registry() {
    static std::once_flag once;
    static std::unordered_map<std::string, ProviderDefaults> r;
    std::call_once(once, [] {
        // Env override (tests + ops post-install patching) takes first
        // crack. If it succeeds we never touch the embedded copy.
        std::string env_text = read_env_file();
        if (!env_text.empty()) {
            try {
                parse_into(env_text, r);
                io::debug("ai_models: loaded from TASH_AI_MODELS_JSON");
                return;
            } catch (const std::exception& e) {
                io::warning(std::string("ai_models: TASH_AI_MODELS_JSON "
                                          "parse failed — ") + e.what() +
                             ". Falling back to embedded defaults.");
                r.clear();
            }
        }
        // Normal path: parse the JSON that CMake embedded at build time.
        // The configure_file step guarantees the literal is the exact
        // contents of data/ai_models.json as of build time, so any parse
        // failure here is a build-system bug, not a user error.
        try {
            parse_into(detail::kEmbeddedModelsJson, r);
        } catch (const std::exception& e) {
            // Hard error — shipped binary's own data is malformed.
            io::error(std::string("ai_models: embedded JSON failed to "
                                    "parse — ") + e.what());
        }
    });
    return r;
}

const ProviderDefaults* lookup(const std::string& provider) {
    const auto& r = registry();
    auto it = r.find(provider);
    return it == r.end() ? nullptr : &it->second;
}

const std::string& empty_string() {
    static const std::string s;
    return s;
}

const std::vector<std::string>& empty_vec() {
    static const std::vector<std::string> v;
    return v;
}

} // namespace

const std::string& default_model_for(const std::string& provider) {
    const auto* pd = lookup(provider);
    return pd ? pd->primary : empty_string();
}

const std::vector<std::string>& fallback_models_for(const std::string& provider) {
    const auto* pd = lookup(provider);
    return pd ? pd->fallbacks : empty_vec();
}

const std::vector<std::string>& id_prefixes_for(const std::string& provider) {
    const auto* pd = lookup(provider);
    return pd ? pd->prefixes : empty_vec();
}

} // namespace tash::ai
