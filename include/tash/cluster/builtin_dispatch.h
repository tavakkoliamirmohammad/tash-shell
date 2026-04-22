// Argv dispatcher for the `cluster` builtin.
//
// Factored out of src/builtins/cluster.cpp so unit tests can drive it
// directly against a test-constructed ClusterEngine, without having to
// plumb a full ShellState or write to real stdout/stderr.
//
// The active engine is set once at startup (by real_mode.cpp or
// demo_mode.cpp) and retrieved here via a module-local pointer rather
// than threading through ShellState.

#ifndef TASH_CLUSTER_BUILTIN_DISPATCH_H
#define TASH_CLUSTER_BUILTIN_DISPATCH_H

#include "tash/cluster/cluster_engine.h"

#include <ostream>
#include <string>
#include <vector>

namespace tash::cluster {

// Dispatch the `cluster` builtin against an engine.
//   argv[0] is the builtin name ("cluster").
//   argv[1] is the subcommand (connect / disconnect / up / launch /
//           attach / list / down / kill / sync / prune / doctor / help).
// Writes output to `out` / `err`; returns POSIX-style exit code.
int dispatch_cluster(const std::vector<std::string>& argv,
                      ClusterEngine& engine,
                      std::ostream& out,
                      std::ostream& err);

// Active engine — set by startup/demo wiring, consumed by builtin_cluster
// in src/builtins/cluster.cpp. nullptr means "no cluster engine installed"
// (-> the builtin prints a helpful message and returns non-zero).
void           set_active_engine(ClusterEngine* engine);
ClusterEngine* active_engine();

}  // namespace tash::cluster

#endif  // TASH_CLUSTER_BUILTIN_DISPATCH_H
