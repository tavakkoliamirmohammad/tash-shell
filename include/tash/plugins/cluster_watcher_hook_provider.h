// Cluster watcher hook provider.
//
// On tash startup, the provider iterates the Registry's Running
// allocations and spawns one watcher thread per allocation. Each
// thread owns an IWatcher (interface below) whose run() method is
// responsible for opening the remote tail-F, decoding events, and
// applying them to the Registry + Notifier. When tash exits, the
// provider signals every IWatcher::stop() and joins the threads with
// a 2s backstop.
//
// The watcher is an injected dependency (a factory) rather than a
// concrete class so unit tests can verify lifecycle without needing
// real SSH. The production factory returns a minimal implementation
// today; M3.3 / M3.4 replace it with the real tail-F + event-decode
// loop.
//
// Thread safety: the provider is single-threaded from tash's point of
// view — on_startup / on_exit / on_*_command are all called serially
// by the plugin registry. Per-thread coordination is via each
// watcher's own stop flag.

#ifndef TASH_PLUGINS_CLUSTER_WATCHER_HOOK_PROVIDER_H
#define TASH_PLUGINS_CLUSTER_WATCHER_HOOK_PROVIDER_H

#include "tash/cluster/registry.h"
#include "tash/plugin.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace tash::cluster {

// Cooperative per-allocation watcher.
class IWatcher {
public:
    virtual ~IWatcher() = default;

    // Runs until stop() is observed. Expected to block; may perform I/O.
    virtual void run() = 0;

    // Asks the run() loop to exit soon. Safe to call from another thread.
    virtual void stop() = 0;
};

// Factory returns shared_ptr because a watcher's lifetime spans (at
// least) the provider's container, the running thread's lambda
// capture, and — for tests that observe post-stop state — the test
// fixture itself. A unique_ptr would force the thread to hold the
// only strong ref, making post-teardown inspection a UAF.
using WatcherFactory =
    std::function<std::shared_ptr<IWatcher>(const Allocation&, Registry&)>;

// Production factory — today returns a NoOpWatcher that idles until
// stopped; M3.3 extends it to open `ssh <cluster> tail -F …` and
// dispatch events via `apply_event`.
WatcherFactory default_watcher_factory();

class ClusterWatcherHookProvider : public IHookProvider {
public:
    ClusterWatcherHookProvider(Registry& reg, WatcherFactory factory);
    ~ClusterWatcherHookProvider() override;

    std::string name() const override { return "cluster-watcher"; }

    // Spawns one watcher thread per Running allocation.
    void on_startup(ShellState& state) override;

    // Signals every watcher to stop, then joins threads. Threads that
    // haven't exited within the join_backstop deadline are detached
    // (so a hung watcher can't block tash shutdown forever).
    void on_exit(ShellState& state) override;

    // Currently no-ops; reserved for M3.4 silence-heuristic bookkeeping
    // (on_after_command tracks the last user-visible prompt timestamp).
    void on_config_reload(ShellState&)                                         override {}
    void on_before_command(const std::string&, ShellState&)                    override {}
    void on_after_command (const std::string&, int, const std::string&, ShellState&) override {}

    // Tunable: backstop for on_exit thread-join. Public so tests can
    // shrink it for tight-feedback lifecycle assertions.
    std::chrono::milliseconds join_backstop{std::chrono::seconds{2}};

    // Diagnostics.
    std::size_t watcher_count() const { return watchers_.size(); }

private:
    // Stops every watcher and joins / detaches threads. Shared by
    // on_exit and the destructor, so the ShellState parameter is
    // optional here (the destructor doesn't have one to pass).
    void stop_and_join_all();

    Registry*      reg_;
    WatcherFactory factory_;

    // Each index i corresponds to the same thread/watcher pair. We
    // use shared_ptr<IWatcher> (not unique_ptr) so that a detached
    // watcher thread keeps its own IWatcher alive via a lambda
    // capture — preventing UAF if on_exit clears the provider's
    // reference while the detached thread is still running.
    std::vector<std::shared_ptr<IWatcher>> watchers_;
    std::vector<std::thread>               threads_;
};

}  // namespace tash::cluster

#endif  // TASH_PLUGINS_CLUSTER_WATCHER_HOOK_PROVIDER_H
