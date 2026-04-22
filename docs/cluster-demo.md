# Tash Cluster — Demo Mode

You don't need a real SLURM cluster to try the `cluster` builtin.
`TASH_CLUSTER_DEMO=1` installs an in-memory engine where every SLURM
operation is a fake backed by a monotonic jobid counter, tmux
operations are silent no-ops, and the notifier prints to stderr
instead of firing a desktop banner. Five minutes, no credentials,
no `ssh` keys.

## Quick tour (2 minutes)

Start tash with the env var set:

```
❯ TASH_CLUSTER_DEMO=1 tash
```

Everything below happens inside that tash.

### 1. Sanity-check with `cluster list`

```
❯ cluster list
(no allocations)
```

Clean slate.

### 2. Submit an allocation

```
❯ cluster up -r a100 -t 1h
allocated demo-n10000 on demo-cluster (jobid 10000)
```

The fake `sbatch` hands out jobids starting at 10000. `cluster list`
now shows your allocation:

```
❯ cluster list
demo-cluster:10000  a100  demo-n10000  running
```

### 3. Launch an instance into a workspace

```
❯ cluster launch --workspace myproject --cmd "echo hello from demo"
launched instance 1 (window '1') — state=running
```

And another in a different workspace (same allocation):

```
❯ cluster launch --workspace training --cmd "python train.py"
launched instance 1 (window '1') — state=running
```

`cluster list` reflects both:

```
❯ cluster list
demo-cluster:10000  a100  demo-n10000  running
  myproject  1 instances
    [1]  running  1
  training  1 instances
    [1]  running  1
```

### 4. Use the built-in preset

Demo mode also ships a `demo-claude` preset (`echo 'hello from demo'`)
so you can exercise the preset-resolution path:

```
❯ cluster launch --workspace other --preset demo-claude
launched instance 1 (window '1') — state=running
```

### 5. Attach (no-op in demo)

```
❯ cluster attach myproject/1
[demo cluster] exec_attach (noop)
```

In real life `cluster attach` exec's `ssh -t <compute> tmux
attach-session -t <session>:<window>` — your terminal is handed to
the remote tmux. In demo mode that would replace the tash process
with something pointless, so the demo impl just logs and returns.

### 6. Kill one instance

```
❯ cluster kill myproject/1 -y
killed myproject/1
```

(The `-y` skips the confirmation prompt; see `cluster kill --help`.)

### 7. Release the allocation

```
❯ cluster down demo-cluster:10000 -y
cancelled allocation demo-cluster:10000

❯ cluster list
(no allocations)
```

## Via a script file

Tash's `-c` flag isn't a thing, but you can feed a script file:

```
$ cat <<'EOF' > /tmp/tour.sh
cluster up -r a100 -t 1:00:00
cluster list
cluster launch --workspace myproject --cmd "echo hi"
cluster list
cluster down demo-cluster:10000 -y
cluster list
EOF

$ TASH_CLUSTER_DEMO=1 ./build/tash.out /tmp/tour.sh
allocated demo-n10000 on demo-cluster (jobid 10000)
demo-cluster:10000  a100  demo-n10000  running
launched instance 1 (window '1') — state=running
demo-cluster:10000  a100  demo-n10000  running
  myproject  1 instances
    [1]  running  1
cancelled allocation demo-cluster:10000
(no allocations)
```

## What demo mode doesn't exercise

- **Real SSH ControlMaster multiplexing** — the demo SSH client is a
  no-op; your real `ssh_host` / `~/.ssh/config` / Duo flow isn't
  touched.
- **Real SLURM** — fake `sbatch` always accepts, fake `squeue`
  reflects the fake state. No real route-selection or
  partition-state probing.
- **Real tmux** — no windows are actually created; you won't see
  your command's stdout anywhere.
- **Real notifications** — `DemoNotifier` writes `[demo notify] …`
  to stderr instead of calling `osascript` / `notify-send`. A real
  Stop-hook event will be printed, but no banner.

For the real-cluster smoke suite (short, opt-in, pointed at your own
cluster profile), see `tests/smoke/cluster/README.md`.

## Using demo mode in tests

Every integration test in `tests/integration/cluster/` either uses
the Tier-2 stub binaries on `$PATH` (for end-to-end coverage that
exercises the real `SshClient` / `SlurmOps` / `TmuxOps`) or installs
the demo engine directly via `install_demo_engine()` (see
`tests/integration/cluster/demo_mode_smoke_test.cpp` for a worked
example).

If you're contributing a new feature that crosses multiple
subsystems, follow the pattern in
`tests/integration/cluster/demo_mode_smoke_test.cpp` — install the
demo engine, call `dispatch_cluster(...)` with your argv, assert on
`stdout`/`stderr` + `active_engine()->registry()` state. No PATH
overrides, no scenario files, no network.

## See also

- `docs/cluster.md` — full user walkthrough (real clusters).
- `docs/superpowers/specs/2026-04-18-cluster-remote-launcher-design.md`
  — the design spec, if you want to know *why* the demo is shaped
  this way.
