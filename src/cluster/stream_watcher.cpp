// StreamWatcher impl. See include/tash/cluster/stream_watcher.h.

#include "tash/cluster/stream_watcher.h"

namespace tash::cluster {

StreamWatcher::StreamWatcher(LineSource source, Registry& reg, INotifier& notify)
    : source_(std::move(source)), reg_(&reg), notify_(&notify) {}

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
    stop_.store(true, std::memory_order_release);
}

}  // namespace tash::cluster
