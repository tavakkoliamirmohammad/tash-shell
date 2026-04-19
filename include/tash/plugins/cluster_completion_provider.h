// Tab completion for the `cluster` builtin.
//
// Knows how to complete every positional slot:
//   `cluster <TAB>`                      → subcommand list
//   `cluster up -r <TAB>`                → resource names from Config
//   `cluster up -r a100 --via <TAB>`     → cluster names
//   `cluster up <TAB>`                   → up-specific flags
//   `cluster launch --preset <TAB>`      → preset names
//   `cluster launch --workspace <TAB>`   → existing workspaces in registry
//   `cluster attach <TAB>`               → "<workspace>/<instance>" pairs
//   `cluster down <TAB>`                 → allocation-ids from registry
//   `cluster kill <TAB>`                 → same as attach
//   `cluster probe -r <TAB>`             → resource names
//   `cluster import --via <TAB>`         → cluster names
//
// When no ClusterEngine is active (no demo / no real wiring yet),
// dynamic completions gracefully degrade to just the subcommand list.

#ifndef TASH_PLUGINS_CLUSTER_COMPLETION_PROVIDER_H
#define TASH_PLUGINS_CLUSTER_COMPLETION_PROVIDER_H

#include "tash/plugin.h"

namespace tash::cluster {

class ClusterCompletionProvider : public ICompletionProvider {
public:
    std::string name() const override { return "cluster-completion"; }
    int         priority() const override { return 50; }

    bool can_complete(const std::string& command) const override;

    std::vector<Completion> complete(
        const std::string& command,
        const std::string& current_word,
        const std::vector<std::string>& args,
        const ShellState& state) const override;
};

}  // namespace tash::cluster

#endif  // TASH_PLUGINS_CLUSTER_COMPLETION_PROVIDER_H
