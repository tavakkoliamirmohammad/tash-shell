// Watcher event decoding + dedup + state-transition logic.
//
// This header holds only the pure logic. The watcher lifecycle
// (spawning per-allocation tail-over-ssh threads, reconnect on
// disconnect, cleanup on tash exit) lives in the companion hook
// provider: include/tash/plugins/cluster_watcher_hook_provider.h
// and src/plugins/cluster_watcher_hook_provider.cpp.
//
// Event source of truth is the packaged stop-hook script
// (data/cluster/stop-hooks/claude-stop-hook.sh) and peer hooks. Each
// writes one JSON object per line:
//
//   {"ts":"<ISO-8601>","instance":"<workspace>/<instance>","kind":"stopped","detail":"…"}
//
// Kinds recognised:
//   "stopped"           — instance paused, awaiting user
//   "idle"              — instance idle (e.g. silence fallback)
//   "crashed"           — instance exited non-zero
//   "window_exited"     — tmux window's pid exited cleanly
//   "silence_threshold" — tmux silence fallback tripped
//
// Unknown kinds decode cleanly but don't produce a state transition.

#ifndef TASH_CLUSTER_WATCHER_H
#define TASH_CLUSTER_WATCHER_H

#include "tash/cluster/notifier.h"
#include "tash/cluster/registry.h"
#include "tash/cluster/types.h"

#include <atomic>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tash::cluster {

struct Event {
    std::string workspace;   // "repoA"
    std::string instance;    // "1" / "feature-x" (no workspace prefix)
    std::string kind;        // "stopped", "idle", "crashed", "window_exited", "silence_threshold"
    std::string ts;          // ISO-8601
    std::string detail;      // optional free-text
};

// Single-line JSON decoder. Returns nullopt on malformed JSON or missing
// required fields (ts, instance, kind).
class EventDecoder {
public:
    static std::optional<Event> decode(std::string_view line);
};

// Admits each (workspace, instance, ts, kind) tuple exactly once.
// The watcher thread uses this to swallow duplicate tail-F reads.
class EventDedup {
public:
    // Returns true if the event is new; false if we've already seen it.
    bool admit(const Event& e);
    std::size_t size()  const { return seen_.size(); }
    void        clear()       { seen_.clear(); }
private:
    std::unordered_set<std::string> seen_;
};

// Map a kind to the resulting InstanceState. Returns nullopt for unknown
// kinds (no state change) or for kinds that are notifications-only.
std::optional<InstanceState> state_for_kind(std::string_view kind);

// Apply a decoded event: locate the instance in the registry, update
// its state + last_event_at, fire desktop + bell notifications.
// Returns true when the event was applied; false if no matching
// instance was found in the registry (caller may log a debug line).
bool apply_event(const Event& e, Registry& reg, INotifier& notify);

// Exponential backoff helper for the watcher's reconnect loop.
// Sequence: base, 2*base, 4*base, …, cap (then cap, cap, …).
// abandon() returns true once cumulative delay >= `abandon_after`.
class Backoff {
public:
    explicit Backoff(std::chrono::milliseconds base =   std::chrono::milliseconds{1000},
                      std::chrono::milliseconds cap   =   std::chrono::milliseconds{30000});
    std::chrono::milliseconds next();          // return current delay, then grow
    void                       reset();
    std::chrono::milliseconds total() const { return total_; }
    bool                       should_abandon(
        std::chrono::milliseconds abandon_after = std::chrono::minutes{3}) const {
        return total_ >= abandon_after;
    }

private:
    std::chrono::milliseconds base_;
    std::chrono::milliseconds cap_;
    std::chrono::milliseconds current_;
    std::chrono::milliseconds total_{0};
};

// Minimal cooperative cancellation token for watcher threads.
class StopToken {
public:
    void request_stop()       { stopped_.store(true); }
    bool stopped()       const { return stopped_.load(); }
    void reset()                { stopped_.store(false); }
private:
    std::atomic<bool> stopped_{false};
};

// ══════════════════════════════════════════════════════════════════════════════
// TmuxFallbackDetector — when a Claude Stop hook isn't installed (or
// the instance isn't running Claude at all), we synthesise two kinds of
// events from coarse tmux state polling:
//
//   window_exited     — a window we saw before is no longer in the list
//   silence_threshold — a window's pane_pid has been stable for >=
//                        silence_threshold since first observation, and
//                        we haven't already emitted silence for this
//                        streak. Streak resets when pid changes.
//
// Caller feeds `observe(workspace, snapshot, now, now_ts)` per allocation
// per poll; emitted Events flow into the same apply_event pipeline used
// by the JSON stream watcher.
// ══════════════════════════════════════════════════════════════════════════════

struct WindowSnapshot {
    std::string session;    // e.g. "tash-utah-1234567-repoA"
    std::string window;     // e.g. "feature-x" or "1"
    long        pane_pid;   // from tmux list-windows -F '#{pane_pid}'
};

class TmuxFallbackDetector {
public:
    // How long a window's pid must stay stable before we declare silence.
    std::chrono::seconds silence_threshold{120};

    // Process a fresh snapshot of currently-alive windows. Returns
    // events that should be dispatched through apply_event.
    //   `workspace`  attached to each emitted Event.workspace
    //   `snapshot`   current windows (caller parses list-windows output)
    //   `now`        monotonic timestamp for staleness calculations
    //   `now_ts`     ISO-8601 string stamped on emitted Event.ts
    std::vector<Event> observe(const std::string& workspace,
                                 const std::vector<WindowSnapshot>& snapshot,
                                 std::chrono::steady_clock::time_point now,
                                 const std::string& now_ts);

private:
    struct Track {
        long                                       pid             = 0;
        std::chrono::steady_clock::time_point       first_seen_at{};
        bool                                        silence_emitted = false;
        bool                                        seen_this_round = false;   // mark-sweep flag
    };
    // Key: "<workspace>/<window>"
    std::unordered_map<std::string, Track> tracks_;
};

}  // namespace tash::cluster

#endif  // TASH_CLUSTER_WATCHER_H
