// `cluster` builtin — thin wrapper around tash::cluster::dispatch_cluster().
//
// The argv parser + engine dispatch live in src/cluster/builtin_dispatch.cpp
// so they can be unit-tested directly without plumbing ShellState or
// intercepting real stdout/stderr. This shim just:
//
//   1. Pulls the active ClusterEngine (set by startup / demo mode).
//   2. Feeds dispatch_cluster its argv + std::ostringstream capture.
//   3. Forwards captured output to tash's write_stdout / write_stderr.
//
// If no engine is installed, the user gets a one-line explanation.

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
                     "(start with TASH_CLUSTER_DEMO=1, or wait for M2 real-seam wiring)\n");
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
