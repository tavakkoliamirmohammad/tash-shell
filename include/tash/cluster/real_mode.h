// Real-cluster runtime wiring.
//
// Mirror of demo_mode.h for the production path: loads the user's
// TOML config + persisted registry, constructs the real ssh / slurm
// / tmux seams, and installs a ClusterEngine backed by them. Does
// not start any watcher thread by default — the hook provider is
// registered but uses the no-op WatcherFactory so we don't spawn
// `ssh tail -F` subprocesses the user hasn't opted into.
//
// Called from startup.cpp when no TASH_CLUSTER_DEMO override is set
// and the config file exists. On any config-load failure the engine
// is not installed and a one-line diagnostic is emitted to stderr;
// the cluster builtin then falls back to its
// "no cluster engine installed" message.
//
// Config path resolution (first match wins):
//   1. $TASH_CLUSTER_HOME/config.toml
//   2. $HOME/.tash/cluster/config.toml
//
// Registry + ssh socket dir live in the same parent as the config.

#ifndef TASH_CLUSTER_REAL_MODE_H
#define TASH_CLUSTER_REAL_MODE_H

namespace tash::cluster {

// Installs the real ClusterEngine if a config file is found and
// parses successfully. Returns true on success, false otherwise.
// Idempotent — a second call uninstalls first.
bool install_real_engine();

// Tears down the real ClusterEngine (if installed). Idempotent.
void uninstall_real_engine();

// True when the real engine is currently installed.
bool real_engine_installed();

}  // namespace tash::cluster

#endif  // TASH_CLUSTER_REAL_MODE_H
