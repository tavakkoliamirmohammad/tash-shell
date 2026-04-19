// ClusterWatcherHookProvider — spawns one IWatcher thread per Running
// allocation at startup; signals + joins every thread at exit.
//
// The production IWatcher is a thin NoOpWatcher for now; M3.3 / M3.4
// replace the factory with the real `ssh tail -F` + event-decode
// loop. The lifecycle machinery here is stable and covered by unit
// tests that drive the provider with FakeWatchers.

#include "tash/plugins/cluster_watcher_hook_provider.h"

#include "tash/cluster/registry.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace tash::cluster {

// ══════════════════════════════════════════════════════════════════════════════
// ClusterWatcherHookProvider
// ══════════════════════════════════════════════════════════════════════════════

ClusterWatcherHookProvider::ClusterWatcherHookProvider(Registry& reg,
                                                         WatcherFactory factory)
    : reg_(&reg), factory_(std::move(factory)) {}

ClusterWatcherHookProvider::~ClusterWatcherHookProvider() {
    // Safety net in case the caller never invoked on_exit.
    stop_and_join_all();
}

void ClusterWatcherHookProvider::on_startup(ShellState& /*state*/) {
    if (!reg_ || !factory_) return;
    for (const auto& a : reg_->allocations) {
        if (a.state != AllocationState::Running) continue;

        auto w = factory_(a, *reg_);
        if (!w) continue;   // factory opted out for this allocation

        IWatcher* raw = w.get();
        watchers_.push_back(std::move(w));
        threads_.emplace_back([raw]() {
            raw->run();
        });
    }
}

void ClusterWatcherHookProvider::on_exit(ShellState& /*state*/) {
    stop_and_join_all();
}

void ClusterWatcherHookProvider::stop_and_join_all() {
    // Signal every watcher to stop.
    for (auto& w : watchers_) {
        if (w) w->stop();
    }

    // Join threads. Any thread still running after join_backstop gets
    // detached so a hung watcher can't block tash shutdown.
    //
    // std::thread doesn't expose try_join, so the strategy is: spawn
    // a helper thread that does join(); wait up to the backstop via
    // condition_variable; if still not done, detach the original.
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + join_backstop;

    for (auto& t : threads_) {
        if (!t.joinable()) continue;

        // Best-effort: the FakeWatchers in tests observe stop almost
        // immediately, so this join returns fast. For production,
        // join_backstop gates it.
        const auto remaining = deadline - clock::now();
        if (remaining.count() <= 0) { t.detach(); continue; }

        // Hand the join to a helper that signals when done, then wait
        // on the helper with a timeout.
        std::mutex              m;
        std::condition_variable cv;
        std::atomic<bool>       done{false};

        std::thread joiner([&]() {
            t.join();
            {
                std::lock_guard<std::mutex> lk(m);
                done.store(true, std::memory_order_release);
            }
            cv.notify_all();
        });

        {
            std::unique_lock<std::mutex> lk(m);
            cv.wait_for(lk, remaining, [&]{ return done.load(std::memory_order_acquire); });
        }

        if (done.load(std::memory_order_acquire)) {
            joiner.join();
        } else {
            // Thread t is still running; we've used the backstop. Detach
            // both the watcher thread and the joiner (the joiner will
            // exit cleanly once t eventually finishes).
            joiner.detach();
        }
    }

    threads_.clear();
    watchers_.clear();
}

// ══════════════════════════════════════════════════════════════════════════════
// default_watcher_factory — NoOpWatcher placeholder (M3.3 replaces)
// ══════════════════════════════════════════════════════════════════════════════

namespace {

class NoOpWatcher : public IWatcher {
public:
    void run() override {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [this]{ return stop_.load(std::memory_order_acquire); });
    }

    void stop() override {
        stop_.store(true, std::memory_order_release);
        std::lock_guard<std::mutex> lk(m_);
        cv_.notify_all();
    }

private:
    std::mutex              m_;
    std::condition_variable cv_;
    std::atomic<bool>       stop_{false};
};

}  // namespace

WatcherFactory default_watcher_factory() {
    return [](const Allocation&, Registry&) -> std::unique_ptr<IWatcher> {
        return std::make_unique<NoOpWatcher>();
    };
}

}  // namespace tash::cluster
