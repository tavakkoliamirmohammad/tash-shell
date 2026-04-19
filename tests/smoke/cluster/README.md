# Tash Cluster — Real-Cluster Smoke Suite

A short, opt-in manual test that drives the `cluster` builtin
against a real SLURM cluster the developer has credentials for.
**Not run in CI** — it requires working SSH + a short allocation —
but it's the only path that exercises live `sbatch` + real ssh
ControlMaster + a real tmux session end-to-end.

Intended cadence: before shipping, before touching anything in
`src/cluster/slurm_parse.cpp` / `src/cluster/ssh_client.cpp` /
`src/cluster/tmux_compose.cpp`, or any time `doctor` warns about
something you want to investigate on a real login node.

## What it covers

One allocation, one instance, the full lifecycle:

```
cluster connect <profile>
cluster doctor <profile>
cluster up      -r <resource> -t 15m
cluster list
cluster launch  --workspace smoke --cmd 'sleep 600'
cluster list        (verify instance appears)
cluster attach      (via expect — detach after ~2s)
cluster sync        (should see no transitions)
cluster kill    smoke/1 -y
cluster down    <alloc-id> -y
cluster list        (verify registry empty)
cluster disconnect <profile>
```

Each step prints `ok` / `FAIL`; the overall script exits non-zero if
any step fails. Total wall-clock: ~2 minutes assuming the node grants
quickly. Resources used: one allocation for ≤ 15 minutes (released
by `cluster down`, or by the SLURM time limit if the script crashes).

## Setup (one-time)

1. **Have a working ssh entry.** Test that `ssh <your-cluster-alias>
   true` succeeds on your laptop. Password + Duo is fine — the
   script calls `cluster connect` first so the prompt happens in a
   clean context.

2. **Copy the template and fill in your cluster details:**

   ```sh
   cp tests/smoke/cluster/smoke-profile.toml.template  \
      tests/smoke/cluster/smoke-profile.toml
   $EDITOR tests/smoke/cluster/smoke-profile.toml
   ```

   Fill in your cluster's ssh alias, account, partition, qos, and gres
   string. The profile file is gitignored — it's yours, don't commit
   it.

3. **Install `expect`** for the attach/detach test (optional; the
   script skips it if `expect` isn't in PATH).

   - Debian/Ubuntu: `sudo apt install expect`
   - Fedora/RHEL:  `sudo dnf install expect`
   - macOS:        `brew install expect`

## Running it

```sh
cd /path/to/tash-shell
./tests/smoke/cluster/run_smoke.sh
```

The script uses your smoke-profile.toml as a distinct
`$TASH_CLUSTER_HOME` (so it can't interfere with your real
`~/.tash/cluster/config.toml`). Output is tagged:

```
[ok  ] cluster connect            (0.8s)
[ok  ] cluster doctor             (1.2s)
[ok  ] cluster up -r a100         (18s)
[ok  ] cluster list shows alloc   (0.1s)
[ok  ] cluster launch smoke       (2.1s)
[ok  ] cluster list shows inst    (0.1s)
[ok  ] cluster attach via expect  (3.0s)   (skipped if expect missing)
[ok  ] cluster sync (no-op)       (1.1s)
[ok  ] cluster kill smoke/1       (0.8s)
[ok  ] cluster down -y            (1.0s)
[ok  ] cluster list is empty      (0.1s)
[ok  ] cluster disconnect         (0.3s)

smoke passed — 12 ok, 0 fail, 0 skip  (28s total)
```

Exit code: 0 on all green, 1 if any step failed, 2 if setup / the
profile is missing.

## What "ok" actually tested

- **connect** — OpenSSH ControlMaster master opens, auth (incl. Duo)
  succeeded.
- **doctor** — `ssh <host> true` + `which sbatch` + `which tmux`
  succeeded on the login node.
- **up** — real `sbatch` accepted the submission; real `squeue`
  showed it pending then Running; tash assigned the compute node
  we got.
- **launch** — on the compute node, `tmux new-session` + `tmux
  new-window` ran cleanly via the multiplexed ssh; the window's
  `pane_pid` is alive.
- **list** — registry persistence + `squeue` reconciliation
  reflects reality.
- **attach** — `ssh -t` + `tmux attach-session` argv was correct
  enough that `expect` saw a prompt-looking-thing and could detach
  via Ctrl-b d.
- **sync** — `squeue` re-reconciliation finds the same job still
  running (0 ghost transitions).
- **kill** — tmux kill-window through ssh worked; instance removed
  from registry.
- **down** — real `scancel`; `cluster list` is empty.

## If a step fails

The script exits after the first FAIL to keep the half-killed
allocation visible for inspection. Common diagnostics:

- **connect FAIL**: `ssh -vvv <your-alias>` for auth / network.
- **doctor FAIL**: check the specific check that failed; `doctor`
  prints a fix hint.
- **up hangs past 5min**: your cluster is busy — there's nothing
  wrong with tash, just the queue. Press `c` at the prompt to
  scancel cleanly; re-run later.
- **launch/attach FAIL**: check that the compute node has tmux in
  PATH (`ssh <login> ssh <compute-node> which tmux` from your
  laptop).
- **down hangs**: real `scancel` sometimes takes seconds. Try
  `cluster sync` from another shell.

If the script crashes or you Ctrl-C out mid-run, don't forget to
clean up the allocation manually — either `cluster down <id> -y`
with the smoke profile, or `scancel -u $USER` on the cluster.

## Why this isn't in CI

- Needs real credentials.
- Needs a real allocation (GPU costs, fair-share hit).
- Depends on queue conditions that aren't reproducible.
- Duo prompts can't be scripted.

What CI gets instead is the Tier-2 stub-binary suite in
`tests/integration/cluster/` — same behaviour, fake transport. This
smoke suite is the "did the real thing actually work on a real
cluster" gut check.
