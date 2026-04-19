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
    for (auto& a : reg.allocations) {
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

}  // namespace tash::cluster
