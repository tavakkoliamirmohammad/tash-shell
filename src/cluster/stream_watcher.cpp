// StreamWatcher impl. See include/tash/cluster/stream_watcher.h.

#include "tash/cluster/stream_watcher.h"

#include <utility>

namespace tash::cluster {

StreamWatcher::StreamWatcher(LineSource source, Registry& reg, INotifier& notify)
    : source_(std::move(source)), on_stop_(nullptr),
      reg_(&reg), notify_(&notify) {}

StreamWatcher::StreamWatcher(LineSource source, OnStop on_stop,
                               Registry& reg, INotifier& notify)
    : source_(std::move(source)), on_stop_(std::move(on_stop)),
      reg_(&reg), notify_(&notify) {}

void StreamWatcher::run() {
    while (!stop_.load(std::memory_order_acquire)) {
        auto line = source_ ? source_() : std::nullopt;
        if (!line) break;              // source EOF => loop exits
        if (line->empty()) continue;

        auto e = EventDecoder::decode(*line);
        if (!e) continue;              // malformed JSON / missing fields
        if (!dedup_.admit(*e)) continue;

        // apply_event traverses Registry pointers and mutates instance
        // state; must not run concurrently with engine commands on the
        // main thread.
        auto guard = reg_->lock();
        apply_event(*e, *reg_, *notify_);
    }
    finished_.store(true, std::memory_order_release);
}

void StreamWatcher::stop() {
    // Set the flag first so a line that arrives between on_stop_() and
    // the next loop iteration doesn't get half-processed.
    stop_.store(true, std::memory_order_release);
    // Then unblock the source. For PipedLineSource this closes the
    // read fd, so a blocking ::read() returns 0 and next_line() yields
    // nullopt; run() then exits the loop and finished_ flips.
    if (on_stop_) on_stop_();
}

}  // namespace tash::cluster
