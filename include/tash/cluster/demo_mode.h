// TASH_CLUSTER_DEMO=1 runtime wiring.
//
// When tash starts with TASH_CLUSTER_DEMO=1 in the environment,
// startup.cpp calls install_demo_engine(), which constructs a
// ClusterEngine backed by minimal in-memory impls of all the seams:
//
//   - DemoSshClient  — every run() succeeds with empty stdout
//   - DemoSlurmOps   — sinfo reports plenty of idle a100 nodes,
//                       sbatch hands out monotonic jobids, squeue
//                       reflects current state, scancel removes
//   - DemoTmuxOps    — every op succeeds; windows are always alive
//   - DemoNotifier   — prints to stderr so the demo user can see
//                       notifications happened
//   - DemoPrompt     — answers 'k' (keep waiting) for every prompt
//   - DemoClock      — fake clock whose sleep_for advances the stored
//                       time instantly (no real sleep)
//
// Config is a single-cluster "demo-cluster" with one "a100" resource
// and a "demo-claude" preset that runs `echo 'hello from demo'`.
//
// This exists for two reasons: (1) a new user can kick the tires with
// zero credentials and zero cluster access, (2) the integration tests
// can drive the whole build against a predictable backend.

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
