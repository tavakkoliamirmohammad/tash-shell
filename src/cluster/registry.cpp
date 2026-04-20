// Registry persistence, CRUD, and reconciliation. See include/tash/cluster/registry.h
// for the contract.

#include "tash/cluster/registry.h"

#include <nlohmann/json.hpp>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <exception>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

namespace tash::cluster {

using nlohmann::json;

// ══════════════════════════════════════════════════════════════════════════════
// Ctor / dtor / move — keep Registry movable so `load` can return by value.
// The mutex itself is held via unique_ptr; moving transfers ownership.
// ══════════════════════════════════════════════════════════════════════════════

Registry::Registry()
    : mu_(std::make_unique<std::recursive_mutex>()) {}

Registry::Registry(Registry&&) noexcept = default;
Registry& Registry::operator=(Registry&&) noexcept = default;
Registry::~Registry() = default;

std::unique_lock<std::recursive_mutex> Registry::lock() const {
    // mu_ is always populated by the default ctor (load() returns by
    // value, which leaves mu_ owned in the destination). A moved-from
    // Registry has null mu_ and must not be used.
    return std::unique_lock<std::recursive_mutex>(*mu_);
}

// ══════════════════════════════════════════════════════════════════════════════
// Enum ↔ string  (single source of truth; used for both read and write).
// ══════════════════════════════════════════════════════════════════════════════

namespace {

std::string to_s(AllocationState s) {
    switch (s) {
        case AllocationState::Pending:     return "pending";
        case AllocationState::Running:     return "running";
        case AllocationState::Ended:       return "ended";
        case AllocationState::Unreachable: return "unreachable";
    }
    return "pending";
}

AllocationState alloc_from_s(std::string_view s) {
    if (s == "running")     return AllocationState::Running;
    if (s == "ended")       return AllocationState::Ended;
    if (s == "unreachable") return AllocationState::Unreachable;
    return AllocationState::Pending;
}

std::string to_s(InstanceState s) {
    switch (s) {
        case InstanceState::Running: return "running";
        case InstanceState::Idle:    return "idle";
        case InstanceState::Stopped: return "stopped";
        case InstanceState::Exited:  return "exited";
        case InstanceState::Crashed: return "crashed";
    }
    return "running";
}

InstanceState inst_from_s(std::string_view s) {
    if (s == "idle")    return InstanceState::Idle;
    if (s == "stopped") return InstanceState::Stopped;
    if (s == "exited")  return InstanceState::Exited;
    if (s == "crashed") return InstanceState::Crashed;
    return InstanceState::Running;
}

// ══════════════════════════════════════════════════════════════════════════════
// JSON <-> struct  (separated by level so save/load mirror each other).
// ══════════════════════════════════════════════════════════════════════════════

json instance_to_json(const Instance& i) {
    json j;
    j["id"]            = i.id;
    j["name"]          = i.name ? json(*i.name) : json(nullptr);
    j["tmux_window"]   = i.tmux_window;
    j["pid"]           = i.pid  ? json(*i.pid)  : json(nullptr);
    j["state"]         = to_s(i.state);
    j["last_event_at"] = i.last_event_at;
    return j;
}

Instance instance_from_json(const json& j) {
    Instance i;
    i.id          = j.value("id", std::string{});
    i.tmux_window = j.value("tmux_window", std::string{});
    if (j.contains("name") && !j["name"].is_null())
        i.name = j["name"].get<std::string>();
    if (j.contains("pid") && !j["pid"].is_null())
        i.pid = j["pid"].get<std::int64_t>();
    if (j.contains("state"))
        i.state = inst_from_s(j["state"].get<std::string>());
    i.last_event_at = j.value("last_event_at", std::string{});
    return i;
}

json workspace_to_json(const Workspace& w) {
    json j;
    j["name"]         = w.name;
    j["cwd"]          = w.cwd;
    j["tmux_session"] = w.tmux_session;
    json arr = json::array();
    for (const auto& i : w.instances) arr.push_back(instance_to_json(i));
    j["instances"] = arr;
    return j;
}

Workspace workspace_from_json(const json& j) {
    Workspace w;
    w.name         = j.value("name", std::string{});
    w.cwd          = j.value("cwd",  std::string{});
    w.tmux_session = j.value("tmux_session", std::string{});
    if (j.contains("instances") && j["instances"].is_array()) {
        for (const auto& ij : j["instances"]) w.instances.push_back(instance_from_json(ij));
    }
    return w;
}

json allocation_to_json(const Allocation& a) {
    json j;
    j["id"]           = a.id;
    j["cluster"]      = a.cluster;
    j["jobid"]        = a.jobid;
    j["resource"]     = a.resource;
    j["node"]         = a.node;
    j["submitted_at"] = a.submitted_at;
    j["started_at"]   = a.started_at;
    j["end_by"]       = a.end_by;
    j["state"]        = to_s(a.state);
    json arr = json::array();
    for (const auto& w : a.workspaces) arr.push_back(workspace_to_json(w));
    j["workspaces"] = arr;
    return j;
}

Allocation allocation_from_json(const json& j) {
    Allocation a;
    a.id           = j.value("id",           std::string{});
    a.cluster      = j.value("cluster",      std::string{});
    a.jobid        = j.value("jobid",        std::string{});
    a.resource     = j.value("resource",     std::string{});
    a.node         = j.value("node",         std::string{});
    a.submitted_at = j.value("submitted_at", std::string{});
    a.started_at   = j.value("started_at",   std::string{});
    a.end_by       = j.value("end_by",       std::string{});
    if (j.contains("state"))
        a.state = alloc_from_s(j["state"].get<std::string>());
    if (j.contains("workspaces") && j["workspaces"].is_array()) {
        for (const auto& wj : j["workspaces"]) a.workspaces.push_back(workspace_from_json(wj));
    }
    return a;
}

std::int64_t now_unix_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

}  // namespace

// ══════════════════════════════════════════════════════════════════════════════
// Registry::load  —  tolerant: missing file = empty; bad JSON = back up + empty.
// ══════════════════════════════════════════════════════════════════════════════

Registry Registry::load(const std::filesystem::path& path) {
    Registry r;

    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return r;
    }

    std::ifstream f(path);
    if (!f.is_open()) {
        return r;
    }
    std::ostringstream buf;
    buf << f.rdbuf();

    json j;
    try {
        j = json::parse(buf.str());
    } catch (const std::exception&) {
        // Corrupt file — rename to a timestamped backup and start fresh.
        auto bak = path;
        bak += ".bak." + std::to_string(now_unix_seconds());
        std::error_code rec;
        std::filesystem::rename(path, bak, rec);
        return r;
    }

    if (!j.is_object()) return r;

    r.schema_version = j.value("schema_version", 1);
    if (j.contains("allocations") && j["allocations"].is_array()) {
        for (const auto& aj : j["allocations"]) {
            r.allocations.push_back(allocation_from_json(aj));
        }
    }
    return r;
}

// ══════════════════════════════════════════════════════════════════════════════
// Registry::save  —  atomic: write to <path>.tmp, then rename.
// ══════════════════════════════════════════════════════════════════════════════

void Registry::save(const std::filesystem::path& path) const {
    std::lock_guard<std::recursive_mutex> lk(*mu_);
    json j;
    j["schema_version"] = schema_version;
    json arr = json::array();
    for (const auto& a : allocations) arr.push_back(allocation_to_json(a));
    j["allocations"] = arr;

    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream f(tmp);
        f << j.dump(2);
    }

    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        // Best-effort cleanup; caller's registry will be out of sync.
        std::filesystem::remove(tmp, ec);
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// CRUD
// ══════════════════════════════════════════════════════════════════════════════

void Registry::add_allocation(Allocation a) {
    std::lock_guard<std::recursive_mutex> lk(*mu_);
    allocations.push_back(std::move(a));
}

bool Registry::remove_allocation(std::string_view id) {
    std::lock_guard<std::recursive_mutex> lk(*mu_);
    for (auto it = allocations.begin(); it != allocations.end(); ++it) {
        if (it->id == id) { allocations.erase(it); return true; }
    }
    return false;
}

Allocation* Registry::find_allocation(std::string_view id) {
    std::lock_guard<std::recursive_mutex> lk(*mu_);
    for (auto& a : allocations) if (a.id == id) return &a;
    return nullptr;
}

const Allocation* Registry::find_allocation(std::string_view id) const {
    std::lock_guard<std::recursive_mutex> lk(*mu_);
    for (const auto& a : allocations) if (a.id == id) return &a;
    return nullptr;
}

bool Registry::add_workspace(std::string_view alloc_id, Workspace ws) {
    std::lock_guard<std::recursive_mutex> lk(*mu_);
    if (auto* a = find_allocation(alloc_id)) {
        a->workspaces.push_back(std::move(ws));
        return true;
    }
    return false;
}

bool Registry::remove_workspace(std::string_view alloc_id, std::string_view ws_name) {
    std::lock_guard<std::recursive_mutex> lk(*mu_);
    auto* a = find_allocation(alloc_id);
    if (!a) return false;
    for (auto it = a->workspaces.begin(); it != a->workspaces.end(); ++it) {
        if (it->name == ws_name) { a->workspaces.erase(it); return true; }
    }
    return false;
}

bool Registry::add_instance(std::string_view alloc_id, std::string_view ws_name, Instance inst) {
    std::lock_guard<std::recursive_mutex> lk(*mu_);
    auto* a = find_allocation(alloc_id);
    if (!a) return false;
    for (auto& w : a->workspaces) {
        if (w.name == ws_name) {
            w.instances.push_back(std::move(inst));
            return true;
        }
    }
    return false;
}

bool Registry::remove_instance(std::string_view alloc_id,
                                std::string_view ws_name,
                                std::string_view inst_id) {
    std::lock_guard<std::recursive_mutex> lk(*mu_);
    auto* a = find_allocation(alloc_id);
    if (!a) return false;
    for (auto& w : a->workspaces) {
        if (w.name != ws_name) continue;
        for (auto it = w.instances.begin(); it != w.instances.end(); ++it) {
            if (it->id == inst_id) { w.instances.erase(it); return true; }
        }
    }
    return false;
}

// ══════════════════════════════════════════════════════════════════════════════
// Reconciliation
// ══════════════════════════════════════════════════════════════════════════════

int Registry::reconcile(std::string_view cluster, const std::vector<JobState>& snapshot) {
    std::lock_guard<std::recursive_mutex> lk(*mu_);
    int transitioned = 0;
    for (auto& a : allocations) {
        if (a.cluster != cluster)            continue;
        if (a.state   == AllocationState::Ended) continue;

        bool present = false;
        for (const auto& js : snapshot) {
            if (js.jobid == a.jobid) { present = true; break; }
        }
        if (!present) {
            a.state = AllocationState::Ended;
            ++transitioned;
        }
    }
    return transitioned;
}

// ══════════════════════════════════════════════════════════════════════════════
// LockScope — POSIX flock advisory lock on a sidecar file.
//
// On Linux, flock is per-open-file-description, so a fresh LockScope after
// destruction of a previous one will acquire successfully. Two independent
// tash processes calling flock on the same file will serialize.
// ══════════════════════════════════════════════════════════════════════════════

Registry::LockScope::LockScope(const std::filesystem::path& lockfile)
    : path_(lockfile) {
    fd_ = ::open(lockfile.c_str(), O_CREAT | O_RDWR, 0644);
    if (fd_ < 0) return;
    if (::flock(fd_, LOCK_EX) < 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

Registry::LockScope::~LockScope() {
    if (fd_ >= 0) {
        ::flock(fd_, LOCK_UN);
        ::close(fd_);
        fd_ = -1;
    }
}

Registry::LockScope::LockScope(LockScope&& other) noexcept
    : fd_(other.fd_), path_(std::move(other.path_)) {
    other.fd_ = -1;
}

Registry::LockScope& Registry::LockScope::operator=(LockScope&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) { ::flock(fd_, LOCK_UN); ::close(fd_); }
        fd_        = other.fd_;
        path_      = std::move(other.path_);
        other.fd_  = -1;
    }
    return *this;
}

Registry::LockScope Registry::lock_scope(const std::filesystem::path& registry_path) {
    auto lockfile = registry_path;
    lockfile += ".lock";
    return LockScope(lockfile);
}

}  // namespace tash::cluster
