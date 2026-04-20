// ClusterEngine — orchestration layer for all cluster commands.
//
// Holds references to the Config, Registry, and every seam, then exposes
// one method per user command:
//
//   up / launch / attach / list / down / kill / probe / sync / import
//
// Each method is synchronous; tests drive them against the fake seams
// in tests/unit/cluster/fakes/.
//
// Two auxiliary seams live here because they're engine-local concerns:
//
//   IPrompt  — foreground user choice (e.g. "queued too long: [c/k/d]?")
//   IClock   — wall/monotonic clock + sleep, injected so wait-timeout
//              logic is deterministic in tests.

#ifndef TASH_CLUSTER_CLUSTER_ENGINE_H
#define TASH_CLUSTER_CLUSTER_ENGINE_H

#include "tash/cluster/config.h"
#include "tash/cluster/notifier.h"
#include "tash/cluster/registry.h"
#include "tash/cluster/slurm_ops.h"
#include "tash/cluster/ssh_client.h"
#include "tash/cluster/tmux_ops.h"
#include "tash/cluster/types.h"

#include <chrono>
#include <functional>
#include <string>
#include <variant>
#include <utility>

namespace tash::cluster {

// ── Engine error value ────────────────────────────────────────
struct EngineError {
    std::string message;
};

// Result sum type — caller get_if<T> or std::visit.
template <typename T>
using ClusterResult = std::variant<T, EngineError>;

// ── IPrompt: foreground user choice ───────────────────────────
// Implementations:
//   - Interactive real impl reads a line from stdin.
//   - Tests inject a FakePrompt that returns scripted choices.
class IPrompt {
public:
    virtual ~IPrompt() = default;
    // Show `message`, accept one of the `choices` (single chars, e.g. "ckd").
    // Return the chosen char; '\0' means non-interactive fall-through.
    virtual char choice(const std::string& message, const std::string& choices) = 0;
};

// ── IClock: monotonic time + sleep ────────────────────────────
class IClock {
public:
    virtual ~IClock() = default;
    virtual std::chrono::steady_clock::time_point now() = 0;
    virtual void sleep_for(std::chrono::milliseconds) = 0;
};

// Real clock — wall-clock sleep, steady_clock now().
class RealClock : public IClock {
public:
    std::chrono::steady_clock::time_point now() override;
    void sleep_for(std::chrono::milliseconds d) override;
};

// ── ClusterEngine ─────────────────────────────────────────────
class ClusterEngine {
public:
    ClusterEngine(const Config& cfg,
                   Registry&    reg,
                   ISshClient&  ssh,
                   ISlurmOps&   slurm,
                   ITmuxOps&    tmux,
                   INotifier&   notify,
                   IPrompt&     prompt,
                   IClock&      clock);

    // ── Commands ──────────────────────────────────────────────
    ClusterResult<Allocation> up(const UpSpec& spec);

    // launch: start a long-running instance inside a workspace. Resolves
    // to an allocation (either `spec.alloc_id` or the unique running one),
    // creates the tmux session if the workspace is new, runs the preset
    // (or --cmd) command in a new window. If the command exits within
    // the "liveness window" (~2 clock ticks), marks the instance Exited
    // and fires a notification — never silently reports a failed launch.
    ClusterResult<Instance> launch(const LaunchSpec& spec);

    // attach: exec into the given <workspace>/<instance> on its allocation.
    // On success, exec_attach() is called (real impl replaces the process
    // image; the fake just records). The result Instance is a snapshot of
    // what we handed off.
    ClusterResult<Instance> attach(const AttachSpec& spec);

    // list: snapshot of allocations (optionally filtered by cluster).
    ClusterResult<std::vector<Allocation>> list(const ListSpec& spec);

    // down: scancel the SLURM job and remove the allocation from the
    // registry. Returns a snapshot of the allocation that was torn down
    // (state set to Ended on the returned copy).
    ClusterResult<Allocation> down(const DownSpec& spec);

    // kill: tmux kill-window + remove that Instance from the registry.
    // Allocation-level state is untouched. Returns the removed instance.
    ClusterResult<Instance> kill(const KillSpec& spec);

    // Diagnostic report for `cluster probe -r <resource>`.
    struct RouteStatus {
        std::string cluster;
        std::string partition;
        int         idle_nodes        = 0;
        int         idle_matching_gres = 0;
        std::string partition_state;          // "up" / "down" / "drain" / …
    };
    struct ProbeReport {
        std::string resource;
        std::vector<RouteStatus> routes;
    };
    ClusterResult<ProbeReport> probe(const ProbeSpec& spec);

    // sync: run squeue for each relevant cluster and reconcile the
    // registry. Returns how many allocations transitioned to Ended.
    struct SyncReport {
        int clusters_probed = 0;
        int transitions     = 0;
    };
    ClusterResult<SyncReport> sync(const SyncSpec& spec);

    // prune: remove Ended allocations from the registry. Stop-hook
    // events on disk are the long-term history; the registry is for
    // live allocations. Returns the count removed.
    struct PruneReport {
        int removed = 0;
    };
    ClusterResult<PruneReport> prune();

    // import: adopt an externally-submitted SLURM job. Queries squeue
    // on the cluster, finds the jobid, creates an Allocation entry.
    ClusterResult<Allocation> import(const ImportSpec& spec);

    // logs: fetch stop-hook events for a workspace (optionally for a
    // specific instance) from the compute node. Returns the last
    // spec.tail_lines of each matching <ws>/<inst>.event file,
    // concatenated in deterministic order, along with a per-file
    // header so callers can attribute lines to an instance.
    struct LogsReport {
        std::string cluster;                  // cluster the allocation lives on
        std::string alloc_id;
        std::string contents;                 // pre-formatted for display
    };
    ClusterResult<LogsReport> logs(const LogsSpec& spec);

    // Diagnostic report: per-cluster ssh reach + tool presence. Each
    // check carries OK / WARN / FAIL + a one-line message (fix hint
    // on WARN/FAIL).
    struct DoctorCheck {
        enum Level { OK, WARN, FAIL };
        std::string name;
        Level       level = OK;
        std::string message;
    };
    struct DoctorReport {
        struct ClusterBlock {
            std::string              cluster;
            std::vector<DoctorCheck> checks;
        };
        std::vector<ClusterBlock> clusters;
    };
    struct DoctorSpec {
        std::optional<std::string> cluster;   // filter to one cluster
    };
    ClusterResult<DoctorReport> doctor(const DoctorSpec& spec);

    // Accessors so cosmetic subsystems (completion, doctor, help,
    // safety-confirmation prompts in dispatch_cluster) can read the
    // engine's state without going through a command.
    const Config&   config()    const { return cfg_; }
    Registry&       registry()        { return reg_; }
    const Registry& registry()  const { return reg_; }
    IPrompt&        prompt()          { return prompt_; }
    INotifier&      notifier()        { return notify_; }

    // Pre-warm / tear down the per-cluster ssh ControlMaster. Used by
    // `cluster connect <name>` (the one explicit interactive handshake
    // per session where password + Duo are typed) and its `disconnect`
    // counterpart. Both are idempotent on the engine side; the ssh
    // impl decides whether a new master is actually spawned.
    void connect   (const std::string& cluster) { ssh_.connect(cluster); }
    void disconnect(const std::string& cluster) { ssh_.disconnect(cluster); }
    bool master_alive(const std::string& cluster) { return ssh_.master_alive(cluster); }

    // Owner-installed callback that persists the registry to disk.
    // Called after every state-mutating command (up / launch / kill /
    // down / import / sync) AND — critically — immediately before
    // exec_attach replaces this process, so an in-progress attach
    // doesn't leave the on-disk registry out of date.
    using SaveFn = std::function<void()>;
    void set_save_callback(SaveFn fn) { save_ = std::move(fn); }

private:
    const Config& cfg_;
    Registry&     reg_;
    ISshClient&   ssh_;
    ISlurmOps&    slurm_;
    ITmuxOps&     tmux_;
    INotifier&    notify_;
    IPrompt&      prompt_;
    IClock&       clock_;
    SaveFn        save_;
};

}  // namespace tash::cluster

#endif  // TASH_CLUSTER_CLUSTER_ENGINE_H
