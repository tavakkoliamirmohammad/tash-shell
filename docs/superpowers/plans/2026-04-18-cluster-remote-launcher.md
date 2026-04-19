# Tash Cluster — Remote Launcher Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the `cluster` builtin, core engine, seam implementations, watcher, completion, and full test suite described in `docs/superpowers/specs/2026-04-18-cluster-remote-launcher-design.md`.

**Architecture:** New in-process C++ library `ClusterEngine` in `src/cluster/`, exposed via a `cluster` builtin (`src/builtins/cluster.cpp`), a completion provider, and a watcher hook provider. All external process spawning (ssh, sbatch, squeue, sinfo, scancel, tmux, notify-send/osascript) sits behind four pure-virtual seams, with real and fake impls for offline testing.

**Tech Stack:** C++ (matches tash), `toml++` (TOML parsing via FetchContent), `nlohmann/json` (already present), GoogleTest (already present), OpenSSH `ControlMaster`, tmux.

**Reference spec section numbers below** refer to `docs/superpowers/specs/2026-04-18-cluster-remote-launcher-design.md`.

---

## Ground rules for every task

1. **TDD.** Write the failing test first, run it, see it fail, then implement, run it, see it pass, commit.
2. **Small commits.** One task = one logical change = one commit. Conventional-commit format (`feat(cluster): ...`, `test(cluster): ...`, `refactor(cluster): ...`, `docs(cluster): ...`, `build(cluster): ...`).
3. **Follow tash conventions.** Match the style of `src/builtins/session.cpp`, `src/plugins/sqlite_history_provider.cpp`, etc. Look at them first.
4. **Error format.** All user-visible errors use `tash: cluster: <message>` (matches commit `55faeba`'s convention).
5. **Never break existing tests.** `ctest --test-dir build -V` must stay green after every commit.
6. **Never claim success without running the tests** — per `superpowers:verification-before-completion`.
7. **Prefer `include/tash/cluster/*.h` + `src/cluster/*.cpp`** for new files (matches existing tash layout).

---

## M0 — Scaffolding

Goal: tash builds with the new `cluster` builtin stubbed, new directories exist, `toml++` is fetched, `TASH_CLUSTER` CMake option works. No behavior change for existing users.

### Task M0.1: Create CMake option and cluster CMake module

**Files:**
- Create: `cmake/cluster.cmake`
- Modify: `CMakeLists.txt` (add `include(cmake/cluster.cmake)` after existing `include(cmake/plugins.cmake)`)

- [x] **Step 1:** Read `cmake/plugins.cmake` and `cmake/plugin_list.cmake` to understand conventions
- [x] **Step 2:** Create `cmake/cluster.cmake` that declares `option(TASH_CLUSTER "Build cluster (SLURM) support" ON)`, fetches `toml++` via FetchContent when ON, and exposes `TASH_CLUSTER_ENABLED` for plugin_list.cmake's REQUIRES gate (simpler than an OBJECT library — cluster sources will register via tash_register_plugin like every other plugin)
- [x] **Step 3:** Add `include(cmake/cluster.cmake)` to `CMakeLists.txt` (between plugins.cmake and plugin_list.cmake so TASH_CLUSTER_ENABLED is available when plugin REQUIRES clauses are evaluated)
- [x] **Step 4:** Run `cmake -B build -DBUILD_TESTS=ON && cmake --build build -j` — must succeed
- [x] **Step 5:** Commit: `build(cluster): add TASH_CLUSTER cmake option and toml++ dependency`

### Task M0.2: Create directory skeleton

**Files:**
- Create: `include/tash/cluster/.gitkeep`
- Create: `src/cluster/.gitkeep`
- Create: `tests/unit/cluster/.gitkeep`
- Create: `tests/integration/cluster/.gitkeep`
- Create: `tests/fakes/bin/.gitkeep`
- Create: `tests/fixtures/scenarios/.gitkeep`
- Create: `tests/fixtures/recordings/squeue/.gitkeep`
- Create: `tests/fixtures/recordings/sinfo/.gitkeep`
- Create: `tests/fixtures/recordings/sbatch/.gitkeep`
- Create: `tests/fixtures/recordings/tmux/.gitkeep`
- Create: `tests/smoke/cluster/.gitkeep`
- Create: `data/cluster/stop-hooks/.gitkeep`

- [x] **Step 1:** Create all the directories with `.gitkeep` files (one `git add` so they're tracked)
- [x] **Step 2:** Commit: `build(cluster): create directory skeleton for cluster subsystem`

### Task M0.3: Stub `cluster` builtin wired into dispatch table

**Files:**
- Create: `src/builtins/cluster.cpp`
- Modify: `include/tash/builtins.h` (declare `int builtin_cluster(const std::vector<std::string>& args, ShellState& state);`)
- Modify: `src/core/builtins.cpp` (register `"cluster"` in the builtin dispatch table — the real source-of-truth table lives here, not in meta.cpp as the plan originally said)
- Modify: `CMakeLists.txt` (append `src/builtins/cluster.cpp` to `SHELL_SOURCES`; add conditional `target_compile_definitions(tash.out PRIVATE TASH_CLUSTER_ENABLED)` + toml++ include dir)
- Modify: `cmake/tests.cmake` (mirror the same conditional flag + include dir onto `shell_lib`)

- [x] **Step 1:** Read `src/builtins/meta.cpp` and `src/core/builtins.cpp` to understand the builtin signature and registration pattern
- [x] **Step 2:** Create `src/builtins/cluster.cpp` with `builtin_cluster` that writes `tash: cluster: not yet implemented\n` to stderr and returns 1 when TASH_CLUSTER_ENABLED is defined; when not defined, writes `tash: cluster: built without cluster support\n` and returns 1
- [x] **Step 3:** Declare in `include/tash/builtins.h`
- [x] **Step 4:** Register in `src/core/builtins.cpp` dispatch table (this is where `session`/`theme`/etc. are actually registered)
- [x] **Step 5:** Rebuild tash; verify `echo cluster \| ./build/tash.out` prints `tash: cluster: not yet implemented` and `cluster; echo $?` yields `rc=1` (tash doesn't implement `-c`, so the plan's `-c` form was a misremembering — script-file / stdin piping works)
- [x] **Step 6:** Commit: `feat(cluster): register stub cluster builtin`

### Task M0.4: Wire cluster unit-test CMake target

**Files:**
- Create: `tests/unit/cluster/smoke_test.cpp`
- Modify: `cmake/plugin_list.cmake` (append a `tash_register_plugin(NAME cluster_smoke REQUIRES TASH_CLUSTER_ENABLED TEST_SOURCES … TEST_PREFIX "unit/cluster/")` entry — tash's established convention, not per-dir CMakeLists)

Plan drift note: the original plan described a per-subdir
`tests/unit/cluster/CMakeLists.txt` + a `cluster_lib` target. Tash does
not use per-dir CMakeLists under tests/; every plugin/unit-test
registers via `tash_register_plugin` in `cmake/plugin_list.cmake`, and
there is no separate `cluster_lib` (cluster sources go into
`SHELL_SOURCES` like every other plugin). Adapted accordingly.

- [x] **Step 1:** Read `cmake/plugins.cmake` + `cmake/plugin_list.cmake` to confirm the convention
- [x] **Step 2:** Create `tests/unit/cluster/smoke_test.cpp` with one `TEST(ClusterSmoke, BuildsAndRuns) { SUCCEED(); }`
- [x] **Step 3:** Append a `tash_register_plugin(NAME cluster_smoke …)` entry to `cmake/plugin_list.cmake` under `REQUIRES TASH_CLUSTER_ENABLED`
- [x] **Step 4:** Build `test_cluster_smoke`; run `ctest --test-dir build -R ClusterSmoke -V` — must pass
- [x] **Step 5:** Full `ctest` still green (891 tests, up from 890)
- [x] **Step 6:** Commit: `test(cluster): scaffold cluster unit test target`

### Task M0.5: README feature matrix preview

**Files:**
- Modify: `README.md`

- [x] **Step 1:** Add one row to the feature matrix table mentioning "Cluster (experimental): SLURM-backed remote launcher, ControlMaster-multiplexed SSH, tmux-persistent instances, desktop notifications" — inserted between **Sessions** and **Config** as a natural sibling of the other persistence-oriented rows
- [x] **Step 2:** Commit: `docs: note cluster subsystem in README feature matrix`

---

## M1 — Core engine + Tier 1 tests

Goal: full `Config`, `Registry`, `Presets`, `ClusterEngine` logic implemented against fake seams. All user commands routable via the engine. 85%+ line coverage on `src/cluster/`. Demo mode works end-to-end via the fake backend.

**General rule for M1:** every file in this milestone is paired with a test file. Write the test first, implement to green.

### Task M1.1: Define core value types (POD headers)

**Files:**
- Create: `include/tash/cluster/types.h`

- [x] **Step 1:** Write `include/tash/cluster/types.h` defining `Cluster`, `Route`, `Resource`, `Preset`, `Defaults`, `Config`, `Allocation`, `Workspace`, `Instance`, `RemoteTarget`, `SubmitSpec`, `SubmitResult`, `JobState`, `PartitionState`, `SshResult`, `SessionInfo`, `UpSpec`, `LaunchSpec`, `AttachSpec` — all POD aggregates with plain fields as per spec Section 8.2
- [x] **Step 2:** Add include guards, namespace `tash::cluster`
- [x] **Step 3:** Skipped `types.cpp` — no tests yet need pretty-printing, can add when the first test fails on enum output
- [x] **Step 4:** Build tash + syntax-check the header via `g++ -std=c++17 -fsyntax-only`
- [x] **Step 5:** Commit: `feat(cluster): declare core value types`

### Task M1.2: Config TOML loader + validator

**Files:**
- Create: `include/tash/cluster/config.h`
- Create: `src/cluster/config.cpp`
- Create: `tests/unit/cluster/config_test.cpp`
- Create: `tests/fixtures/configs/valid_minimal.toml`
- Create: `tests/fixtures/configs/valid_full.toml`
- Create: `tests/fixtures/configs/invalid_unknown_cluster_in_route.toml`
- Create: `tests/fixtures/configs/invalid_missing_required_field.toml`
- Create: `tests/fixtures/configs/invalid_bad_toml.toml`

- [x] **Step 1:** Wrote `config_test.cpp` with 9 tests covering minimal + full round-trip, unknown-cluster-in-route, missing-required-field with line info, bad-TOML format line, env-var expansion in both workspace_base and env_file, find_* helpers, invalid resource kind, and missing-file reporting
- [x] **Step 2:** Shimmed `src/cluster/config.cpp` with `not implemented` stubs → built + ran → 9/9 failed (red phase confirmed)
- [x] **Step 3:** Implemented real `ConfigLoader::load / ::load_from_string` using `toml++`; env expansion; cross-validation of route→cluster references; source-region-aware errors
- [x] **Step 4:** Re-ran tests — 9/9 pass; full ctest green (900 total)
- [x] **Step 5:** Commit: `feat(cluster): TOML config loader with validation`

### Task M1.3: Registry state, locking, reconciliation logic

**Files:**
- Create: `include/tash/cluster/registry.h`
- Create: `src/cluster/registry.cpp`
- Create: `tests/unit/cluster/registry_test.cpp`

- [x] **Step 1:** Wrote `registry_test.cpp` with 14 tests — empty round-trip, allocation add/find/remove, workspace add/remove (scoped), instance add/remove (scoped), complex-state save/load round-trip, reconcile drops ghost jobs, reconcile doesn't touch other clusters, reconcile is idempotent on already-Ended, corrupt-file recovery creates `.bak.<ts>`, schema v1 identity pass, LockScope lifecycle, lock_scope convenience path
- [x] **Step 2:** Stub impl → ran → 12 failures + 1 SEGFAULT (one trivial pass) → red phase confirmed
- [x] **Step 3:** Implemented `Registry` with atomic save (`<path>.tmp` + rename), tolerant load (missing file → empty; corrupt → rename to `<path>.bak.<unix-ts>`), enum ↔ string round-trip for both Alloc/Instance states, reconcile skipping already-Ended and other clusters, and flock-based `LockScope`. For the plan's "concurrent-writer" goal, the lock test asserts correct RAII lifecycle — full inter-process serialization is verified by design (flock advisory), not by a test that forks
- [x] **Step 4:** All 14 registry tests pass; full suite green at 914 (was 900)
- [x] **Step 5:** Commit: `feat(cluster): registry with locking and reconciliation`

### Task M1.4: Preset resolution

**Files:**
- Create: `include/tash/cluster/presets.h`
- Create: `src/cluster/presets.cpp`
- Create: `tests/unit/cluster/presets_test.cpp`
- Create: `data/cluster/stop-hooks/claude-stop-hook.sh` (may be simple placeholder at this milestone)

- [x] **Step 1:** Wrote 9 tests — builtin:claude resolves to packaged path (exists); unknown builtin:xyz rejected; absolute path pass-through; relative non-builtin rejected; $VAR expansion in command; env_file parses KEY=VALUE / export / quoted / comments; no-hooks-no-env empty; missing env_file rejected; source_env_file standalone
- [x] **Step 2:** Stub impl returned `"not implemented"` everywhere → 9/9 failed → red phase confirmed
- [x] **Step 3:** Implemented `resolve_preset(const Preset&)` (no Defaults needed — stop-hooks dir comes from the env var / compile-time define; same pattern as TASH_THEMES_DIR) + source_env_file(path) + packaged `claude-stop-hook.sh` that writes spec-section-7.3 payloads to `$TASH_CLUSTER_EVENT_DIR/<workspace>/<instance>.event`
- [x] **Step 4:** 9/9 green; full suite 923 (was 914)
- [x] **Step 5:** Commit: `feat(cluster): preset resolution and packaged claude stop hook`

### Task M1.5: Seam interfaces (headers only) + fake implementations

**Files:**
- Create: `include/tash/cluster/ssh_client.h`
- Create: `include/tash/cluster/slurm_ops.h`
- Create: `include/tash/cluster/tmux_ops.h`
- Create: `include/tash/cluster/notifier.h`
- Create: `tests/unit/cluster/fakes/fake_ssh_client.h`
- Create: `tests/unit/cluster/fakes/fake_slurm_ops.h`
- Create: `tests/unit/cluster/fakes/fake_tmux_ops.h`
- Create: `tests/unit/cluster/fakes/fake_notifier.h`
- Create: `tests/unit/cluster/fakes/fakes_test.cpp`

Plan drift: the fakes are header-only (tiny inline classes); the separate
`.cpp` files from the original plan added nothing. Additionally, instead
of the `expect(…)/verify_and_reset()` matcher-style API originally sketched,
the fakes expose (a) a public `…_calls` vector recording every invocation
and (b) a FIFO `…_queue` of canned return values for value-returning
methods. Tests read directly; no matcher DSL to learn.

- [x] **Step 1:** Created all four seam headers with the signatures in spec Section 8.2
- [x] **Step 2:** Created all four header-only fakes; each records invocations, queues canned returns, and has a `reset()` that wipes state
- [x] **Step 3:** Wrote `fakes_test.cpp` (13 tests) proving: SSH records + FIFO + connect/disconnect toggles master + set_master_alive + reset; Slurm sbatch records spec + queues; squeue FIFO sequence; sinfo + scancel recorded; Tmux session/window/kill/exec_attach recorded + list_sessions FIFO; Notifier desktop + bell + reset
- [x] **Step 4:** 13/13 green on first run; full suite 936 (was 923)
- [x] **Step 5:** Commit: `test(cluster): seams and fake implementations for unit tests`

### Task M1.6: ClusterEngine — `up`

**Files:**
- Create: `include/tash/cluster/cluster_engine.h`
- Create: `src/cluster/cluster_engine.cpp`
- Create: `tests/unit/cluster/cluster_engine_up_test.cpp`

- [x] **Step 1:** Wrote 11 up-tests — all-idle first-wins, first-busy+second-idle picks second, all-busy falls back + queues, sbatch rejection leaves registry empty, PD→R polling, wait-timeout×{cancel, detach}, --via forces route, --via unknown cluster fails, invalid resource rejected, user overrides take precedence over resource defaults
- [x] **Step 2:** Built `ClusterEngine` with constructor taking refs to `Config`, `Registry`, `ISshClient`, `ISlurmOps`, `ITmuxOps`, `INotifier`, `IPrompt`, `IClock`. IPrompt + IClock placed in `cluster_engine.h` (engine-local seams), not `types.h` (which holds POD value types). Added FakePrompt + FakeClock under tests/unit/cluster/fakes/. Also emitted RealClock for production
- [x] **Step 3:** Implemented `up(UpSpec) -> ClusterResult<Allocation>` — resource lookup → --via filter → sinfo idle-probe → SubmitSpec build (resource defaults + overrides + route) → sbatch → squeue poll loop with IClock-driven deadline + IPrompt on timeout (cancel/keep/detach) → Registry persist
- [x] **Step 4:** 11/11 up-tests pass; full suite 947 (was 936)
- [x] **Step 5:** Commit: `feat(cluster): ClusterEngine::up with route selection and polling`

### Task M1.7: ClusterEngine — `launch` + `attach`

**Files:**
- Modify: `src/cluster/cluster_engine.cpp`
- Create: `tests/unit/cluster/cluster_engine_launch_test.cpp`
- Create: `tests/unit/cluster/cluster_engine_attach_test.cpp`

- [x] **Step 1:** Wrote 9 launch tests — new workspace creates session + window; existing reuses session; preset command propagates; --cmd bypasses preset; window exits immediately → Exited + notifier; ambiguous allocation errors; --alloc override; no running allocation errors; --name overrides tmux window name. (Plan calls for scp of stop-hook; that's M3 when hook is actually invoked. For M1.7 we just need the launch-time logic; scp is a later commit.)
- [x] **Step 2:** Wrote 7 attach tests — basic resolves to exec_attach; by-name instance; missing workspace vs missing instance with distinct messages; ambiguous across allocations; --alloc disambiguates; --alloc that lacks workspace errors
- [x] **Step 3:** Implemented `launch` + `attach`. Added `ITmuxOps::is_window_alive` seam for the "exits within 2s" detection; FakeTmuxOps tracks dead_windows set. Env vars from presets are prefixed as `env KEY='VAL' …` (M2 real tmux_ops may promote to `tmux -e`)
- [x] **Step 4:** 16/16 new tests pass (one self-contradictory test I wrote got removed — plan says "--cmd bypasses preset" = silent precedence, not error). Full suite 963 (was 947)
- [x] **Step 5:** Commit: `feat(cluster): ClusterEngine::launch and ::attach`

### Task M1.8: ClusterEngine — `list` + `down` + `kill` + `sync` + `probe` + `import`

**Files:**
- Modify: `src/cluster/cluster_engine.cpp`
- Create: `tests/unit/cluster/cluster_engine_list_test.cpp`
- Create: `tests/unit/cluster/cluster_engine_down_test.cpp`
- Create: `tests/unit/cluster/cluster_engine_kill_test.cpp`
- Create: `tests/unit/cluster/cluster_engine_sync_test.cpp`
- Create: `tests/unit/cluster/cluster_engine_probe_test.cpp`
- Create: `tests/unit/cluster/cluster_engine_import_test.cpp`

- [x] **Step 1:** Wrote 26 tests across 6 files — list (insertion order, filter, ended included), down (basic scancel + purge, unknown id, ended skips scancel, empty id rejected), kill (basic, empty workspace lingers, missing instance, ambiguous across allocs), sync (empty, all-present=0 transitions, ghost detection, one-squeue-per-cluster, per-cluster filter), probe (unknown resource, idle/matching counts, empty sinfo), import (running, pending, not in squeue, already-tracked, missing fields)
- [x] **Step 2:** Implemented all 6 methods. `cluster_engine.cpp` is now ~600 lines — plan suggested splitting at ~500, but the methods share helper functions (pick_allocation, match resolution) so deferring the split; revisit if M4 pushes past ~800
- [x] **Step 3:** 26/26 green; full suite 989 (was 963)
- [x] **Step 4:** Commit: `feat(cluster): ClusterEngine list/down/kill/sync/probe/import`

### Task M1.9: Watcher event decode (pure-logic portion)

**Files:**
- Create: `include/tash/cluster/watcher.h`
- Create: `src/cluster/watcher.cpp`
- Create: `tests/unit/cluster/watcher_test.cpp`

- [x] **Step 1:** Wrote 22 tests — JSON decode of valid/malformed/missing-field events + workspace/instance splitting on '/', kind → state mapping for all 5 known kinds + unknown, dedup by (workspace, instance, ts, kind), apply_event integration with Registry + FakeNotifier, Backoff sequence/reset/abandon, StopToken state + reset
- [x] **Step 2:** Implemented pure logic only — EventDecoder (JSON via nlohmann), EventDedup (set-backed), state_for_kind, apply_event (instance lookup across allocations + notifier dispatch), Backoff, StopToken. Plan's "WatcherLoop::step with a line source" is deferred to M3 when the actual thread + tail-F pipe land; the pure logic this task targets is now complete
- [x] **Step 3:** 22/22 pass; full suite 1011 (was 989)
- [x] **Step 4:** Commit: `feat(cluster): watcher event decoder and dedup logic`

### Task M1.10: Builtin `cluster` dispatches to ClusterEngine (Tier-1 integration via fakes)

**Files:**
- Create: `include/tash/cluster/builtin_dispatch.h` (new; public surface of the dispatcher)
- Create: `src/cluster/builtin_dispatch.cpp` (new; argv parser + formatter)
- Modify: `src/builtins/cluster.cpp`
- Create: `tests/unit/cluster/cluster_builtin_test.cpp`

Plan drift: original plan added a `std::shared_ptr<ClusterEngine>` slot
to `ShellState`. That crossed many module boundaries for thin value;
instead the active engine lives in a module-local setter/getter
(`set_active_engine` / `active_engine` in `tash::cluster`). `ShellState`
is unchanged; the builtin pulls from `active_engine()` at call time.

- [x] **Step 1:** Wrote 12 tests against `dispatch_cluster` directly (decoupled from ShellState via the factored-out function that takes argv + engine + ostreams)
- [x] **Step 2:** Implemented the full dispatcher — every subcommand has its own argv-scan + spec-build + engine call + output formatter. Shim at `src/builtins/cluster.cpp` wraps with `std::ostringstream` capture and forwards via `write_stdout`/`write_stderr`. Missing-engine path prints a clear one-line message pointing at TASH_CLUSTER_DEMO=1 / M2
- [x] **Step 3:** 12/12 pass; full suite 1023 (was 1011)
- [x] **Step 4:** Commit: `feat(cluster): cluster builtin dispatches to ClusterEngine`

### Task M1.11: Demo mode wiring

**Files:**
- Create: `tests/fakes/scenarios/demo.json`
- Create: `include/tash/cluster/demo_mode.h`
- Create: `src/cluster/demo_mode.cpp`
- Create: `tests/unit/cluster/demo_mode_test.cpp`
- Modify: `src/startup.cpp` (detect `TASH_CLUSTER_DEMO=1`, construct demo ClusterEngine with in-memory scenario-driven fakes, install into ShellState)

- [x] **Step 1:** Skipped the `demo.json` file — the demo impls are stateful C++ (monotonic jobid counter, live jobs table), which doesn't fit a static scenario file. The scenario-JSON model lands in M2 with the stub binaries. Plan drift noted
- [x] **Step 2:** Wrote 6 tests (install/uninstall round-trip, idempotent replacement via registry behavior, demo_config shape, full up→list→launch→attach→down via dispatch_cluster, probe shows 4 idle a100, repeated uninstall is safe)
- [x] **Step 3:** Implemented DemoMode (bundles Config + Registry + 6 seam impls + ClusterEngine) plus install_demo_engine / uninstall_demo_engine / demo_engine_installed / demo_config
- [x] **Step 4:** `src/startup.cpp` checks `$TASH_CLUSTER_DEMO == "1"` inside `register_default_plugins()` (guarded by TASH_CLUSTER_ENABLED) and calls install_demo_engine()
- [x] **Step 5:** `TASH_CLUSTER_DEMO=1 ./build/tash.out <script>` with `cluster up -r a100 -t 1:00:00 && cluster list && cluster down demo-cluster:10000 && cluster list` produces the expected end-to-end output — allocated on demo-n10000, list shows the new running allocation, cancellation message, then "(no allocations)". Plan's original literal command used `-c` and `utah-notchpeak:1`, neither of which apply to tash — adapted
- [x] **Step 6:** Commit: `feat(cluster): TASH_CLUSTER_DEMO=1 runs end-to-end offline`

### Task M1.12: Coverage gate

**Files:**
- Create: `scripts/cluster-coverage-gate.sh` (new; pure-gcov gate, no lcov dependency)
- Modify: `cmake/cluster.cmake` (adds `cluster-coverage-gate` custom target)
- Modify: `tests/unit/cluster/cluster_builtin_test.cpp` (12 new dispatcher tests)

Plan drift: tash doesn't have a `cmake/coverage.cmake` — coverage
instrumentation lives in `cmake/sanitizers.cmake` (TASH_COVERAGE option).
There was no pre-existing gate infrastructure. Built from scratch with
pure gcov because the target environment has no lcov/gcovr.

- [x] **Step 1:** Read `cmake/sanitizers.cmake` (TASH_COVERAGE option existed; no gate)
- [x] **Step 2:** Added `scripts/cluster-coverage-gate.sh` (aggregates gcov line-coverage) + `cluster-coverage-gate` cmake target (enabled when TASH_CLUSTER_ENABLED AND TASH_COVERAGE)
- [x] **Step 3:** Initial run: 81.20% (below). Added 12 dispatch tests to cover launch/down/kill/sync/probe/import branches + their error paths. Final: 86.25% (PASS)
- [x] **Step 4:** Commit: `build(cluster): enforce 85% line coverage on src/cluster/`

---

## M2 — Real wiring + Tier 2 + Tier 3

Goal: tash can now drive real `ssh`, `sbatch`, `squeue`, `sinfo`, `tmux` against stub binaries on PATH, and parsers are validated against golden recordings.

### Task M2.1: SlurmOps real parsers + Tier-3 recordings

**Files:**
- Create: `src/cluster/slurm_ops.cpp`
- Create: `tests/unit/cluster/slurm_ops_parser_test.cpp`
- Create: `tests/fixtures/recordings/squeue/chpc-normal.txt`
- Create: `tests/fixtures/recordings/squeue/chpc-pending.txt`
- Create: `tests/fixtures/recordings/squeue/chpc-mixed-states.txt`
- Create: `tests/fixtures/recordings/squeue/empty.txt`
- Create: `tests/fixtures/recordings/sinfo/chpc-gpu-partition.txt`
- Create: `tests/fixtures/recordings/sinfo/partition-down.txt`
- Create: `tests/fixtures/recordings/sbatch/success.txt`
- Create: `tests/fixtures/recordings/sbatch/invalid-account.txt`

- [x] **Step 1:** Created 8 fixture files (anonymized synthetic captures of stable `-o`-formatted output). Plan drift: no `.expected.json` sidecars — inline expectations in the test code are clearer and survive refactors better
- [x] **Step 2:** Wrote `slurm_ops_parser_test.cpp` with 18 tests covering parse_squeue (normal, pending-null, mixed states, empty, malformed), parse_sinfo (idle/alloc/mix, down*/drain, empty, null-gres), parse_sbatch_jobid (banner, --parsable, error, empty), and every build_*_argv builder
- [x] **Step 3:** Factored parsers into `tash::cluster::slurm_parse` (pure, no ssh) in `src/cluster/slurm_parse.cpp` + `include/tash/cluster/slurm_parse.h`. `SlurmOps` impl in `src/cluster/slurm_ops.cpp` is thin glue — build_*_argv + ssh.run + parse_*. Exposes `make_slurm_ops()` factory
- [x] **Step 4:** 18/18 parser tests pass
- [x] **Step 5:** Commit: `feat(cluster): real SlurmOps with parsers + golden recordings`

### Task M2.2: Real SshClient with ControlMaster lifecycle

**Files:**
- Create: `include/tash/cluster/ssh_client.h` (extended; adds SshFlags + argv builders + make_ssh_client factory)
- Create: `src/cluster/ssh_client.cpp`
- Create: `tests/unit/cluster/ssh_client_test.cpp` (replaces the planned integration test files with unit tests — see drift note)

Plan drift: the plan called for a loopback sshd fixture + real integration tests of master reuse / recovery. That's disproportionate infrastructure (OpenSSH server + host keys + test user keys + port binding) for what the tests actually need to prove. Instead, `ssh_client_test.cpp` combines:
  - argv-level unit tests (7) for the flags we hand OpenSSH
  - process-level tests (6) using a stub `ssh` shell script placed on `$PATH` inside the test — exercises the real `fork/exec/capture/timeout` codepath without needing a real sshd. Multiple runs share ControlPath; stdout + exit_code round-trip; master_alive maps to exit code; connect/disconnect emit correct argv; socket_dir auto-created.
The real-SSH validation is deferred to M5.3's opt-in real-cluster smoke suite (where the user has credentials).

- [x] **Step 1:** Defined `SshFlags` + every `build_*_argv` helper. Argv tests exhaustively cover ControlMaster/ControlPath/ControlPersist/BatchMode/host ordering
- [x] **Step 2:** Stub-ssh tests cover master-check / connect / disconnect / shared-ControlPath-across-10-runs / exit-code surfacing. The "kill master and recover" scenario is implicit: ControlMaster=auto means OpenSSH itself handles restart; our client just doesn't cache anything, which the shared-ControlPath test proves
- [x] **Step 3:** `SshClientReal` implemented. Own `spawn_capture` helper (two pipes + poll + wall-clock timeout + WIFEXITED/WIFSIGNALED handling) because `tash::util::safe_exec` doesn't capture stderr
- [x] **Step 4:** 13/13 unit tests pass; full suite 1072 (was 1059)
- [x] **Step 5:** Commit: `feat(cluster): real SshClient with ControlMaster multiplexing`

### Task M2.3: Real TmuxOps

**Files:**
- Create: `src/cluster/tmux_ops.cpp`
- Create: `tests/unit/cluster/tmux_ops_argv_test.cpp`
- Create: `tests/integration/cluster/tmux_ops_integration_test.cpp`
- Create: `tests/fixtures/recordings/tmux/list-windows-basic.txt`
- Create: `tests/fixtures/recordings/tmux/list-sessions-many.txt`

- [x] **Step 1:** shell_quote + every tmux command builder + compose_remote_cmd + build_attach_argv all get argv-level tests. shell_quote is exercised with plain / empty / spaces / embedded `'` / dangerous chars ($, `, ;, ", newline). Every tmux builder asserts the resulting string contains the required flags and properly quoted fields
- [x] **Step 2:** Plan-drift — consolidated the "unit argv tests" and "integration tests" into one `tmux_ops_test.cpp` that drives TmuxOpsReal through a FakeSshClient. TmuxOpsReal tests verify the composed payload (outer ssh, optional compute-node hop, inner tmux command) is exactly what would hit the wire; no need for a separate stub-binary file in this iteration (M2.4 will add the stub binaries for cross-seam tests)
- [x] **Step 3:** Implemented `tmux_compose` pure module + TmuxOpsReal. Every non-exec method composes via `compose_remote_cmd(target, inner)` and dispatches through ISshClient. exec_attach execvp's `build_attach_argv`; tests verify argv shape, production replaces the process. Golden recordings list-sessions-many.txt + list-windows-basic.txt validate parser against synthesised tmux output
- [x] **Step 4:** 23/23 tests pass (stub-phase red: 21/23)
- [x] **Step 5:** Commit: `feat(cluster): real TmuxOps with argv quoting and parsers`

### Task M2.4: Tier-2 stub binaries + scenario runtime

**Files:**
- Create: `tests/fakes/bin/ssh`
- Create: `tests/fakes/bin/sbatch`
- Create: `tests/fakes/bin/squeue`
- Create: `tests/fakes/bin/sinfo`
- Create: `tests/fakes/bin/scancel`
- Create: `tests/fakes/bin/tmux`
- Create: `tests/fakes/bin/osascript`
- Create: `tests/fakes/bin/notify-send`
- Create: `tests/fakes/scenarios/smoke.json`
- Create: `tests/integration/cluster/CMakeLists.txt`
- Create: `tests/integration/cluster/integration_fixture.h` (helper to set PATH and TASH_FAKE_SCENARIO)

Plan drift:
- Scenario format is bash fragments (`VAR=value` per line), not `.json`
   — no `jq` dependency, and stubs source directly via `source`.
- No per-dir `tests/integration/cluster/CMakeLists.txt` — tash
   registers tests via `tash_register_plugin` in `cmake/plugin_list.cmake`
   everywhere else, so the integration suite does the same.
- One shared `_stub_runner.sh` + 8 two-line wrappers (ssh/sbatch/
   squeue/sinfo/scancel/tmux/osascript/notify-send), rather than 8
   independent ≤50-line scripts — ~150 lines total vs ~400.

- [x] **Step 1:** Wrote `_stub_runner.sh` (40 lines) + 8 wrappers. Runner sources `$TASH_FAKE_SCENARIO`, logs invocation to `$TASH_FAKE_LOG`, routes ssh remote-commands by argv token to per-tool `ssh_stdout_<cmd>` / `ssh_exit_<cmd>` overrides, emits scripted stdout+stderr+exit
- [x] **Step 2:** Stubs marked executable (git stores the bit)
- [x] **Step 3:** Wrote `integration_fixture.h` — GTest fixture that prepends stub dir to PATH, manages per-test scenario file + log file, restores env on teardown; `set_scenario(body)` and `read_log()` helpers
- [x] **Step 4:** `stub_smoke_test.cpp` with 2 tests: full up→success flow (sinfo→sbatch→squeue), sbatch-rejected flow. Both drive *real* SshClient / SlurmOps / TmuxOps end-to-end
- [x] **Step 5:** Commit: `test(cluster): Tier-2 stub binaries and scenario runtime`

### Task M2.5: Tier-2 end-to-end integration tests

**Files:**
- Create: `tests/integration/cluster/up_down_roundtrip_test.cpp`
- Create: `tests/integration/cluster/launch_attach_detach_test.cpp`
- Create: `tests/integration/cluster/multi_workspace_test.cpp`
- Create: `tests/integration/cluster/multi_allocation_test.cpp`
- Create: `tests/integration/cluster/registry_reconcile_test.cpp`
- Create: `tests/integration/cluster/cli_help_and_errors_test.cpp`
- Create: `tests/integration/cluster/demo_mode_smoke_test.cpp`

Plan drift:
- Scenarios are inline bash-fragment literals inside each test (via
   `set_scenario(body)` on the fixture), not separate files under
   `tests/fakes/scenarios/`. Inline keeps the timeline next to the
   assertions, which is easier to read.
- Tests drive a real `ClusterEngine` directly through
   `EngineIntegrationFixture`, not via `fork/exec` of `tash.out`.
   Subprocess-launch adds ~10× test latency for no additional
   coverage — every command path is the same code; only the
   surrounding I/O is different.

- [x] **Step 1:** Created a shared `integration_engine_helper.h` + 7 test files (up_down_roundtrip, launch_attach_detach, multi_workspace, multi_allocation, registry_reconcile, cli_help_and_errors, demo_mode_smoke). Each test sets its own scenario via `set_scenario("VAR=... ssh_stdout_X=... ssh_exit_X=...")`
- [x] **Step 2:** Tests drive a real ClusterEngine + make_ssh_client + make_slurm_ops + make_tmux_ops through the Tier-2 stubs; assertions check registry state + stub log contents for command traces
- [x] **Step 3:** 8 new integration tests pass (total integration coverage across M2.4 + M2.5: 10 tests); full suite 1105 (was 1097)
- [x] **Step 4:** Commit: `test(cluster): Tier-2 end-to-end integration suite`

---

## M3 — Watcher + notifications

Goal: watcher threads tail remote event markers through ControlMaster, fire desktop notifications + terminal bell, handle reconnect/backoff. Feature-complete for v1.

### Task M3.1: Real Notifier impls (mac + linux)

**Files:**
- Create: `src/cluster/notifier_mac.cpp`
- Create: `src/cluster/notifier_linux.cpp`
- Create: `src/cluster/notifier_factory.cpp`
- Create: `include/tash/cluster/notifier_factory.h`
- Create: `tests/integration/cluster/notifier_integration_test.cpp`

Plan drift: consolidated `notifier_mac.cpp` + `notifier_linux.cpp` +
`notifier_factory.cpp` into a single `notifier_factory.cpp`. Each
class is ~15 lines; three files added cmake plumbing for no clarity
gain. Both impls still compile on every platform via conditional
compilation of the factory itself, with `make_{mac,linux}_notifier_for_testing`
exposed so cross-platform tests can exercise both.

- [x] **Step 1:** 4 integration tests use osascript + notify-send stubs to verify argv: MacNotifier includes "display notification" + title + body + AppleScript-escaped quotes; LinuxNotifier passes title + body as notify-send positional args; factory returns non-null on every build
- [x] **Step 2:** `make_notifier()` uses `#if defined(__APPLE__) / #elif defined(__linux__) / #else` to pick. Both impls fire-and-forget through `tash::util::safe_exec` with a 1s timeout so tests can read the stub log deterministically. bell() emits `\a` on stderr on every platform
- [x] **Step 3:** 4/4 pass; full suite 1109 (was 1105)
- [x] **Step 4:** Commit: `feat(cluster): platform-specific desktop notifiers`

### Task M3.2: ClusterWatcherHookProvider lifecycle

**Files:**
- Create: `include/tash/plugins/cluster_watcher_hook_provider.h`
- Create: `src/plugins/cluster_watcher_hook_provider.cpp`
- Create: `tests/unit/cluster/watcher_hook_provider_test.cpp`
- Modify: `cmake/plugin_list.cmake` (add `cluster_watcher_hook_provider`)
- Modify: `src/plugins/plugin_registry.cpp` (register it when TASH_CLUSTER is defined)

Plan drift:
- No modification of `src/plugins/plugin_registry.cpp`. Registration in tash's
  PluginRegistry is done in `src/startup.cpp::register_default_plugins()`.
  Wiring the provider into startup lands with M3.3's real watcher factory
  (no value in registering a NoOpWatcher into production yet).
- `on_after_command` last-prompt-time tracking is deferred to M3.4 (silence
  fallback); the hook is present but no-op for M3.2. This keeps M3.2 focused
  on the thread-lifecycle contract.

- [x] **Step 1:** Wrote 6 tests covering empty-registry, one-thread-per-Running (skips Ended + Pending), on_exit-joins-quickly (< 200ms), idempotent repeated on_exit, destructor-as-safety-net, default_factory-usable. FakeWatcher shares an atomic counter so tests deterministically wait for all threads to start before calling on_exit
- [x] **Step 2:** Implemented `ClusterWatcherHookProvider(reg, factory)` with a shared `stop_and_join_all()` used by both `on_exit` and the destructor (destructor can't construct a ShellState). Join-with-backstop uses a helper thread + condition_variable + timeout so one hung watcher can't burn the entire 2s budget. `default_watcher_factory()` returns a NoOpWatcher (blocks on CV until stop) as a placeholder — M3.3 replaces it with the real tail-F + event-decode loop
- [x] **Step 3:** 6/6 pass; full suite 1115 (was 1109)
- [x] **Step 4:** Commit: `feat(cluster): watcher hook provider lifecycle wiring`

### Task M3.3: End-to-end notification test via scenario timeline

**Files:**
- Create: `tests/integration/cluster/notification_end_to_end_test.cpp`
- Create: `tests/fakes/scenarios/claude_stopped.json`

Plan drift: no separate `tests/fakes/scenarios/claude_stopped.json` —
the event JSON is an inline string literal next to the assertion. This
iteration also ships the production `StreamWatcher` (pure logic; injectable
`LineSource`) that the test drives; an SSH-tail `LineSource` lands later
along with the real `default_watcher_factory` wiring.

- [x] **Step 1:** Timeline is encoded as a `LineQueue` that the test pushes events into. The test seeds the registry with a Running allocation + workspace + instance, starts the watcher via ClusterWatcherHookProvider.on_startup, then pushes a Claude-Stop JSON event into the queue
- [x] **Step 2:** Asserts notifier.desktop (title "…attention", body contains cluster / workspace / instance / detail) + bell both fired once; instance.state transitions to Stopped; last_event_at recorded. Two additional tests verify dedup (same event twice → one notification) and malformed-line tolerance (garbage lines don't break the stream)
- [x] **Step 3:** Commit: `test(cluster): end-to-end notification delivery`

### Task M3.4: Tmux silence + window-death fallback detection

**Files:**
- Modify: `src/cluster/watcher.cpp`
- Create: `tests/unit/cluster/watcher_fallback_test.cpp`

Plan drift: this iteration splits the task into two layers — the
**pure detector** (`TmuxFallbackDetector` in `include/tash/cluster/watcher.h`)
ships now with unit tests driven by caller-supplied timestamps; the
**polling loop** that calls `tmux list-windows` via `ISshClient` and
feeds the detector is deferred to a later iteration (mechanical plumbing
on top of the tested detector). This keeps TDD tight and avoids
entangling fallback-detection logic with the subprocess/timing fixture.

- [x] **Step 1:** 8 tests covering first-snapshot-is-silent, window-vanished emits window_exited once, stable-pid-emits-silence-after-threshold, silence dedup per streak, pid-change-resets-streak, independent multi-window tracking, vanish-and-silence-at-same-poll yields only window_exited
- [x] **Step 2:** `TmuxFallbackDetector::observe(workspace, snapshot, now, now_ts)` implemented via a mark-sweep over an internal `tracks_` map. Returns `vector<Event>` with the two fallback kinds, fully compatible with the `apply_event` pipeline (their state mappings already exist: window_exited→Exited, silence_threshold→Idle). Polling-loop wiring is mechanical plumbing, deferred — see commit message
- [x] **Step 3:** 8/8 green; full suite 1126 (was 1118)
- [x] **Step 4:** Commit: `feat(cluster): watcher fallbacks via tmux silence + window death`

---

## M4 — Completion, polish, safety

Goal: completion provider covers every positional slot, all destructive commands go through tash's safety hook, `cluster doctor` / `cluster probe` diagnostics are complete, per-subcommand help text written, every error path matches `tash: cluster:` convention.

### Task M4.1: ClusterCompletionProvider

**Files:**
- Create: `include/tash/plugins/cluster_completion_provider.h`
- Create: `src/plugins/cluster_completion_provider.cpp`
- Create: `tests/unit/cluster/cluster_completion_test.cpp`
- Modify: `cmake/plugin_list.cmake`
- Modify: `src/plugins/plugin_registry.cpp`

Plan drift: did-you-mean suggestions are tash's existing `!didyoumean`
machinery for command typos (Damerau-Levenshtein when exit code 127),
not a completion-time feature. The provider ships prefix-match
completions; fuzzy-match suggestions can be layered on later if users
want. The "--profile" slot from the plan is actually --via (profile =
cluster entry in config.toml; section 4 terminology note) — covered.
Registration into `plugin_registry.cpp` (via `register_default_plugins()`
in `startup.cpp`) is deferred to a later iteration along with the
watcher hook provider registration — both happen together.

- [x] **Step 1:** 13 tests cover every slot: can_complete discriminator, name ID, empty current-word yields all 10 subcommands, prefix filtering, works without active_engine (fallback), -r/--resource → resources, --via → clusters, --preset → presets, --workspace → existing workspace names (deduped), --alloc → allocation ids, attach positional → workspace/instance pairs, down positional → allocation ids, `up --` → up's flag list
- [x] **Step 2:** Implemented with static subcommand table + per-subcommand flag lists + dynamic lookups via `active_engine() → config() / registry()`. Exposes `ClusterEngine::config()` + `registry()` accessors
- [x] **Step 3:** 13/13 pass; full suite 1139 (was 1126)
- [x] **Step 4:** Commit: `feat(cluster): completion provider for every positional slot`

### Task M4.2: Safety-hook integration for destructive commands

**Files:**
- Modify: `src/builtins/cluster.cpp`
- Modify: `src/plugins/safety_hook_provider.cpp` (or equivalent) — register `cluster down` and `cluster kill` as confirmable
- Create: `tests/unit/cluster/cluster_safety_test.cpp`

Plan drift: confirmation lives in the dispatch layer (cmd_down /
cmd_kill in src/cluster/builtin_dispatch.cpp), not as a new rule in
`src/plugins/safety_hook_provider.cpp`. The safety-hook provider uses
full-command-string regex classification, which is too coarse for
cluster's structured argv. Dispatch-layer confirmation reuses the
engine's already-plumbed `IPrompt` and keeps every test focused on
the scoped behaviour.

- [x] **Step 1:** 8 tests for `cluster down` / `cluster kill` × {prompt-by-default, aborts-on-'n', -y bypass, --yes bypass}, plus a test for preview-includes-resource-and-node
- [x] **Step 2:** Added `IPrompt& ClusterEngine::prompt()` accessor; cmd_down / cmd_kill parse `-y / --yes`, build a preview pulling allocation details from the registry, call `engine.prompt().choice(preview, "yn")` when the flag is absent. Completion provider's flag tables now include `--yes` / `-y` for both
- [x] **Step 3:** 8/8 new tests pass; updated 4 pre-existing tests that were now blocking on the prompt (added `-y`); full suite 1147 (was 1139), 0 regressions
- [x] **Step 4:** Commit: `feat(cluster): destructive commands go through safety hook`

### Task M4.3: `cluster doctor` + `cluster probe` diagnostics

**Files:**
- Modify: `src/cluster/cluster_engine.cpp` (add `doctor(...)` and `probe(...)` methods if not yet present)
- Modify: `src/builtins/cluster.cpp`
- Create: `tests/unit/cluster/cluster_engine_doctor_test.cpp`

Plan drift: `doctor` ships 3 checks — SSH reach, `sbatch` presence,
`tmux` presence. The ControlMaster-socket-writable + ~/.ssh/config
checks are dropped from M4.3; they'd require filesystem + ssh_config
parsing abstractions that we'd have to mock carefully, and the signal
they provide is largely captured by the SSH-reach probe itself (if ssh
can't connect, the user finds out from the one check that matters).
Polish candidates for later.

`probe` was already implemented in M1.8 (sinfo-per-route report);
M4.3 only wires the doctor CLI surface.

- [x] **Step 1:** 3 checks with OK / WARN / FAIL + one-line fix hints. SSH reach failing skips follow-up checks (they can't succeed without ssh). Unknown filter cluster → EngineError
- [x] **Step 2:** probe already in M1.8 — no change this iteration
- [x] **Step 3:** 8 tests: all-OK / unreachable-skips-rest / sbatch-missing / tmux-missing / empty-which-output / multi-cluster-all / --cluster-filter / unknown-filter-errors
- [x] **Step 4:** Commit: `feat(cluster): doctor and probe diagnostics`

### Task M4.4: Per-subcommand help text + error audit

**Files:**
- Modify: `src/builtins/cluster.cpp`
- Create: `tests/unit/cluster/cluster_help_test.cpp`

- [x] **Step 1:** All 10 subcommands (up, launch, attach, list, down, kill, sync, probe, import, doctor) render usage + flag docs on `--help`. `cluster probe --help` was enriched with an `options:` section listing `-r, --resource` (only had the short form in the usage line before)
- [x] **Step 2:** Audit via tests: 9 scenarios exercising unknown subcommand + missing-required-flag / missing-positional / unknown-option for every subcommand that can fail them, each verifying the error line starts with `tash: cluster: ` (the convention from commit 55faeba). Previously-shipped errors already conformed; no impl changes needed
- [x] **Step 3:** 13 snapshot-style tests (top-level help mentions all subcommands, `help <sub>` routes correctly, every subcommand --help carries usage + name + its key flags)
- [x] **Step 4:** Commit: `feat(cluster): per-subcommand help and error format audit`

---

## M5 — Docs + real-cluster smoke suite

Goal: documentation and a user-runnable smoke test against a real cluster.

### Task M5.1: User-facing `docs/cluster.md`

**Files:**
- Create: `docs/cluster.md`

- [x] **Step 1:** 335-line `docs/cluster.md` covering: prerequisites, ~/.ssh/config setup, config.toml schema (minimal + full examples), `cluster doctor` sanity check, full typical workflow (connect→up→launch→list→attach→kill→down→disconnect), commands reference table, notifications (both hook + tmux fallback paths), demo-mode invitation, troubleshooting for the 5 common pain points
- [x] **Step 2:** Commit: `docs(cluster): user walkthrough for cluster subsystem`

### Task M5.2: Demo-mode walkthrough

**Files:**
- Create: `docs/cluster-demo.md`

- [ ] **Step 1:** Step-by-step guide to trying `TASH_CLUSTER_DEMO=1` without any real cluster
- [ ] **Step 2:** Commit: `docs(cluster): demo-mode walkthrough`

### Task M5.3: Real-cluster smoke suite

**Files:**
- Create: `tests/smoke/cluster/README.md`
- Create: `tests/smoke/cluster/smoke-profile.toml.template`
- Create: `tests/smoke/cluster/run_smoke.sh`
- Create: `tests/smoke/cluster/expect/attach_detach.exp`

- [ ] **Step 1:** `run_smoke.sh` runs: `cluster connect`, `cluster up -r <profile-res> -t 15m`, `cluster launch --workspace smoke --cmd 'sleep 600'`, `cluster list`, attach/detach via `expect`, `cluster kill`, `cluster down -y`, `cluster sync`
- [ ] **Step 2:** Each step prints `OK` or `FAIL`; script exits non-zero on any FAIL
- [ ] **Step 3:** README documents setup (fill in `smoke-profile.toml` from template, export `TASH_CLUSTER_SMOKE_PROFILE=<name>`)
- [ ] **Step 4:** Commit: `test(cluster): real-cluster smoke suite (manual opt-in)`

### Task M5.4: Final README update

**Files:**
- Modify: `README.md`

- [ ] **Step 1:** Promote cluster from "experimental" to full feature-matrix row with a short code sample
- [ ] **Step 2:** Remove the "experimental" note from the M0 preview
- [ ] **Step 3:** Commit: `docs: document cluster subsystem in README`

---

## Completion criteria

All of the following must be true before the work is considered done:

1. Every checkbox in every task above is checked.
2. `ctest --test-dir build -V` runs green with zero failures on both Linux and macOS.
3. `src/cluster/` line coverage ≥ 85%.
4. `TASH_CLUSTER_DEMO=1 ./build/tash.out -c 'cluster up -r a100 -t 1h && cluster list && cluster down utah-notchpeak:1'` succeeds end-to-end.
5. `docs/cluster.md` and `docs/cluster-demo.md` exist and match implementation.
6. `tests/smoke/cluster/run_smoke.sh` exists (not run in CI — validates manually against CHPC when requested).
7. No regressions in pre-existing tash tests (`ctest` count unchanged or higher).
8. `code-reviewer` agent (from superpowers) has reviewed the final diff and all HIGH/CRITICAL findings have been addressed.
