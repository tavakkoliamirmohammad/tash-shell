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
#include <string>
#include <variant>

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

    // Future tasks (M1.7, M1.8): launch, attach, list, down, kill,
    // probe, sync, import.

private:
    const Config& cfg_;
    Registry&     reg_;
    ISshClient&   ssh_;
    ISlurmOps&    slurm_;
    ITmuxOps&     tmux_;
    INotifier&    notify_;
    IPrompt&      prompt_;
    IClock&       clock_;
};

}  // namespace tash::cluster

#endif  // TASH_CLUSTER_CLUSTER_ENGINE_H
