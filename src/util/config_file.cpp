#include "tash/util/config_file.h"

#include "tash/util/config_resolver.h"
#include "tash/util/io.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace tash::config {
namespace {

// Process-wide copy of the config loaded at startup. Stored so the
// plugin-registration call site can consult `disabled_plugins` without
// re-reading the file, and so a future log-level consumer can pick up
// the stored value. Defaulted until set_loaded() runs.
UserConfig g_loaded;

std::string env_or_empty(const char *name) {
    const char *v = std::getenv(name);
    return (v && *v) ? std::string(v) : std::string();
}

// Apply TASH_LOG_LEVEL if set; overrides whatever was read from the
// file (per the spec).
void apply_env_overrides(UserConfig &cfg) {
    std::string env_lvl = env_or_empty("TASH_LOG_LEVEL");
    if (!env_lvl.empty()) {
        cfg.log_level = env_lvl;
    }
}

} // namespace

UserConfig load() {
    UserConfig cfg;

    const std::string path = get_data_dir() + "/config.json";
    std::ifstream in(path);
    if (!in.is_open()) {
        // Missing file is expected: silent defaults.
        tash::io::debug("config: no file at " + path + ", using defaults");
        apply_env_overrides(cfg);
        return cfg;
    }

    nlohmann::json j;
    try {
        in >> j;
    } catch (const std::exception &e) {
        // Malformed JSON: warn, keep defaults, don't abort. We use
        // std::cerr directly because there's no shared `io::warning`
        // helper yet — that'll come in a later PR.
        std::cerr << "tash: warning: failed to parse " << path << ": "
                  << e.what() << " (using defaults)\n";
        apply_env_overrides(cfg);
        return cfg;
    }

    // plugins.disabled ------------------------------------------------
    try {
        if (j.contains("plugins") && j["plugins"].is_object() &&
            j["plugins"].contains("disabled") &&
            j["plugins"]["disabled"].is_array()) {
            for (const auto &item : j["plugins"]["disabled"]) {
                if (item.is_string()) {
                    cfg.disabled_plugins.push_back(item.get<std::string>());
                }
            }
        }
    } catch (const std::exception &e) {
        std::cerr << "tash: warning: " << path
                  << ": invalid `plugins.disabled` (" << e.what()
                  << "), ignoring\n";
        cfg.disabled_plugins.clear();
    }

    // log_level -------------------------------------------------------
    try {
        if (j.contains("log_level") && j["log_level"].is_string()) {
            cfg.log_level = j["log_level"].get<std::string>();
        }
    } catch (const std::exception &e) {
        std::cerr << "tash: warning: " << path
                  << ": invalid `log_level` (" << e.what()
                  << "), using default\n";
        cfg.log_level = "info";
    }

    apply_env_overrides(cfg);

    // Summary of what we loaded — disabled list + resolved log level.
    std::string disabled_csv;
    for (size_t i = 0; i < cfg.disabled_plugins.size(); ++i) {
        if (i) disabled_csv += ",";
        disabled_csv += cfg.disabled_plugins[i];
    }
    tash::io::debug("config: loaded " + path +
                    " (plugins.disabled=[" + disabled_csv +
                    "], log_level=" + cfg.log_level + ")");
    return cfg;
}

const UserConfig& loaded() {
    return g_loaded;
}

void set_loaded(UserConfig cfg) {
    g_loaded = std::move(cfg);
}

} // namespace tash::config
