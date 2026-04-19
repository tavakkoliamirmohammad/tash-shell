# Tash Cluster — Remote Long-Running Job Launcher

**Status:** Design (awaiting plan)
**Date:** 2026-04-18
**Scope:** v1

## 1. Summary

A new tash subsystem, surfaced as the `cluster` builtin, that makes it easy to
launch and manage long-running interactive jobs — Claude Code, training runs,
Jupyter, REPLs, or anything else — on SLURM-based HPC clusters, directly from
a laptop's tash shell.

The user thinks in terms of **resources** ("I need an A100 for 12 hours"). Tash
resolves the resource to a (cluster, account, partition, QoS) route, submits a
SLURM job, SSHes into the allocated compute node, and launches the requested
command inside a tmux window. Multiple commands can share a single allocation,
grouped by directory ("workspace"). The user switches between running
instances from one tash shell, gets desktop notifications when an instance
needs attention, and releases the allocation when done.

Tash runs only on the laptop. Persistence on the cluster side is delegated to
tmux. Authenticated connections are reused across all operations via OpenSSH
`ControlMaster`, so password + Duo MFA is prompted at most once per working
session, not per operation.

## 2. Goals and non-goals

### Goals

- **Resource-first UX.** The user asks for a GPU type (e.g. `a100`); tash knows
  which clusters can provide one and picks one with capacity.
- **Multi-cluster, single control plane.** One tash shell manages allocations
  and instances across any number of clusters simultaneously.
- **Multi-instance per allocation.** One SLURM job can host many instances,
  optionally grouped by working directory.
- **Persistence through tmux.** Instances survive network drops, laptop sleep,
  and tash restarts.
- **Generalizable.** Works for Claude Code, but equally well for any
  long-running command (training, Jupyter, bash).
- **Auth that works on CHPC (password + Duo) and on any key-based cluster.**
  No new auth mechanism invented; OpenSSH handles it.
- **Notifications when an instance needs attention.** Desktop notification +
  terminal bell on detectable events (Stop hook, process exit, silence).
- **Offline-first testability.** Every external interaction sits behind a
  mockable seam. Core test suite runs in <60s with no network, no credentials,
  no cluster.

### Non-goals (v1)

- **No code sync.** Tash does not rsync, push, or manage git worktrees. Code
  is assumed to already live on the cluster; the user points at a remote
  path.
- **No daemonized sidecar.** Watcher threads live inside the tash process.
- **No SSH config rewriting.** `~/.ssh/config` is user-managed; tash requires
  that `ssh <cluster-alias>` already works.
- **No simultaneous multi-queue submission.** One probe, one submit.
  Multi-cluster racing is not worth the fair-share policy headaches.
- **No cross-cluster wait-time prediction.** Route selection uses
  currently-idle capacity; queue-time guessing is unreliable.
- **No cluster-side tash.** Only stock `ssh`, `sbatch`, `squeue`, `sinfo`,
  `scancel`, `tmux` are required on the cluster.

## 3. Architecture

```
┌───────────────────────────── Laptop (tash process) ──────────────────────────────┐
│                                                                                  │
│  ┌────────────┐   ┌────────────┐   ┌────────────┐   ┌────────────┐                │
│  │ cluster    │   │ Cluster    │   │ Cluster    │   │ Notifier   │                │
│  │ builtin    │   │ Completion │   │ Watcher    │   │ (desktop + │                │
│  │ (cmd UX)   │   │ Provider   │   │ HookProvider│   │  bell)     │                │
│  └─────┬──────┘   └─────┬──────┘   └─────┬──────┘   └─────┬──────┘                │
│        │                │                │                │                      │
│        ▼                ▼                ▼                ▼                      │
│  ┌────────────────────────────────────────────────────────────────────────┐      │
│  │                        ClusterEngine (core library)                    │      │
│  │  ┌──────────┐  ┌───────────┐  ┌───────────┐  ┌──────────┐  ┌────────┐  │      │
│  │  │ Config   │  │ Registry  │  │ SshClient │  │ SlurmOps │  │ TmuxOps│  │      │
│  │  │ (TOML)   │  │ (JSON+FS) │  │ (ctrlmstr)│  │ (sbatch) │  │ (ssh)  │  │      │
│  │  └──────────┘  └───────────┘  └───────────┘  └──────────┘  └────────┘  │      │
│  └────────────────────────────────────────────────────────────────────────┘      │
│                                        │                                         │
└────────────────────────────────────────┼─────────────────────────────────────────┘
                                         │ ssh (ControlMaster: one master per cluster)
                                         ▼
       ┌─────────────────── Login node (per cluster) ───────────────────┐
       │  sbatch / squeue / sinfo / scancel — stock SLURM CLI            │
       │  ssh <compute-node> — jump to compute node when attaching       │
       └─────────────────────────────────┬───────────────────────────────┘
                                         │
                                         ▼
       ┌────────────── Compute node (per allocation) ────────────────────┐
       │  tmux sessions = workspaces, tmux windows = instances            │
       │  Optional per-preset stop hook writes event markers              │
       │  Bell (\a) from instance propagates through tmux + ssh           │
       └─────────────────────────────────────────────────────────────────┘
```

### Key structural properties

- **`ClusterEngine`** is a pure orchestration library: no direct I/O, no tash
  types leaking in. It takes references to four seams (`ISshClient`,
  `ISlurmOps`, `ITmuxOps`, `INotifier`) plus `Config` and `Registry` and
  exposes one method per user command.
- **Three plugin-system touchpoints** consume `ClusterEngine`: a builtin for
  user commands, a completion provider for tab-completion, a hook provider
  for watcher lifecycle.
- **Registry is a cache.** The source of truth is the cluster's own state
  (`squeue`, `tmux list-sessions`). On tash startup, `on_startup`
  reconciles the registry and drops ghosts.
- **One ControlMaster per `(user@login-node)`.** Shared by every operation
  for that cluster. `ControlPersist=yes` by default — one password + Duo per
  working session. Socket path is namespaced under `~/.tash/cluster/sockets/`
  so it cannot collide with the user's own ssh multiplexing.
- **No daemon, no sidecar.** Watcher threads own event tailing; they die with
  tash. Remote tmux sessions outlive tash restarts.
- **Hierarchy:** `allocation → workspace → instance`.
  - Allocation = one SLURM job, one node.
  - Workspace = one tmux session inside the allocation, scoped to a working
    directory.
  - Instance = one tmux window inside a workspace, running one long-running
    command.

## 4. User model

### Hierarchy and addressing

```
cluster    → a ssh-reachable site           (named entry in config.toml)
resource   → a user-declared GPU/CPU type   (named entry; has routes)
route      → (cluster, account, partition, qos, gres)
preset     → named (command, env_file, stop_hook) triple
                                             (everything a launch needs)

allocation → cluster:jobid                   (one SLURM job on one cluster)
workspace  → allocation/name                 (tmux session, scoped to a cwd)
instance   → workspace/id                    (tmux window running one command)
```

Terminology note: the `cluster profile list / show / edit` subcommand treats
each `[[clusters]]` entry as a *profile*. "Profile" and "cluster" are
interchangeable at the user-facing level.

Short-form addressing for the common case:

- `cluster attach repoA/1` — attach to instance 1 of workspace `repoA`, using
  the latest running allocation.
- `cluster attach repoA/feature-x` — by instance name if the user provided
  `--name feature-x` at launch.
- `cluster attach --alloc utah-notchpeak:1234567 repoA/1` — disambiguate when
  more than one allocation has a workspace named `repoA`.

### Switching UX (v1 and beyond)

- **v1: in-terminal attach.** `cluster attach X` takes over the current
  terminal with the target tmux session; Ctrl-b d detaches back to the tash
  prompt. `cluster list` enumerates.
- **Future: TUI picker** (`cluster` with no args or a keybinding) — fuzzy
  picker over all instances across all clusters.
- **Future: mirror mode** (`cluster attach X --mirror`) — open each attached
  instance in a local tmux pane on the laptop.

Both futures are additive and do not reshape v1.

## 5. Configuration

### File layout

```
~/.tash/cluster/
├── config.toml                 # user-edited: clusters + resources + presets
├── registry.json               # tash-owned cache: allocations + state
├── sockets/                    # ControlMaster sockets (path namespaced to tash)
│   └── <ssh-connection-hash>
├── events/                     # notification markers synced from clusters
│   └── <cluster>/<jobid>/<workspace>/<instance>.event
├── logs/
│   ├── watcher.log
│   └── ssh-<cluster>.log
└── hook/                       # stop-hook scripts staged for scp to clusters
    └── claude-stop-hook.sh
```

Root path override: `$TASH_CLUSTER_HOME` or `$XDG_CONFIG_HOME/tash/cluster`.

### `config.toml` schema

```toml
[defaults]
workspace_base     = "/scratch/general/vast/u1419116"
default_preset     = "claude"
control_persist    = "yes"            # ssh ControlPersist — default: keep alive
notify_silence_sec = 120              # tmux silence fallback threshold

# -------- Clusters (connection info) --------
[[clusters]]
name        = "utah-notchpeak"
ssh_host    = "utah-notchpeak"        # must resolve in ~/.ssh/config
description = "University of Utah CHPC — Notchpeak"

[[clusters]]
name     = "utah-kingspeak"
ssh_host = "utah-kingspeak"

# -------- Resources (what the user asks for) --------
[[resources]]
name         = "a100"
kind         = "gpu"                  # "gpu" | "cpu"
description  = "NVIDIA A100 40GB"
default_time = "4:00:00"
default_cpus = 8
default_mem  = "64G"
routes = [
  { cluster = "utah-notchpeak", account = "owner-gpu", partition = "notchpeak-gpu", qos = "notchpeak-gpu", gres = "gpu:a100:1" },
  { cluster = "utah-kingspeak", account = "kpcg-gpu",  partition = "kingspeak-gpu", qos = "kingspeak-gpu", gres = "gpu:a100:1" },
]

[[resources]]
name         = "h100"
kind         = "gpu"
description  = "NVIDIA H100 80GB"
default_time = "2:00:00"
default_cpus = 16
default_mem  = "128G"
routes = [
  { cluster = "utah-notchpeak", account = "owner-gpu", partition = "notchpeak-gpu-h100", qos = "notchpeak-gpu-h100", gres = "gpu:h100:1" },
]

[[resources]]
name         = "cpu-large"
kind         = "cpu"
description  = "Plain CPU node, lots of RAM"
default_time = "8:00:00"
default_cpus = 32
default_mem  = "256G"
routes = [
  { cluster = "utah-notchpeak", account = "owner-cpu", partition = "notchpeak", qos = "notchpeak" },
]

# -------- Presets (what gets launched in a tmux window) --------
[[presets]]
name      = "claude"
command   = "claude"
env_file  = "~/.config/anthropic/env.sh"  # optional; sourced before launch
stop_hook = "builtin:claude"              # ships with tash

[[presets]]
name    = "python-train"
command = "python train.py"
# No stop_hook — relies on tmux silence + window-death

[[presets]]
name    = "jupyter"
command = "jupyter lab --no-browser --port=$PORT"
```

### `registry.json` schema

```json
{
  "schema_version": 1,
  "allocations": [
    {
      "id": "utah-notchpeak:1234567",
      "cluster": "utah-notchpeak",
      "jobid": "1234567",
      "resource": "a100",
      "node": "notch123",
      "submitted_at": "2026-04-18T14:10:03Z",
      "started_at":   "2026-04-18T14:10:46Z",
      "end_by":       "2026-04-18T18:10:46Z",
      "state": "running",
      "workspaces": [
        {
          "name": "repoA",
          "cwd": "/scratch/general/vast/u1419116/repoA",
          "tmux_session": "tash-utah-notchpeak-1234567-repoA",
          "instances": [
            { "id": "1", "name": "feature-x", "tmux_window": "feature-x", "pid": 48211, "state": "running", "last_event_at": "2026-04-18T14:32:10Z" },
            { "id": "2", "name": null,        "tmux_window": "2",         "pid": 48399, "state": "idle",    "last_event_at": "2026-04-18T14:35:55Z" }
          ]
        }
      ]
    }
  ]
}
```

Registry is reconciled against `squeue` on tash startup and on demand via
`cluster sync`. A corrupt file is backed up to `registry.json.bak.<ts>` and
the engine starts with an empty registry.

## 6. Command surface

Single builtin `cluster`, subcommand-dispatched:

```
cluster up        -r <resource> [-t <time>] [--gpus N] [--cpus N] [--mem M]
                                [--via <cluster>] [--name <alloc-name>]
                                [--wait-timeout <duration>]
cluster down      <alloc-id | alloc-name>        # scancel + registry cleanup
cluster list      [<cluster>] [--json]
cluster attach    <workspace>/<instance> [--alloc <id>]
cluster launch    --workspace <name> [--cwd <path>] [--name <instance-name>]
                  [--preset <name> | --cmd <command>] [--alloc <id>]
cluster kill      <workspace>/<instance> [--alloc <id>]
cluster connect   <cluster>                      # warm ssh ControlMaster
cluster disconnect <cluster>                     # tear down ssh ControlMaster
cluster doctor    [<cluster>]                    # diagnose prerequisites
cluster probe     -r <resource>                  # current route state
cluster sync      [<cluster>]                    # reconcile registry
cluster profile   list
                  show   <cluster>
                  edit                           # $EDITOR on config.toml
                  import <jobid> --via <cluster> # adopt an external job
cluster logs      [--watcher | --ssh <cluster>]
```

### Deliberate choices

- `cluster attach` is positional, short, and tab-completes via the completion
  provider. `--alloc` is rarely needed.
- Default alloc resolution: if exactly one running allocation exists, use it
  silently; otherwise prompt with a numbered menu. No arbitrary defaults.
- `cluster down` without args is rejected; explicit id required. Routed
  through tash's existing safety confirmation (same path as `rm -rf`);
  supports `-y` for scripted use.
- `cluster kill` removes one instance (closes tmux window). `cluster down`
  cancels the whole SLURM allocation. Distinct blast radius, distinct verb.
- `--json` on `cluster list` for structured output; integrates with tash's
  `|>` pipelines.
- No `cluster exec`: user already has `ssh <cluster> <cmd>`.

## 7. Data flows

### 7.1 `cluster up -r a100 -t 12h`

```
ClusterBuiltin::run("up", argv)
  └─> ClusterEngine::up({ resource="a100", time="12h", ... })
        ├─ Config::lookup_resource("a100") → { routes, defaults }
        ├─ for each route:
        │    ISlurmOps::sinfo(cluster, partition, ssh) → idle nodes matching gres?
        ├─ pick route (first with idle capacity; else first declared)
        ├─ ISlurmOps::sbatch(SubmitSpec{...}) → jobid
        ├─ poll ISlurmOps::squeue until state=RUNNING (timeout, progress)
        ├─ Registry::add_allocation(...) → persist
        └─ return AllocationId
  └─> print "allocated <node> — <N>h <M>m remaining"
```

### 7.2 `cluster launch --workspace repoA --preset claude`

```
ClusterBuiltin::run("launch", argv)
  └─> ClusterEngine::launch({ workspace="repoA", preset="claude", ... })
        ├─ Registry::pick_allocation(--alloc or latest-running)
        ├─ Workspace exists?
        │     yes → use existing tmux session
        │     no  → ITmuxOps::new_session(target, session_name, cwd)
        │           + scp stop-hook script (once per allocation+workspace)
        ├─ ITmuxOps::new_window(target, session, window_name, cwd, preset.command)
        ├─ Registry::add_instance(...)
        └─ return InstanceRef
  └─> print "launched instance <id> (window '<name>')"
```

### 7.3 Notification round-trip

```
(cluster, inside allocation)
  Stop hook runs  →  ~/.tash-cluster/events/<workspace>/<instance>.event
      payload: { ts, instance, kind: "stopped", detail: "awaiting input" }

(laptop)
  Watcher thread for alloc 1234567 is holding:
      ssh <cluster> tail -F ~/.tash-cluster/events/<jobid>/**/*.event
  reads new line, parses event:
      ├─ Registry::update_instance(state=idle, last_event_at=...)
      ├─ INotifier::desktop("<preset> needs attention",
      │                     "<cluster> · <workspace>/<instance> — <detail>")
      └─ INotifier::bell()
```

## 8. Components

### 8.1 Core library (`src/cluster/` + `include/tash/cluster/`)

```
include/tash/cluster/
  config.h            — Config, Cluster, Resource, Route, Preset, Defaults
  registry.h          — Registry, Allocation, Workspace, Instance
  presets.h           — Preset resolution, env_file sourcing, builtin: scheme
  ssh_client.h        — ISshClient (seam)
  slurm_ops.h         — ISlurmOps (seam)
  tmux_ops.h          — ITmuxOps (seam)
  notifier.h          — INotifier (seam)
  cluster_engine.h    — ClusterEngine (orchestration)
  commands.h          — ClusterCommand variants + ClusterResult

src/cluster/
  config.cpp          — TOML load, validation, route resolution
  registry.cpp        — JSON load/save/lock/reconcile
  presets.cpp         — Preset resolution
  ssh_client.cpp      — Real ISshClient — OpenSSH ControlMaster lifecycle
  slurm_ops.cpp       — Real ISlurmOps — sbatch/squeue/sinfo/scancel wrappers + parsers
  tmux_ops.cpp        — Real ITmuxOps — ssh + tmux composition
  notifier_mac.cpp    — osascript-based INotifier
  notifier_linux.cpp  — notify-send-based INotifier
  cluster_engine.cpp  — command dispatch against seams
```

### 8.2 Seam interfaces

```cpp
class ISshClient {
public:
    virtual ~ISshClient() = default;
    virtual SshResult run(const std::string& cluster,
                          const std::vector<std::string>& argv,
                          std::chrono::milliseconds timeout) = 0;
    virtual bool master_alive(const std::string& cluster) = 0;
    virtual void connect(const std::string& cluster) = 0;
    virtual void disconnect(const std::string& cluster) = 0;
};

class ISlurmOps {
public:
    virtual ~ISlurmOps() = default;
    virtual SubmitResult sbatch(const SubmitSpec&, ISshClient&) = 0;
    virtual std::vector<JobState> squeue(const std::string& cluster, ISshClient&) = 0;
    virtual std::vector<PartitionState> sinfo(const std::string& cluster,
                                               const std::string& partition,
                                               ISshClient&) = 0;
    virtual void scancel(const std::string& cluster, const std::string& jobid, ISshClient&) = 0;
};

class ITmuxOps {
public:
    virtual ~ITmuxOps() = default;
    virtual void new_session(const RemoteTarget&, const std::string& session,
                              const std::string& cwd, ISshClient&) = 0;
    virtual void new_window(const RemoteTarget&, const std::string& session,
                             const std::string& window,
                             const std::string& cwd, const std::string& cmd,
                             ISshClient&) = 0;
    virtual std::vector<SessionInfo> list_sessions(const RemoteTarget&, ISshClient&) = 0;
    virtual void kill_window(const RemoteTarget&, const std::string& session,
                              const std::string& window, ISshClient&) = 0;
    // exec-style; replaces the current process image
    virtual void exec_attach(const RemoteTarget&, const std::string& session,
                              const std::string& window) = 0;
};

class INotifier {
public:
    virtual ~INotifier() = default;
    virtual void desktop(const std::string& title, const std::string& body) = 0;
    virtual void bell() = 0;
};
```

### 8.3 Plugin-system touchpoints

- `src/builtins/cluster.cpp` — argv parsing, formatting, dispatched from
  `src/builtins/meta.cpp`'s builtin table.
- `src/plugins/cluster_completion_provider.cpp` — implements
  `ICompletionProvider`. Completes subcommands, profiles, resources,
  allocations, workspaces, instances.
- `src/plugins/cluster_watcher_hook_provider.cpp` — implements `IHookProvider`.
  `on_startup`: reconciles registry, starts one watcher thread per running
  allocation. `on_exit`: cancels stop-tokens, joins threads (with 2s
  backstop). `on_after_command`: updates last-prompt time for silence
  heuristic.

### 8.4 Watcher thread (per allocation)

```cpp
void watch_allocation(Allocation alloc, ISshClient& ssh, INotifier& notify,
                       StopToken stop) {
    // One persistent `ssh <cluster> tail -F ...` invocation per allocation.
    // Yields lines: "<workspace>/<instance> <event> <timestamp> <payload>".
    // Events: claude_stopped, claude_idle, claude_crashed,
    //         window_exited, silence_threshold, job_ending_soon.
    // For each event: dedup via (instance, ts), update registry, notify.
    // Pipe closed unexpectedly: exponential backoff (1s → 30s), max 3 min,
    // then mark allocation "unreachable".
}
```

### 8.5 Build wiring

- `cmake/cluster.cmake` — declares `cluster_lib` static library; included
  after `cmake/plugins.cmake`.
- `cmake/plugin_list.cmake` — append `cluster_completion_provider` and
  `cluster_watcher_hook_provider`. Disablable via
  `[plugins].disabled = ["cluster-watcher", "cluster-completion"]`.
- `toml++` via `FetchContent` (header-only, no system dep).
- `TASH_CLUSTER` CMake option (default ON) — compile out entirely if
  undesired.
- No new external system dependencies.

## 9. Detection and notification

Three trigger mechanisms, applied in parallel per instance (strongest wins):

1. **Preset stop hook.** Per-preset configured script; Claude's is packaged
   as `builtin:claude` at `data/cluster/stop-hooks/claude-stop-hook.sh`,
   staged to `~/.tash/cluster/hook/` on tash install and `scp`'d to the
   cluster on first use. Writes an event file consumed by the watcher.
2. **Tmux window death.** Watcher polls `tmux list-windows` per session;
   a disappeared window emits a `window_exited` event (covers crashes and
   clean exits for non-Claude presets).
3. **Tmux silence.** `tmux set-window -g monitor-silence <N>` per window;
   watcher observes flagged windows on poll → `silence_threshold` event.

Delivery: **desktop notification via `osascript` (macOS) or `notify-send`
(Linux), plus terminal bell at the tash prompt.** Both fire on every trigger.
Configurable via `[defaults].notify_desktop` and `[defaults].notify_bell`.

## 10. Error handling and edge cases

Guiding principle: **state changes only on confirmed success.** Registry is
mutated after an operation completes; failures leave registry untouched and
emit a clear `tash: cluster: <message>` error.

| Failure | Detection | Behavior | Invariant |
|---|---|---|---|
| ssh master not alive | master probe before op | Re-open master (user prompt); on second failure: `cluster doctor` hint | No partial registry writes |
| Wrong `ssh_host` | ssh exit 255 | Clear error + `cluster doctor` suggestion | No partial writes |
| Network down mid-watcher | tail pipe closes | Backoff 1s→30s (max 3min); mark allocation `unreachable` | Events dedup via ts; no replay |
| Stale ControlMaster after sleep | handshake fails | Remove socket, reconnect once | Single user-visible prompt |
| sbatch rejects | nonzero exit + stderr | Emit sbatch stderr verbatim + route hint | No allocation added |
| Job queued too long | `PD` past `--wait-timeout` (default 5min) | Prompt `[c]ancel [k]eep [d]etach` | Detach keeps jobid for later adoption |
| Allocation ended mid-work | squeue returns empty / pipe dies | Notifier fires; state→`ended`; instances→`ended` | Always visible to user |
| Pre-expiry warning | `end_by - now < 10min` | Desktop notif + bell, once per allocation | Dedup via registry flag |
| External `scancel` | reconciler detects gone jobid | Purge on sync; notify "allocation ended" | Never silently drop |
| Tmux session collision | `tmux new-session` duplicate | Adopt existing session (idempotent) | Safe re-launch |
| Window command exits instantly | `tmux list-windows` shows dead pid within 2s | Notify "instance exited immediately"; state→`exited`; window retained for inspection | No false success |
| Manual tmux session kill on cluster | reconciler sees missing session | Purge workspace + instances; notify | No dangling state |
| `config.toml` parse error | toml++ returns line/col | `tash: cluster: config.toml:<L>:<C>: <msg>`; disable cluster builtin gracefully | Other tash features unaffected |
| Invalid route → unknown cluster | Validated on load | Fail fast with field location | No partial cluster load |
| Corrupt `registry.json` | JSON parse fails | Back up to `.bak.<ts>`, start empty, reconcile | User state preserved |
| Schema migration | `schema_version` field | On-load migration, write back | Old tash tolerates unknown fields |

Concurrency:

- Per-`~/.tash/cluster/` file lock around any registry mutation.
- Watcher threads are joinable via `StopToken`; `pthread_cancel` is a 2s
  backstop.
- ControlMaster sockets are owned by OpenSSH itself via
  `ControlPersist=yes`, not tash.

## 11. Testing strategy

Five tiers. Tiers 1 + 2 + 3 + 5 run on every PR, offline, no credentials,
under 60 seconds combined.

### Tier 1 — Unit tests with fake seams (`tests/unit/cluster/`)

Purpose: exhaustive coverage of `ClusterEngine`, `Config`, `Registry`,
`Presets`, and the watcher's event decoder. Fully deterministic,
millisecond-fast.

Fakes (`tests/unit/cluster/fakes/`): `FakeSshClient`, `FakeSlurmOps`,
`FakeTmuxOps`, `FakeNotifier` — programmable with scripted responses,
record invocations for assertions.

Test files:

```
config_test.cpp, registry_test.cpp,
cluster_engine_up_test.cpp, cluster_engine_launch_test.cpp,
cluster_engine_attach_test.cpp, cluster_engine_down_test.cpp,
cluster_engine_list_test.cpp, cluster_engine_probe_test.cpp,
presets_test.cpp, watcher_test.cpp, notifier_stub_test.cpp
```

Coverage target: ≥85% lines in `src/cluster/` (enforced via existing lcov
gate).

### Tier 2 — Integration tests with stub binaries (`tests/integration/cluster/`)

Purpose: exercise the real `SshClient`/`SlurmOps`/`TmuxOps`/builtin/watcher
code, including fork/exec, pipe I/O, signal handling, argv construction,
output parsing.

**Stub binaries** (`tests/fakes/bin/`, shell scripts):
`ssh`, `sbatch`, `squeue`, `sinfo`, `scancel`, `tmux`, `osascript`. Each
reads `$TASH_FAKE_SCENARIO` and emits canned output matching the real tools.

**Loopback sshd for ControlMaster tests.** A real local `sshd` on
`127.0.0.1:22xxx` (no root; `sshd -f <config> -D`) or an `asyncssh`-based
Python fallback. Covers master open/close, stale-socket recovery, persistence
across ops, socket path uniqueness, argv quoting. Runs on Linux + macOS CI.

Test files:

```
up_down_roundtrip_test.cpp, launch_attach_detach_test.cpp,
multi_workspace_test.cpp, multi_allocation_test.cpp,
notification_end_to_end_test.cpp, ssh_master_reuse_test.cpp,
control_master_recovery_test.cpp, registry_reconcile_test.cpp,
cli_help_and_errors_test.cpp, demo_mode_smoke_test.cpp
```

### Tier 3 — Golden recordings (`tests/fixtures/recordings/`)

Captured real-world output (anonymized) for parser robustness:

```
squeue/, sinfo/, sbatch/, tmux/  (normal + pending + mixed + error variants)
```

Parser tests feed each fixture, assert structured output. New cluster quirks
→ new fixture → targeted fix.

### Tier 4 — Real-cluster smoke tests (manual, opt-in)

`tests/smoke/cluster/`:

- `README.md` with setup instructions.
- `smoke-profile.toml` template.
- `run_smoke.sh` — executes ~8 commands against a user-supplied real profile:
  connect → up → launch (bash-holds-open) → list → attach-detach → kill →
  down → reconcile.

Contract: ≤2 min wall-time (assuming node grants within 30s), 1 allocation
at ≤15 min, read-biased. Not in CI. Run manually pre-release or when
touching high-risk areas.

**No Docker anywhere.** All CI-level testing is offline or uses the
developer's own cluster credentials. Rationale: Docker is not available in
the target developer environment.

### Tier 5 — Fuzz

- `fuzz/cluster_config_fuzz.cpp` — arbitrary bytes → `Config::load`. No
  crashes; must diagnose or accept. CI coverage gate.
- `fuzz/cluster_squeue_parse_fuzz.cpp` — arbitrary bytes →
  `SlurmOps::parse_squeue`. No crashes; OK to return `{}` or `ParseError`.

### Demo mode

`TASH_CLUSTER_DEMO=1 tash` sets `PATH=tests/fakes/bin:$PATH` and
`$TASH_FAKE_SCENARIO` to a bundled demo scenario. A new user (or
contributor) can run the full `up / launch / attach / list / down` flow
with no credentials, no cluster. Demo mode and integration tests share the
same fakes, so drift between them breaks the build.

### Coverage contract

| Component | Tier 1 fake seams | Tier 2 stubs + local sshd | Tier 3 recordings | Tier 4 real cluster |
|---|---|---|---|---|
| `Config`, `Registry`, `Presets`, `ClusterEngine`, `Watcher` | ✓ | ✓ | — | — |
| `SshClient` (argv, ControlMaster) | — | ✓ | — | ✓ |
| `SlurmOps` (parsing) | ✓ | ✓ | ✓ | ✓ |
| `TmuxOps` (argv + session ops) | ✓ | ✓ | ✓ | ✓ |
| `ClusterBuiltin` (user flows) | ✓ | ✓ | — | ✓ |
| `Notifier` | ✓ contract | ✓ stubs | — | manual |

### CI wiring

- Default `ctest` run: Tiers 1 + 2 + 3 + 5. Linux + macOS matrix.
- Coverage gate: `src/cluster/` ≥85% line coverage (reuses tash's existing
  lcov gate).
- Tier 4 is manual.

## 12. Incremental build sequence

Five milestones. Each is independently shippable; each ends with all
earlier-tier tests green and no regressions in existing tash tests.

### M0 — Scaffolding

- `cmake/cluster.cmake`, `TASH_CLUSTER` option.
- Empty shells for every core header and source file.
- Builtin registered but emits "not implemented."
- `toml++` via FetchContent.
- `tests/unit/cluster/`, `tests/integration/cluster/` directories wired
  into ctest.
- Smoke test: `cluster --help` renders.

### M1 — Core engine + Tier 1 tests

- Full `Config` TOML loader + validator.
- Full `Registry` (load/save/lock/reconcile/migration).
- Full `ClusterEngine` (every user command) against fake seams.
- Full `Presets` resolution including `builtin:claude`.
- All Tier 1 tests passing; coverage ≥85%.
- `TASH_CLUSTER_DEMO=1` works end-to-end via fake backend.

### M2 — Real wiring + Tier 2 + Tier 3

- Real `SshClient` with ControlMaster lifecycle.
- Real `SlurmOps` parsers (validated against Tier 3 fixtures).
- Real `TmuxOps` (session/window orchestration + argv quoting).
- Stub binaries (`tests/fakes/bin/`) + scenario format.
- Loopback sshd fixture.
- All Tier 2 tests passing on Linux + macOS.

### M3 — Watcher + notifications

- Stop-hook script packaged at `data/cluster/stop-hooks/claude-stop-hook.sh`.
- Watcher thread (`tail -F` + backoff + dedup).
- `ClusterWatcherHookProvider` lifecycle wiring.
- `INotifier` impls (mac + linux) + fakes.
- Tmux silence + window-death fallback detection.
- End-to-end notification tests via Tier 2 scenario timelines.

### M4 — Completion, polish, safety

- `ClusterCompletionProvider` for all subcommands, profiles, resources,
  workspaces, instances.
- Safety-hook integration for `cluster down` / `cluster kill`.
- `cluster doctor` + `cluster probe` diagnostics.
- `tash: cluster: <msg>` error formatting throughout.
- Per-subcommand help text.
- Tab completion works over ControlMaster instantly after first auth.

### M5 — Docs + real-cluster smoke suite

- `docs/cluster.md` user walkthrough.
- `docs/cluster-demo.md` demo-mode walkthrough.
- `tests/smoke/cluster/` runnable against a real profile.
- README feature matrix update.

## 13. Open questions deferred to v2

- **Mirror mode** for `cluster attach --mirror` (laptop-side tmux panes
  streaming remote tmux sessions).
- **TUI picker** (`cluster` with no args → full-screen fuzzy picker across
  clusters).
- **Code sync** (`--sync-from` for rsync push, git worktree management).
- **Queue-time-aware route probing** beyond current-idle-capacity.
- **Simultaneous multi-queue submission with cancel-losers** (pending
  fair-share policy research per cluster).
- **Sidecar daemon** (if notifications while tash is closed becomes a
  recurring need).
- **External push notifications** (ntfy.sh, Pushover, Slack webhook) —
  plug in at `[notify]` config section.
- **Claude Code Skills integration** on the cluster side (e.g. a
  tash-provided skill that exposes `cluster list` to a running Claude).
