// StreamWatcher — a real IWatcher implementation driven by an
// injectable LineSource. Works for:
//   - tests (inject a lambda that yields queued lines)
//   - production (ssh <cluster> tail -F piped through PipedLineSource)
//
// Per-line behaviour:
//   1. decode via EventDecoder — malformed lines skipped silently
//   2. dedup via EventDedup — repeated (workspace, instance, ts, kind) eaten
//   3. apply_event on Registry + INotifier — updates instance state,
//      fires notifier.desktop + .bell
//
// Shutdown: stop() sets an atomic flag AND invokes the owner-supplied
// on_stop callback (typically "close the ssh pipe"). Without the
// callback the run loop stays blocked inside source_() — a
// PipedLineSource's blocking read() won't unblock on its own, so the
// provider's join timeout would fire and a detached thread would leak
// the child process.

#ifndef TASH_CLUSTER_STREAM_WATCHER_H
#define TASH_CLUSTER_STREAM_WATCHER_H

#include "tash/cluster/notifier.h"
#include "tash/cluster/registry.h"
#include "tash/cluster/watcher.h"
#include "tash/plugins/cluster_watcher_hook_provider.h"

#include <atomic>
#include <functional>
#include <optional>
#include <string>

namespace tash::cluster {

// Returns the next line (blocking is OK) or nullopt when the source
// has definitively finished (EOF, or stop() was signalled and the
// source is now drained).
using LineSource = std::function<std::optional<std::string>()>;

// Called from stop() before the atomic flag is set — owner's chance
// to unblock a pending next_line() call (e.g. close the ssh pipe).
// May be nullptr for sources that naturally return nullopt (tests).
using OnStop = std::function<void()>;

class StreamWatcher : public IWatcher {
public:
    // Two-arg overload preserved for tests whose sources drain naturally.
    StreamWatcher(LineSource source, Registry& reg, INotifier& notify);
    // Production form. on_stop is called from stop() so a blocking
    // source can be unblocked deterministically.
    StreamWatcher(LineSource source, OnStop on_stop,
                    Registry& reg, INotifier& notify);

    void run()  override;
    void stop() override;

    // Test hook: true when the run loop has exited.
    bool finished() const { return finished_.load(); }

private:
    LineSource         source_;
    OnStop             on_stop_;
    Registry*          reg_;
    INotifier*         notify_;
    EventDedup         dedup_;
    std::atomic<bool>  stop_{false};
    std::atomic<bool>  finished_{false};
};

}  // namespace tash::cluster

#endif  // TASH_CLUSTER_STREAM_WATCHER_H
