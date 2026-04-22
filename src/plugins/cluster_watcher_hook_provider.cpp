// ClusterWatcherHookProvider — spawns one IWatcher thread per Running
// allocation at startup; signals + joins every thread at exit.
//
// Two factories ship from this TU: default_watcher_factory returns a
// NoOpWatcher (used when notifications are disabled or by tests that
// only want to exercise the lifecycle machinery); and
// make_ssh_tail_watcher_factory returns a StreamWatcher backed by a
// PipedLineSource running `ssh <host> bash -c '<tail -F>'` on the
// cluster login node.

#include "tash/plugins/cluster_watcher_hook_provider.h"

#include "tash/cluster/piped_line_source.h"
#include "tash/cluster/registry.h"
#include "tash/cluster/stream_watcher.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

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
    for (const auto& a : reg_->allocations()) {
        if (a.state != AllocationState::Running) continue;

        auto sp = factory_(a, *reg_);
        if (!sp) continue;   // factory opted out for this allocation

        watchers_.push_back(sp);
        // Capture `sp` by value so the watcher stays alive as long as
        // this thread is running — even if the provider later clears
        // its own references.
        threads_.emplace_back([sp]() {
            sp->run();
        });
    }
}

void ClusterWatcherHookProvider::on_exit(ShellState& /*state*/) {
    stop_and_join_all();
}

// Shared state between stop_and_join_all and each joiner helper.
// Heap-allocated (via shared_ptr) so that:
//   - waiter and joiner each hold a strong reference,
//   - state outlives the stack frame of stop_and_join_all even if we
//     end up detaching the joiner on timeout.
namespace {
struct JoinState {
    std::mutex              m;
    std::condition_variable cv;
    bool                    done{false};
};
}  // namespace

void ClusterWatcherHookProvider::stop_and_join_all() {
    // Signal every watcher to stop.
    for (auto& w : watchers_) {
        if (w) w->stop();
    }

    // Join threads. Any thread still running after join_backstop gets
    // detached so a hung watcher can't block tash shutdown.
    //
    // std::thread doesn't expose try_join, so the strategy is:
    //   1. Move the thread into a helper ("joiner") that calls join().
    //   2. Wait up to the remaining backstop via condition_variable.
    //   3. If the joiner finished, join it; else detach it.
    //
    // The joiner owns the underlying thread object, so after detach
    // there's no joinable std::thread sitting in threads_ waiting to
    // trigger std::terminate on destruction. The joiner also owns a
    // shared_ptr to the JoinState, so the mutex/cv/flag outlive the
    // stack frame of stop_and_join_all.
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + join_backstop;

    for (auto& t : threads_) {
        if (!t.joinable()) continue;

        const auto remaining = deadline - clock::now();
        if (remaining.count() <= 0) {
            t.detach();   // budget exhausted — give up on this one
            continue;
        }

        auto st = std::make_shared<JoinState>();

        std::thread joiner([st, tt = std::move(t)]() mutable {
            tt.join();
            {
                std::lock_guard<std::mutex> lk(st->m);
                st->done = true;
            }
            st->cv.notify_all();
        });

        bool finished;
        {
            std::unique_lock<std::mutex> lk(st->m);
            st->cv.wait_for(lk, remaining, [&]{ return st->done; });
            finished = st->done;
        }

        if (finished) {
            joiner.join();
        } else {
            // Underlying watcher thread is still running inside the
            // joiner. Detach the joiner — it owns both the thread and
            // the JoinState, so it will finish cleanly on its own.
            joiner.detach();
        }
    }

    // Every element of threads_ has been moved-from (joiner owns it) or
    // detached — safe to clear. Watchers are held by shared_ptr; any
    // detached thread still running has its own shared_ptr via lambda
    // capture and keeps its IWatcher alive.
    threads_.clear();
    watchers_.clear();
}

// ══════════════════════════════════════════════════════════════════════════════
// default_watcher_factory — NoOpWatcher for tests + notifications-off
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
    return [](const Allocation&, Registry&) -> std::shared_ptr<IWatcher> {
        return std::make_shared<NoOpWatcher>();
    };
}

WatcherFactory make_ssh_tail_watcher_factory(
        std::function<std::string(const Allocation&)> cluster_to_ssh_host,
        std::function<std::string(const Allocation&)> event_dir_for,
        INotifier& notifier) {
    // Notifier is owned by the caller (typically the engine); we only
    // capture a reference-like pointer. The factory closes over the
    // lambdas + pointer, which are cheap to copy.
    return [host = std::move(cluster_to_ssh_host),
            dir  = std::move(event_dir_for),
            n    = &notifier](const Allocation& a, Registry& reg)
               -> std::shared_ptr<IWatcher> {
        const std::string ssh_host   = host ? host(a) : std::string{};
        const std::string event_dir  = dir  ? dir(a)  : std::string{};
        if (ssh_host.empty() || event_dir.empty()) return nullptr;

        // Run a small bash loop on the remote that:
        //   1. Ensures the event dir exists (idempotent mkdir).
        //   2. Polls every 2s for any <ws>/<inst>.event file.
        //   3. Once at least one exists, execs `tail -qF` on the
        //      matched set. -qF -n +1 has its usual meaning:
        //        -q suppresses per-file banners,
        //        -F re-opens on rotate/truncate/recreate,
        //        -n +1 emits existing content from line 1.
        //
        // The two-level glob (<dir>/<workspace>/<instance>.event) is
        // what tash actually writes — earlier versions watched
        // <dir>/*.event (one level) and matched nothing.
        //
        // stderr is discarded so tail's pre-file-created noise doesn't
        // bubble back to the user's terminal every 2s.
        const std::string remote = std::string{} +
            "mkdir -p -- \"" + event_dir + "\" 2>/dev/null; "
            "while :; do "
            "  if ls \"" + event_dir + "\"/*/*.event >/dev/null 2>&1; then "
            "    exec tail -qF -n +1 \"" + event_dir + "\"/*/*.event 2>/dev/null; "
            "  fi; "
            "  sleep 2; "
            "done";
        std::vector<std::string> argv = {
            "ssh", ssh_host,
            "bash", "-c", remote,
        };
        auto proc = std::make_shared<PipedLineSource>(std::move(argv));
        auto src  = PipedLineSource::as_line_source(proc);

        // on_stop closes the PipedLineSource's read fd so a blocking
        // ::read() inside next_line() returns 0 immediately, letting
        // run() observe nullopt and exit. Without this StreamWatcher's
        // stop() would only flip an atomic the run loop never reads
        // (it's blocked in read()), and the provider's join backstop
        // would detach the thread — leaking thread + ssh subprocess.
        OnStop on_stop = [proc]() {
            if (proc) proc->stop();
        };
        auto watcher = std::make_shared<StreamWatcher>(
            std::move(src), std::move(on_stop), reg, *n);
        return watcher;
    };
}

}  // namespace tash::cluster
