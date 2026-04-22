// `cluster` builtin — thin forwarder to tash::cluster::dispatch_cluster.
// Pulls the active engine (set by startup), captures the dispatcher's
// stdout/stderr, and forwards through tash's write_* helpers. If no
// engine is installed the user gets a one-line remediation hint.

#include "tash/builtins.h"
#include "tash/core/signals.h"

#ifdef TASH_CLUSTER_ENABLED
  #include "tash/cluster/builtin_dispatch.h"
  #include <sstream>
#endif

int builtin_cluster(const std::vector<std::string> &argv, ShellState &) {
#ifdef TASH_CLUSTER_ENABLED
    auto* eng = tash::cluster::active_engine();
    if (!eng) {
        write_stderr("tash: cluster: no cluster engine installed "
                     "(create ~/.tash/cluster/config.toml for the real engine, "
                     "or set TASH_CLUSTER_DEMO=1 for the in-memory demo)\n");
        return 1;
    }
    std::ostringstream out, err;
    int rc = tash::cluster::dispatch_cluster(argv, *eng, out, err);
    const std::string so = out.str();
    const std::string se = err.str();
    if (!so.empty()) write_stdout(so);
    if (!se.empty()) write_stderr(se);
    return rc;
#else
    (void)argv;
    write_stderr("tash: cluster: built without cluster support\n");
    return 1;
#endif
}
