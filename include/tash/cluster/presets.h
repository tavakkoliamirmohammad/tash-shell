// Preset resolution for the cluster subsystem.
//
// resolve_preset() takes a Preset straight out of Config and produces a
// ResolvedPreset — the same information, but fully ready to hand to
// ClusterEngine::launch():
//
//   - command    : $VAR / ${VAR} expanded against the current environment
//   - env_vars   : parsed from preset.env_file (if set), as a key=value map
//   - stop_hook_path : absolute path to the hook script, or empty if none
//                       - "builtin:claude" → packaged claude-stop-hook.sh
//                       - absolute path    → passes through as-is
//                       - "builtin:<other>" OR non-absolute strings → error
//
// env_file sourcing is intentionally *parsing*, not *executing*:
//   - KEY=VALUE and "export KEY=VALUE" forms accepted
//   - VALUE may be bare, "double-quoted", or 'single-quoted'
//   - # comments and blank lines skipped
//   - no shell expansion, no command substitution
// This keeps the code safe and deterministic — if you need shell magic,
// source the file yourself before launching tash.

#ifndef TASH_CLUSTER_PRESETS_H
#define TASH_CLUSTER_PRESETS_H

#include "tash/cluster/types.h"

#include <filesystem>
#include <map>
#include <string>
#include <variant>

namespace tash::cluster {

struct ResolvedPreset {
    std::string name;
    std::string command;                             // post env-expansion
    std::map<std::string, std::string> env_vars;     // empty if no env_file
    std::string stop_hook_path;                      // absolute; empty if none
};

struct PresetResolveError {
    std::string message;                             // human-readable
};

using PresetResolveResult = std::variant<ResolvedPreset, PresetResolveError>;

PresetResolveResult resolve_preset(const Preset& preset);

// Parse a shell-style env file as a key=value map. Exposed for direct
// testing and reuse. Returns empty map if path does not exist or cannot
// be opened (no throw).
std::map<std::string, std::string> source_env_file(const std::filesystem::path& path);

}  // namespace tash::cluster

#endif  // TASH_CLUSTER_PRESETS_H
