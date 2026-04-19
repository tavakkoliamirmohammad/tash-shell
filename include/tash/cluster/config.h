// TOML config loader + validator for the cluster subsystem.
//
// Public interface:
//
//   ConfigLoader::load(path)              — load+validate from disk
//   ConfigLoader::load_from_string(src, path)
//                                          — same, but from an in-memory string
//                                            (path is used only for error reports)
//
// Both return ConfigLoadResult = std::variant<Config, ConfigError>.
//
// Error format matches tash's parser-error convention (commit 55faeba):
//   tash: cluster: <path>:<line>:<col>: <message>
// Lines and columns are 1-based, mirroring toml++'s source regions.
//
// Environment variable expansion ($VAR / ${VAR}) is applied to
// [defaults].workspace_base and each [[presets]].env_file — the two
// fields that most frequently reference host paths like "$HOME" or
// "$XDG_CONFIG_HOME".
//
// Cross-validation performed after parse:
//   - every [[resources]].routes[].cluster must name a declared [[clusters]]
//   - [[clusters]] and [[presets]] must each have their required fields
//   - resource kind must be "gpu" or "cpu"
//
// This file is pure declarations; see src/cluster/config.cpp for impl.

#ifndef TASH_CLUSTER_CONFIG_H
#define TASH_CLUSTER_CONFIG_H

#include "tash/cluster/types.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <variant>

namespace tash::cluster {

struct ConfigError {
    std::string path;     // file that failed (or "<memory>" for load_from_string)
    int         line = 0; // 1-based, 0 = unknown
    int         column = 0;
    std::string message;

    // Full diagnostic line ready to send to stderr.
    //   "tash: cluster: <path>:<line>:<col>: <message>"
    // If line/col are 0 they're omitted cleanly.
    std::string format() const;
};

using ConfigLoadResult = std::variant<Config, ConfigError>;

class ConfigLoader {
public:
    static ConfigLoadResult load(const std::filesystem::path& path);

    static ConfigLoadResult load_from_string(const std::string& source,
                                              const std::string& path = "<memory>");
};

// Linear-scan lookups over Config's vectors. Cheap enough at the scale of
// a single user's cluster list (O(10)). Return nullptr if not found.
const Cluster*  find_cluster (const Config&, std::string_view name);
const Resource* find_resource(const Config&, std::string_view name);
const Preset*   find_preset  (const Config&, std::string_view name);

}  // namespace tash::cluster

#endif  // TASH_CLUSTER_CONFIG_H
