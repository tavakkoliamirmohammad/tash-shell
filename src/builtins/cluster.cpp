// `cluster` builtin — SLURM-backed remote launcher.
//
// This file currently ships only the entry-point stub so the command is
// registered and discoverable (via `help cluster`, completion, etc.) even
// before ClusterEngine lands in M1. The stub returns non-zero and tells
// the user why — either the feature is not yet implemented in this build
// (TASH_CLUSTER_ENABLED path), or tash was compiled with -DTASH_CLUSTER=OFF
// (the #else path).
//
// Implementation proper arrives in M1.10 when this dispatches to
// ClusterEngine via a ShellState slot.

#include "tash/builtins.h"
#include "tash/core/signals.h"

int builtin_cluster(const std::vector<std::string> &, ShellState &) {
#ifdef TASH_CLUSTER_ENABLED
    write_stderr("tash: cluster: not yet implemented\n");
#else
    write_stderr("tash: cluster: built without cluster support\n");
#endif
    return 1;
}
