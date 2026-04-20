# Tash Cluster — SLURM-backed remote launcher

`cluster` is a tash built-in that makes it easy to drive long-running
interactive jobs on SLURM HPC clusters from your laptop:

```
❯ cluster up     -r a100 -t 4h
❯ cluster launch --workspace myrepo --preset claude
❯ cluster attach myrepo/1
❯ cluster down  utah-notchpeak:1234567
```

It handles SLURM allocation, SSH multiplexing (one password / Duo prompt
per session, not per command), tmux-persistent sessions on the compute
node, notifications when Claude (or any long-running job) needs attention,
and a unified view across multiple clusters.

Jump to:

- [Prerequisites](#prerequisites)
- [Config file](#config-file)
- [First run: sanity-check with `cluster doctor`](#first-run-sanity-check-with-cluster-doctor)
- [Typical workflow](#typical-workflow)
- [Commands reference](#commands-reference)
- [Notifications](#notifications)
- [Demo mode (no cluster required)](#demo-mode-no-cluster-required)
- [Troubleshooting](#troubleshooting)

## Prerequisites

**On your laptop (where tash runs):**

- OpenSSH client (7.0+; any modern distro / macOS is fine).
- Tash built with `-DTASH_CLUSTER=ON` (the default; confirm with
  `tash --features`).

**On the cluster's login node:**

- `sbatch` / `squeue` / `sinfo` / `scancel` in `PATH`.
- `tmux` in `PATH` (`cluster launch` / `cluster attach` use it).

**SSH access to the cluster must already work.** Tash does not rewrite
`~/.ssh/config`; add the cluster alias yourself:

```
# ~/.ssh/config
Host utah-notchpeak
    HostName notchpeak.chpc.utah.edu
    User     your-username
    # ProxyJump bastion.example   # if your cluster hides behind a jump host
```

Password + Duo MFA works fine — tash's SSH ControlMaster caches the
authenticated connection, so you'll only be prompted once per work
session (hours, not per command).

## Config file

Tash reads `~/.tash/cluster/config.toml`. Override with
`$TASH_CLUSTER_HOME` or `$XDG_CONFIG_HOME/tash/cluster`.

Minimal:

```toml
[defaults]
workspace_base = "/scratch/general/vast/$USER"
default_preset = "claude"

[[clusters]]
name        = "utah-notchpeak"
ssh_host    = "utah-notchpeak"    # must resolve in ~/.ssh/config
description = "University of Utah CHPC — Notchpeak"

[[resources]]
name         = "a100"
kind         = "gpu"
default_time = "4:00:00"
default_cpus = 8
default_mem  = "64G"
routes = [
  { cluster = "utah-notchpeak",
    account = "owner-gpu", partition = "notchpeak-gpu",
    qos = "notchpeak-gpu", gres = "gpu:a100:1" },
]

[[presets]]
name      = "claude"
command   = "claude"
env_file  = "~/.config/anthropic/env.sh"   # optional; sourced before launch
stop_hook = "builtin:claude"               # packaged notification hook
```

Key shapes:

- **`[[clusters]]`** — one entry per site. `ssh_host` is what `ssh
  <ssh_host>` should use on your machine.
- **`[[resources]]`** — user-facing buckets (think "I want an A100").
  Each resource has one or more **routes** — `(cluster, account,
  partition, qos, gres)` tuples. `cluster up -r a100` probes each
  route's `sinfo`; the first route with idle capacity wins; `--via
  <cluster>` forces a specific route.
- **`[[presets]]`** — named `(command, env_file, stop_hook)` triples.
  `stop_hook = "builtin:claude"` uses the Claude Code Stop hook tash
  ships; set an absolute path to use your own. Omit `stop_hook` and
  the watcher falls back to tmux silence / window-death detection.

A larger example:

```toml
[defaults]
workspace_base     = "/scratch/general/vast/$USER"
default_preset     = "claude"
control_persist    = "yes"          # keep ssh master alive all session
notify_silence_sec = 120             # tmux silence fallback threshold

[[clusters]]
name = "utah-notchpeak"
ssh_host = "utah-notchpeak"
[[clusters]]
name = "utah-kingspeak"
ssh_host = "utah-kingspeak"

[[resources]]
name = "a100"
kind = "gpu"
default_time = "4:00:00"
default_cpus = 8
default_mem  = "64G"
routes = [
  { cluster = "utah-notchpeak", account = "owner-gpu", partition = "notchpeak-gpu", qos = "notchpeak-gpu", gres = "gpu:a100:1" },
  { cluster = "utah-kingspeak", account = "kpcg-gpu",  partition = "kingspeak-gpu", qos = "kingspeak-gpu", gres = "gpu:a100:1" },
]

[[resources]]
name = "cpu-large"
kind = "cpu"
default_time = "8:00:00"
default_cpus = 32
default_mem  = "256G"
routes = [
  { cluster = "utah-notchpeak", account = "owner-cpu", partition = "notchpeak", qos = "notchpeak" },
]

[[presets]]
name      = "claude"
command   = "claude"
env_file  = "~/.config/anthropic/env.sh"
stop_hook = "builtin:claude"

[[presets]]
name    = "python-train"
command = "python train.py"

[[presets]]
name    = "jupyter"
command = "jupyter lab --no-browser --port=8888"
```

## First run: sanity-check with `cluster doctor`

```
❯ cluster doctor
utah-notchpeak:
  [ok  ] SSH reach: utah-notchpeak — ssh works (password+Duo may still prompt once per session)
  [ok  ] sbatch on utah-notchpeak — /usr/bin/sbatch
  [warn] tmux on utah-notchpeak   — `which tmux` came up empty; install tmux on the cluster to use cluster launch / attach.
```

`doctor` exits non-zero if any check is FAIL (ssh unreachable), zero
otherwise. WARN is informational — `tmux` may be optional for your use
case, but you'll need it for `launch` / `attach`.

Filter to one cluster: `cluster doctor utah-notchpeak`.

## Typical workflow

```
# 1. Warm the ssh master so password+Duo happens in a clean context
❯ cluster connect utah-notchpeak
# (password prompt, then Duo)

# 2. Allocate
❯ cluster up -r a100 -t 4h
allocated notch123 on utah-notchpeak (jobid 1234567)

# 3. Launch Claude in a workspace (first launch creates the tmux session)
❯ cluster launch --workspace myrepo --preset claude
launched instance 1 (window '1') — state=running

# 4. Launch a second instance, this one running a training script
❯ cluster launch --workspace myrepo --cmd "python train.py --epochs 10"
launched instance 2 (window '2') — state=running

# 5. See what's running
❯ cluster list
utah-notchpeak:1234567  a100  notch123  running
  myrepo  2 instances
    [1]  running  1
    [2]  running  2

# 6. Attach to the first (Ctrl-b d inside tmux to detach back to the shell)
❯ cluster attach myrepo/1

# 7. Kill just the training run
❯ cluster kill myrepo/2
kill myrepo/2? [y/n] y
killed myrepo/2

# 8. When you're done, release the allocation
❯ cluster down utah-notchpeak:1234567
cancel allocation utah-notchpeak:1234567 (a100) on notch123? [y/n] y
cancelled allocation utah-notchpeak:1234567

# 9. Tear down the ssh master when leaving for the day
❯ cluster disconnect utah-notchpeak
```

## Commands reference

| Command | What it does |
|---|---|
| `cluster doctor [<cluster>]` | Diagnose SSH reach + `sbatch` / `tmux` presence per cluster. |
| `cluster probe -r <resource>` | Show each route's idle node count + GRES match for a resource. |
| `cluster connect <cluster>` | Warm the SSH master in a clean foreground context (so the password / Duo prompt doesn't interleave a queued `sbatch`). |
| `cluster disconnect <cluster>` | Tear down the SSH master. |
| `cluster up -r <resource> [-t <time>] [--via <cluster>] [--cpus N] [--mem M] [--name <label>]` | Submit a SLURM allocation. Polls `squeue` until Running; prompts `[c]ancel / [k]eep / [d]etach` if wait exceeds `--wait-timeout` (default 5m). |
| `cluster launch --workspace <name> [--preset <name> \| --cmd "<shell>"] [--cwd <path>] [--name <instance-name>] [--alloc <id>]` | Start a new tmux window on the allocation's compute node. First launch in a workspace creates a tmux session; subsequent launches add windows. |
| `cluster attach <workspace>/<instance> [--alloc <id>]` | exec into the instance (ssh -t + tmux attach). Ctrl-b d to detach. |
| `cluster list [<cluster>] [--json]` | Show known allocations + their workspaces + instances. |
| `cluster down <alloc-id> [-y]` | scancel + remove from registry. `-y` / `--yes` skips the confirmation prompt. |
| `cluster kill <workspace>/<instance> [-y] [--alloc <id>]` | tmux kill-window + remove the instance. |
| `cluster sync [<cluster>]` | Reconcile registry against `squeue`; mark missing jobs `ended`. |
| `cluster import <jobid> --via <cluster> [--resource <name>]` | Adopt a job you submitted via bare `sbatch` so tash can manage it. |
| `cluster help [<subcommand>]` | Short help. Also `cluster <sub> --help`. |

## Notifications

When Claude Code stops and wants your input — or a training run exits
unexpectedly — tash pops a desktop notification and rings the terminal
bell. Two detection paths, both automatic:

1. **Stop hook.** If the preset has `stop_hook = "builtin:claude"`,
   tash installs a small script on the compute node that writes a
   marker file when Claude pauses. The laptop-side watcher, tailing
   the event dir over the same SSH ControlMaster, picks it up within
   a second or two.

2. **Tmux fallback.** For presets without a stop hook (say, `python
   train.py`), tash polls `tmux list-windows` through SSH. If a
   window's `pane_pid` has been stable for `notify_silence_sec`
   (default 120s), or the window disappears entirely, the watcher
   fires a notification.

Notifications are delivered via the platform's native mechanism:

- macOS — `osascript -e 'display notification …'` (Notification Centre)
- Linux — `notify-send` (whatever your DE wires to libnotify)

Plus a `\a` BEL on stderr on every platform. No cloud / push
integrations in v1.

### Scope of auto-notifications in v1

Two notification paths are live:

1. **Engine-driven notifications.** Whenever a `cluster` command
   observes an event (e.g. `launch` detects that the window died
   immediately, `doctor` fails a check), the engine calls `notify_`
   directly and you get the desktop notification + bell. This works in
   both demo and production.
2. **Watcher-hook lifecycle.** `ClusterWatcherHookProvider` is
   instantiated inside demo mode's bundle and its `on_startup` /
   `on_exit` are invoked at the right boundaries, so the thread-
   management scaffolding (spawn one watcher per Running allocation,
   join with a 2s backstop on shutdown) is exercised end-to-end. In
   v1, the factory returns a `NoOpWatcher`.

Background event-stream notifications via `ssh <cluster> tail -F
<event-dir>` are **not** wired in v1 — that requires a real
`LineSource` backed by a piped `ssh` process, which is tracked as
post-v1 work (M3.3 / M3.4 in the plan). If you want live
notifications on a cluster today, run `cluster sync` periodically or
wrap `cluster list` in your prompt loop.

## Demo mode (no cluster required)

Want to kick the tires without a real cluster? Launch tash with
`TASH_CLUSTER_DEMO=1` and every `cluster` command works against an
in-memory fake:

```
❯ TASH_CLUSTER_DEMO=1 tash

❯ cluster up -r a100
allocated demo-n10000 on demo-cluster (jobid 10000)

❯ cluster launch --workspace demo --cmd "echo hello from demo"
launched instance 1 (window '1') — state=running

❯ cluster list
demo-cluster:10000  a100  demo-n10000  running
  demo  1 instances
    [1]  running  1

❯ cluster down demo-cluster:10000 -y
cancelled allocation demo-cluster:10000
```

See `docs/cluster-demo.md` for a full walkthrough.

## Troubleshooting

**`cluster doctor` shows `[fail] SSH reach`.**
Check that `ssh <ssh_host>` works on your laptop first. `-vvv` on
OpenSSH will tell you whether it's network, auth, or config. If Duo
is timing out, run `cluster connect <cluster>` in a fresh terminal so
the prompt isn't mixed with other command output.

**`cluster up` hangs in `PD` (pending).**
Your job is queued behind others; `squeue --me` on the cluster will
show the position. Wait, or `d` (detach) at the next timeout prompt —
tash records the pending allocation and you can `cluster attach` once
it starts. `cluster probe -r <resource>` tells you which route has
idle capacity if you want to try `--via` instead.

**`cluster launch` says `instance exited immediately`.**
Your preset command crashed within 2s of starting. The tmux window is
kept around (not killed) so you can `cluster attach <ws>/<id>` and see
the scrollback. Common causes: typos in `command`, missing binaries on
the compute node, or an `env_file` that doesn't exist.

**Notifications aren't firing.**
`cluster doctor` won't help here — check:
- On Linux, is `notify-send` installed? (`apt install libnotify-bin`
  or equivalent.)
- On macOS, is Terminal/iTerm allowed to send notifications in System
  Settings → Notifications?
- The watcher runs per-allocation. No allocation, no watcher. Check
  `cluster list` first.

**Stale state after killing tash mid-session.**
Allocations persist in `~/.tash/cluster/registry.json`. On next tash
start, the watcher hook auto-reconciles via `squeue` and drops any
allocation that's no longer running. You can force it with `cluster
sync`.

**Something else is weird.**
`cluster logs` shows the watcher's log (reconnect attempts, events
received, etc.). The registry is human-readable JSON; inspect it at
`~/.tash/cluster/registry.json` if you suspect state divergence.

## See also

- `docs/cluster-demo.md` — demo mode walkthrough (no cluster needed).
- `docs/superpowers/specs/2026-04-18-cluster-remote-launcher-design.md`
  — the design document this feature grew from.
- `tests/smoke/cluster/README.md` — how to run the real-cluster smoke
  suite against your own profile before a release.
