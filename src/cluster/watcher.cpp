// Watcher pure-logic impl. See include/tash/cluster/watcher.h for contract.

#include "tash/cluster/watcher.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <exception>
#include <string>

namespace tash::cluster {

using nlohmann::json;

// ══════════════════════════════════════════════════════════════════════════════
// EventDecoder
// ══════════════════════════════════════════════════════════════════════════════

std::optional<Event> EventDecoder::decode(std::string_view line) {
    json j;
    try {
        j = json::parse(line);
    } catch (const std::exception&) {
        return std::nullopt;
    }
    if (!j.is_object()) return std::nullopt;

    auto get_req_str = [&](const char* k) -> std::optional<std::string> {
        if (!j.contains(k) || !j[k].is_string()) return std::nullopt;
        return j[k].get<std::string>();
    };

    const auto ts       = get_req_str("ts");
    const auto instance = get_req_str("instance");
    const auto kind     = get_req_str("kind");
    if (!ts || !instance || !kind) return std::nullopt;

    // instance is the full "<workspace>/<name>" string; split.
    const auto slash = instance->find('/');
    if (slash == std::string::npos || slash == 0 || slash + 1 == instance->size()) {
        return std::nullopt;
    }

    Event e;
    e.ts        = *ts;
    e.workspace = instance->substr(0, slash);
    e.instance  = instance->substr(slash + 1);
    e.kind      = *kind;
    if (j.contains("detail") && j["detail"].is_string()) {
        e.detail = j["detail"].get<std::string>();
    }
    return e;
}

// ══════════════════════════════════════════════════════════════════════════════
// EventDedup
// ══════════════════════════════════════════════════════════════════════════════

namespace {
std::string dedup_key(const Event& e) {
    return e.workspace + "/" + e.instance + "@" + e.ts + "@" + e.kind;
}
}  // namespace

bool EventDedup::admit(const Event& e) {
    auto [_, inserted] = seen_.insert(dedup_key(e));
    return inserted;
}

// ══════════════════════════════════════════════════════════════════════════════
// state_for_kind
// ══════════════════════════════════════════════════════════════════════════════

std::optional<InstanceState> state_for_kind(std::string_view kind) {
    if (kind == "stopped")           return InstanceState::Stopped;
    if (kind == "idle")              return InstanceState::Idle;
    if (kind == "silence_threshold") return InstanceState::Idle;
    if (kind == "crashed")           return InstanceState::Crashed;
    if (kind == "window_exited")     return InstanceState::Exited;
    return std::nullopt;
}

// ══════════════════════════════════════════════════════════════════════════════
// apply_event
// ══════════════════════════════════════════════════════════════════════════════

namespace {

// Find the Instance matching (workspace, instance_id_or_name). Returns
// nullptr if not found. Sets cluster_out to the hosting allocation's
// cluster on success (for notification body).
Instance* find_instance(Registry& reg, const Event& e,
                          std::string& cluster_out) {
    for (auto& a : reg.mutable_allocations()) {
        for (auto& w : a.workspaces) {
            if (w.name != e.workspace) continue;
            for (auto& i : w.instances) {
                const bool id_match   = (i.id == e.instance);
                const bool name_match = i.name.has_value() && *i.name == e.instance;
                if (id_match || name_match) {
                    cluster_out = a.cluster;
                    return &i;
                }
            }
        }
    }
    return nullptr;
}

std::string humanised_title(std::string_view kind) {
    if (kind == "stopped")           return "Instance needs attention";
    if (kind == "idle")              return "Instance is idle";
    if (kind == "silence_threshold") return "Instance has been silent";
    if (kind == "crashed")           return "Instance crashed";
    if (kind == "window_exited")     return "Instance exited";
    return "Instance event";
}

}  // namespace

bool apply_event(const Event& e, Registry& reg, INotifier& notify) {
    std::string cluster;
    Instance* inst = find_instance(reg, e, cluster);
    if (!inst) return false;

    if (auto new_state = state_for_kind(e.kind)) {
        inst->state = *new_state;
    }
    inst->last_event_at = e.ts;

    std::string body = cluster + " · " + e.workspace + "/" + e.instance;
    if (!e.detail.empty()) body += " — " + e.detail;
    notify.desktop(humanised_title(e.kind), body);
    notify.bell();
    return true;
}

// ══════════════════════════════════════════════════════════════════════════════
// Backoff
// ══════════════════════════════════════════════════════════════════════════════

Backoff::Backoff(std::chrono::milliseconds base, std::chrono::milliseconds cap)
    : base_(base), cap_(cap), current_(base) {}

std::chrono::milliseconds Backoff::next() {
    const auto out = current_;
    total_        += out;
    current_       = std::min(current_ * 2, cap_);
    return out;
}

void Backoff::reset() {
    current_ = base_;
    total_   = std::chrono::milliseconds{0};
}

// ══════════════════════════════════════════════════════════════════════════════
// TmuxFallbackDetector — window-death + silence synthesis
// ══════════════════════════════════════════════════════════════════════════════

std::vector<Event> TmuxFallbackDetector::observe(
    const std::string& workspace,
    const std::vector<WindowSnapshot>& snapshot,
    std::chrono::steady_clock::time_point now,
    const std::string& now_ts) {

    std::vector<Event> out;

    // Mark-sweep flag reset; we'll mark each window present in `snapshot`.
    for (auto& [_, t] : tracks_) t.seen_this_round = false;

    // Sweep one: process current snapshot for silence + track updates.
    for (const auto& w : snapshot) {
        const std::string key = workspace + "/" + w.window;
        auto it = tracks_.find(key);

        if (it == tracks_.end()) {
            // First observation of this window — seed the track.
            Track t;
            t.pid             = w.pane_pid;
            t.first_seen_at   = now;
            t.silence_emitted = false;
            t.seen_this_round = true;
            tracks_[key]      = t;
            continue;
        }

        Track& t = it->second;
        t.seen_this_round = true;

        // Pid changed -> new streak.
        if (t.pid != w.pane_pid) {
            t.pid             = w.pane_pid;
            t.first_seen_at   = now;
            t.silence_emitted = false;
            continue;
        }

        // Same pid: maybe emit silence if over threshold and not yet sent.
        if (!t.silence_emitted && (now - t.first_seen_at) >= silence_threshold) {
            Event e;
            e.workspace = workspace;
            e.instance  = w.window;
            e.kind      = "silence_threshold";
            e.ts        = now_ts;
            out.push_back(std::move(e));
            t.silence_emitted = true;
        }
    }

    // Sweep two: any track not marked this round is a vanished window.
    for (auto it = tracks_.begin(); it != tracks_.end();) {
        if (it->second.seen_this_round) { ++it; continue; }

        // Extract workspace + window from key.
        const auto slash = it->first.find('/');
        Event e;
        e.workspace = it->first.substr(0, slash);
        e.instance  = slash == std::string::npos ? std::string{} : it->first.substr(slash + 1);
        e.kind      = "window_exited";
        e.ts        = now_ts;
        out.push_back(std::move(e));

        it = tracks_.erase(it);
    }

    return out;
}

}  // namespace tash::cluster
