// Runtime registry for the cluster subsystem.
//
// Cache of every known Allocation + its Workspaces + Instances, persisted to
// ~/.tash/cluster/registry.json. The source of truth is always the cluster's
// own state (squeue / tmux); Registry::reconcile() prunes ghosts.
//
// Public contract:
//
//   load(path)             — read + validate; if file is absent, empty registry;
//                             if corrupt, rename to <path>.bak.<unix-ts> and
//                             start empty. Never throws.
//   save(path)             — atomic: write to <path>.tmp, then rename.
//   add/remove_*           — CRUD on allocations / workspaces / instances.
//   reconcile(cluster, js) — mark allocations absent from squeue as Ended.
//   lock_scope(path)       — RAII POSIX flock (advisory) on a sidecar
//                             <path>.lock file so two tash processes can't
//                             both mutate the registry concurrently.
//
// Thread safety: the object itself is NOT thread-safe. Callers that need
// cross-process safety acquire lock_scope() around read-modify-write
// sequences. Within a single process, one ClusterEngine owns the Registry.

#ifndef TASH_CLUSTER_REGISTRY_H
#define TASH_CLUSTER_REGISTRY_H

#include "tash/cluster/types.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace tash::cluster {

class Registry {
public:
    std::vector<Allocation> allocations;
    int schema_version = 1;

    // ── Serialization ──────────────────────────────────────────
    static Registry load(const std::filesystem::path& path);
    void save(const std::filesystem::path& path) const;

    // ── Allocation CRUD ────────────────────────────────────────
    void add_allocation(Allocation a);
    bool remove_allocation(std::string_view id);

    Allocation*       find_allocation(std::string_view id);
    const Allocation* find_allocation(std::string_view id) const;

    // ── Workspace CRUD (scoped to an allocation id) ────────────
    bool add_workspace   (std::string_view alloc_id, Workspace ws);
    bool remove_workspace(std::string_view alloc_id, std::string_view ws_name);

    // ── Instance CRUD (scoped to an allocation + workspace) ────
    bool add_instance   (std::string_view alloc_id, std::string_view ws_name, Instance inst);
    bool remove_instance(std::string_view alloc_id, std::string_view ws_name, std::string_view inst_id);

    // ── Reconciliation ─────────────────────────────────────────
    // For the given cluster, mark every non-Ended allocation whose jobid
    // isn't present in `snapshot` as Ended. Other clusters are untouched.
    // Returns the number of allocations that transitioned to Ended.
    int reconcile(std::string_view cluster, const std::vector<JobState>& snapshot);

    // ── File lock (advisory, POSIX flock) ──────────────────────
    class LockScope {
    public:
        explicit LockScope(const std::filesystem::path& lockfile);
        ~LockScope();

        LockScope(const LockScope&)            = delete;
        LockScope& operator=(const LockScope&) = delete;
        LockScope(LockScope&& other) noexcept;
        LockScope& operator=(LockScope&& other) noexcept;

        bool locked() const { return fd_ >= 0; }

    private:
        int fd_ = -1;
        std::filesystem::path path_;
    };

    // Convenience: locks "<registry_path>.lock".
    static LockScope lock_scope(const std::filesystem::path& registry_path);
};

}  // namespace tash::cluster

#endif  // TASH_CLUSTER_REGISTRY_H
