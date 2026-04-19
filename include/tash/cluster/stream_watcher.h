// StreamWatcher — a real IWatcher implementation driven by an
// injectable LineSource. Works for:
//   - tests (inject a lambda that yields queued lines)
//   - production (M3.3/M3.4 wires it to `ssh <cluster> tail -F
//      <event-dir>/**/*.event` via ISshClient)
//
// Per-line behaviour:
//   1. decode via EventDecoder — malformed lines skipped silently
//   2. dedup via EventDedup — repeated (workspace, instance, ts, kind) eaten
//   3. apply_event on Registry + INotifier — updates instance state,
//      fires notifier.desktop + .bell
//
// Shutdown: stop() sets an atomic flag and asks the LineSource for one
// more line; the run() loop checks the flag between reads and exits.
// Sources that may block forever (real tail -F) MUST become interruptible
// when stop() fires — typically by closing the underlying ssh pipe.

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

class StreamWatcher : public IWatcher {
public:
    StreamWatcher(LineSource source, Registry& reg, INotifier& notify);

    void run()  override;
    void stop() override;

    // Test hook: true when the run loop has exited.
    bool finished() const { return finished_.load(); }

private:
    LineSource         source_;
    Registry*          reg_;
    INotifier*         notify_;
    EventDedup         dedup_;
    std::atomic<bool>  stop_{false};
    std::atomic<bool>  finished_{false};
};

}  // namespace tash::cluster

#endif  // TASH_CLUSTER_STREAM_WATCHER_H
