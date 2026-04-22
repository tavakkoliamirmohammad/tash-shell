// Core value types for the cluster subsystem — plain aggregates split
// into three sections: config (loaded from TOML), runtime registry
// (persisted to JSON), and seam I/O (passed through ISshClient /
// ISlurmOps / ITmuxOps). Timestamps and SLURM vocabulary stay as
// strings so wire round-trips are trivial; enums appear only where
// the value space is small and closed.

#ifndef TASH_CLUSTER_TYPES_H
#define TASH_CLUSTER_TYPES_H

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tash::cluster {

// ══════════════════════════════════════════════════════════════════════════════
// Configuration model  (from ~/.tash/cluster/config.toml)
// ══════════════════════════════════════════════════════════════════════════════

enum class ResourceKind { Gpu, Cpu };

struct Route {
    std::string cluster;       // must reference a Cluster::name declared above
    std::string account;
    std::string partition;
    std::string qos;
    std::string gres;          // e.g. "gpu:a100:1"; empty for CPU-only routes
};

struct Cluster {
    std::string name;          // unique key, user-facing
    std::string ssh_host;      // must resolve in ~/.ssh/config
    std::string description;
};

struct Resource {
    std::string name;          // unique key; user-facing (cluster up -r <name>)
    ResourceKind kind = ResourceKind::Gpu;
    std::string description;
    std::string default_time = "4:00:00";   // "HH:MM:SS"
    int default_cpus = 8;
    std::string default_mem = "32G";
    std::vector<Route> routes;
};

struct Preset {
    std::string name;                            // unique key
    std::string command;                         // what to run in the tmux window
    std::optional<std::string> env_file;         // sourced before launch, if set
    std::optional<std::string> stop_hook;        // "builtin:claude" or absolute path
};

struct Defaults {
    std::string workspace_base;                   // default --cwd root
    std::string default_preset;                   // name of a declared preset
    std::string control_persist = "yes";          // ssh ControlPersist
    int notify_silence_sec = 120;                 // tmux silence fallback threshold
};

struct Config {
    Defaults defaults;
    std::vector<Cluster>  clusters;
    std::vector<Resource> resources;
    std::vector<Preset>   presets;
};

// ══════════════════════════════════════════════════════════════════════════════
// Runtime registry model  (~/.tash/cluster/registry.json)
// ══════════════════════════════════════════════════════════════════════════════

enum class AllocationState {
    Pending,      // submitted, waiting to start
    Running,      // node assigned, sbatch body running
    Ended,        // completed or timed out normally
    Unreachable,  // we lost contact with the cluster
};

enum class InstanceState {
    Running,      // process alive, output within notify_silence_sec
    Idle,         // claude stop-hook idle, or silence-threshold tripped
    Stopped,      // claude stop-hook said stopped (awaiting user)
    Exited,       // tmux window's pid exited cleanly
    Crashed,      // pid exited non-zero
};

struct Instance {
    std::string id;                              // "1", "2", …; unique per workspace
    std::optional<std::string> name;             // user-provided --name, if any
    std::string tmux_window;                     // id, or name if set
    std::optional<std::int64_t> pid;             // null until discovered
    InstanceState state = InstanceState::Running;
    std::string last_event_at;                   // ISO-8601; empty = never
};

struct Workspace {
    std::string name;                            // unique within an allocation
    std::string cwd;                             // absolute path on compute node
    std::string tmux_session;                    // session name on compute node
    std::vector<Instance> instances;
};

struct Allocation {
    std::string id;                              // "<cluster>:<jobid>"
    std::string cluster;                         // Cluster::name
    std::string jobid;
    std::string resource;                        // Resource::name
    std::string node;                            // compute node hostname
    std::string submitted_at;                    // ISO-8601
    std::string started_at;                      // ISO-8601; empty if still pending
    // (historical note: an `end_by` ISO-8601 field lived here but was
    // never populated anywhere — dropping the persisted-but-dead
    // field is simpler than building a SLURM time-string parser for
    // a field only used in one preview line.)
    AllocationState state = AllocationState::Pending;
    std::vector<Workspace> workspaces;
};

// ══════════════════════════════════════════════════════════════════════════════
// Seam value types  (ISshClient / ISlurmOps / ITmuxOps input/output)
// ══════════════════════════════════════════════════════════════════════════════

// Where on the cluster we want to operate.
//
// Hop priority (first non-empty wins):
//   jobid  → `ssh <cluster> srun --jobid=<jobid> --overlap <cmd>`
//            Works on any SLURM site regardless of inter-node ssh policy.
//            This is the portable, default path when an allocation exists.
//   node   → `ssh <cluster> ssh <node> <cmd>` (legacy direct hop).
//            Only works where the site allows login→compute ssh without
//            extra auth (many sites disable this, e.g. CHPC granite).
//   empty  → run on login node (for doctor / diagnostic calls).
struct RemoteTarget {
    std::string cluster;     // Cluster::name; resolves to ssh_host via Config
    std::string node;        // compute node hostname; fallback if jobid empty
    std::string jobid;       // SLURM jobid — preferred compute-node hop
};

// Result of one ssh invocation.
// Fields are spelled `out` / `err` rather than stdout/stderr because those
// identifiers are macros in <cstdio> on some platforms.
struct SshResult {
    int exit_code = 0;
    std::string out;
    std::string err;
};

struct SubmitSpec {
    std::string cluster;     // where to submit
    std::string account;
    std::string partition;
    std::string qos;
    std::string gres;        // may be empty for CPU-only
    std::string time;        // "HH:MM:SS"
    int         cpus = 1;
    std::string mem;         // e.g. "32G"
    std::string job_name;    // sbatch --job-name
    std::string wrap;        // sbatch --wrap body; may be empty (sleep-forever default)
};

struct SubmitResult {
    std::string jobid;
    std::string raw_stdout;  // whole sbatch stdout, kept for diagnostics
};

struct JobState {
    std::string jobid;
    std::string state;       // SLURM state code: PD, R, CG, CD, F, TO, …
    std::string node;        // compute node; empty while pending
    std::string time_left;   // "HH:MM:SS"; empty while pending
};

struct PartitionState {
    std::string partition;
    std::string state;                    // "up", "down", "drain", …
    int idle_nodes = 0;
    std::vector<std::string> idle_gres;   // gres strings reported for idle nodes
};

struct SessionInfo {
    std::string name;
    int  window_count = 0;
    bool attached = false;
};

// ══════════════════════════════════════════════════════════════════════════════
// Command-input specs  (ClusterEngine public method arguments)
// ══════════════════════════════════════════════════════════════════════════════

struct UpSpec {
    std::string resource;                         // required: Resource::name
    std::string time;                             // "Xh", "HH:MM:SS", or empty for default
    std::optional<int> cpus;
    std::string mem;                              // empty = use resource default
    std::optional<std::string> via;               // force a specific cluster
    std::optional<std::string> name;              // user-friendly allocation name
    std::chrono::seconds wait_timeout{300};       // cluster up --wait-timeout (default 5min)
};

struct LaunchSpec {
    std::string workspace;                        // required
    std::string cwd;                              // empty = defaults.workspace_base
    std::optional<std::string> name;              // instance --name override
    std::optional<std::string> preset;            // preset name; else defaults.default_preset
    std::optional<std::string> cmd;               // ad-hoc --cmd (mutually exclusive with preset)
    std::optional<std::string> alloc_id;          // disambiguate when multiple running
};

struct AttachSpec {
    std::string workspace;                        // required
    std::string instance;                         // required: instance id or name
    std::optional<std::string> alloc_id;
};

struct ListSpec {
    std::optional<std::string> cluster;           // filter to one cluster
};

struct DownSpec {
    std::string alloc_id;                         // required
};

struct KillSpec {
    std::string workspace;
    std::string instance;
    std::optional<std::string> alloc_id;
};

struct SyncSpec {
    std::optional<std::string> cluster;           // default: all known clusters
};

}  // namespace tash::cluster

#endif  // TASH_CLUSTER_TYPES_H
