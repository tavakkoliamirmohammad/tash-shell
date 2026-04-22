// TASH_CLUSTER_DEMO=1 runtime wiring — composes a ClusterEngine with
// in-memory Demo* impls of every seam (sbatch hands out monotonic
// jobids, tmux always "succeeds", notifier prints to stderr, etc.).
// Lets new users kick the tires with zero credentials and lets
// integration tests drive the whole build against a predictable
// backend. Config ships a single "demo-cluster" with one "a100"
// resource.

#ifndef TASH_CLUSTER_DEMO_MODE_H
#define TASH_CLUSTER_DEMO_MODE_H

#include "tash/cluster/builtin_dispatch.h"

namespace tash::cluster {

// Install (or re-install) the demo engine. Idempotent — any existing
// demo engine is torn down first. Calls set_active_engine() under the
// hood.
void install_demo_engine();

// Tear down the demo engine, if installed. After this, active_engine()
// returns nullptr.
void uninstall_demo_engine();

// Whether install_demo_engine() currently holds an engine.
bool demo_engine_installed();

// The Config shipped with the demo — exposed for tests that want to
// assert on its shape without installing the whole engine.
Config demo_config();

}  // namespace tash::cluster

#endif  // TASH_CLUSTER_DEMO_MODE_H
