// Argv dispatcher for the `cluster` builtin.
//
// Factored out of src/builtins/cluster.cpp so unit tests can drive it
// directly against a test-constructed ClusterEngine, without having to
// plumb a full ShellState or write to real stdout/stderr.
//
// Plan drift: the original plan proposed adding a ClusterEngine slot to
// ShellState. That crosses a lot of module boundaries for little gain;
// instead we set / get the active engine via a module-local getter and
// keep ShellState untouched. builtin_cluster() in src/builtins/cluster.cpp
// pulls from active_engine() and forwards to dispatch_cluster().

#ifndef TASH_CLUSTER_BUILTIN_DISPATCH_H
#define TASH_CLUSTER_BUILTIN_DISPATCH_H

#include "tash/cluster/cluster_engine.h"

#include <ostream>
#include <string>
#include <vector>

namespace tash::cluster {

// Dispatch the `cluster` builtin against an engine.
//   argv[0] is the builtin name ("cluster").
//   argv[1] is the subcommand (up / launch / attach / list / down /
//           kill / sync / probe / import / help).
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
