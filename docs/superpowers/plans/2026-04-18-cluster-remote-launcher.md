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

- [ ] **Step 1:** Add one row to the feature matrix table mentioning "Cluster (experimental): SLURM-backed remote launcher, ControlMaster-multiplexed SSH, tmux-persistent instances, desktop notifications"
- [ ] **Step 2:** Commit: `docs: note cluster subsystem in README feature matrix`

---

## M1 — Core engine + Tier 1 tests

Goal: full `Config`, `Registry`, `Presets`, `ClusterEngine` logic implemented against fake seams. All user commands routable via the engine. 85%+ line coverage on `src/cluster/`. Demo mode works end-to-end via the fake backend.

**General rule for M1:** every file in this milestone is paired with a test file. Write the test first, implement to green.

### Task M1.1: Define core value types (POD headers)

**Files:**
- Create: `include/tash/cluster/types.h`

- [ ] **Step 1:** Write `include/tash/cluster/types.h` defining `Cluster`, `Route`, `Resource`, `Preset`, `Defaults`, `Config`, `Allocation`, `Workspace`, `Instance`, `RemoteTarget`, `SubmitSpec`, `SubmitResult`, `JobState`, `PartitionState`, `SshResult`, `SessionInfo`, `UpSpec`, `LaunchSpec`, `AttachSpec` — all POD aggregates with plain fields as per spec Section 8.2
- [ ] **Step 2:** Add include guards, namespace `tash::cluster`
- [ ] **Step 3:** Add `types.cpp` only if operator<< is needed for gtest pretty-printing; otherwise skip
- [ ] **Step 4:** Build tash; no new tests yet (pure header)
- [ ] **Step 5:** Commit: `feat(cluster): declare core value types`

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

- [ ] **Step 1:** Write `config_test.cpp` with tests for: valid-minimal round-trips, valid-full captures all fields, unknown cluster in route → error with field location, missing required field → error with field location, bad TOML → `tash: cluster: <path>:<L>:<C>: <msg>`, environment-variable expansion in `env_file` / `workspace_base`
- [ ] **Step 2:** Run — must fail (no impl)
- [ ] **Step 3:** Implement `Config::load(std::filesystem::path) → std::variant<Config, ConfigError>` using `toml++`; validate post-parse; look up routes' cluster names against declared clusters
- [ ] **Step 4:** Run tests — must pass
- [ ] **Step 5:** Commit: `feat(cluster): TOML config loader with validation`

### Task M1.3: Registry state, locking, reconciliation logic

**Files:**
- Create: `include/tash/cluster/registry.h`
- Create: `src/cluster/registry.cpp`
- Create: `tests/unit/cluster/registry_test.cpp`

- [ ] **Step 1:** Write `registry_test.cpp` covering: empty-registry round-trip, add/remove allocation, add/remove workspace, add/remove instance, reconcile drops ghost jobids given a current `squeue` snapshot, reconcile marks allocation `ended` when squeue no longer lists it, schema-version migration v1→v1 identity pass, corrupt-file recovery backs up to `.bak.<ts>` and starts empty, concurrent-writer file lock serializes two mutations
- [ ] **Step 2:** Run — fails
- [ ] **Step 3:** Implement `Registry` with `load / save / add_allocation / remove_allocation / add_workspace / add_instance / reconcile(cluster, std::vector<JobState>) / lock_scope()`; use `flock` on POSIX for the file lock; JSON via nlohmann/json
- [ ] **Step 4:** Run tests — pass
- [ ] **Step 5:** Commit: `feat(cluster): registry with locking and reconciliation`

### Task M1.4: Preset resolution

**Files:**
- Create: `include/tash/cluster/presets.h`
- Create: `src/cluster/presets.cpp`
- Create: `tests/unit/cluster/presets_test.cpp`
- Create: `data/cluster/stop-hooks/claude-stop-hook.sh` (may be simple placeholder at this milestone)

- [ ] **Step 1:** Write `presets_test.cpp` covering: `builtin:claude` resolves to the packaged script path, unknown `builtin:xyz` → error, explicit absolute path passes through, `$VAR` expansion in command/env_file, env_file sourcing returns key=value map
- [ ] **Step 2:** Write the failing tests and watch them fail
- [ ] **Step 3:** Implement `resolve_preset(const Preset&, const Defaults&) → ResolvedPreset` and a minimal `claude-stop-hook.sh` that writes an event marker (payload format per spec Section 7.3)
- [ ] **Step 4:** Run tests — pass
- [ ] **Step 5:** Commit: `feat(cluster): preset resolution and packaged claude stop hook`

### Task M1.5: Seam interfaces (headers only) + fake implementations

**Files:**
- Create: `include/tash/cluster/ssh_client.h`
- Create: `include/tash/cluster/slurm_ops.h`
- Create: `include/tash/cluster/tmux_ops.h`
- Create: `include/tash/cluster/notifier.h`
- Create: `tests/unit/cluster/fakes/fake_ssh_client.h`
- Create: `tests/unit/cluster/fakes/fake_ssh_client.cpp`
- Create: `tests/unit/cluster/fakes/fake_slurm_ops.h`
- Create: `tests/unit/cluster/fakes/fake_slurm_ops.cpp`
- Create: `tests/unit/cluster/fakes/fake_tmux_ops.h`
- Create: `tests/unit/cluster/fakes/fake_tmux_ops.cpp`
- Create: `tests/unit/cluster/fakes/fake_notifier.h`
- Create: `tests/unit/cluster/fakes/fake_notifier.cpp`
- Create: `tests/unit/cluster/fakes/fakes_test.cpp`

- [ ] **Step 1:** Create all four seam headers with the signatures in spec Section 8.2
- [ ] **Step 2:** Create all four fake impls — each has `expect(...)` to script responses, records invocations, and `verify_and_reset()` to assert all expectations consumed
- [ ] **Step 3:** Write `fakes_test.cpp` with tests that prove each fake respects its scripted responses and records invocations correctly
- [ ] **Step 4:** Run tests — pass
- [ ] **Step 5:** Commit: `test(cluster): seams and fake implementations for unit tests`

### Task M1.6: ClusterEngine — `up`

**Files:**
- Create: `include/tash/cluster/cluster_engine.h`
- Create: `src/cluster/cluster_engine.cpp`
- Create: `tests/unit/cluster/cluster_engine_up_test.cpp`

- [ ] **Step 1:** Write `cluster_engine_up_test.cpp`: all-routes-idle → first declared wins; first-route-busy + second-idle → second picked; all-busy → first declared (falls through to queue); sbatch rejected → no registry write; squeue polling transitions PD→R→success; polling exceeds wait-timeout → user-visible detach/cancel prompt (test via callback injection); `--via` forces route; invalid resource name → error
- [ ] **Step 2:** Create `ClusterEngine` skeleton with constructor accepting `Config&, Registry&, ISshClient&, ISlurmOps&, ITmuxOps&, INotifier&, IPrompt&` (new seam `IPrompt` for the wait-timeout interactive choice — define in `types.h`)
- [ ] **Step 3:** Implement `ClusterResult<Allocation> up(UpSpec)` against fakes
- [ ] **Step 4:** All `cluster_engine_up_test.cpp` tests pass
- [ ] **Step 5:** Commit: `feat(cluster): ClusterEngine::up with route selection and polling`

### Task M1.7: ClusterEngine — `launch` + `attach`

**Files:**
- Modify: `src/cluster/cluster_engine.cpp`
- Create: `tests/unit/cluster/cluster_engine_launch_test.cpp`
- Create: `tests/unit/cluster/cluster_engine_attach_test.cpp`

- [ ] **Step 1:** Write `cluster_engine_launch_test.cpp`: new workspace → tmux new-session + scp stop-hook + new-window; existing workspace → reuses session, new-window only; preset resolution happens before launch; `--cmd` bypasses preset; duplicate session on cluster → adopted silently; window command exits within 2s → `state=exited` + notifier fires; ambiguous allocation → error; `--alloc` override works
- [ ] **Step 2:** Write `cluster_engine_attach_test.cpp`: resolves `workspace/instance` to RemoteTarget + session + window; ambiguous instance → error; missing → error; exec_attach called (recorded by fake)
- [ ] **Step 3:** Implement `launch(LaunchSpec)` and `attach(AttachSpec)` methods
- [ ] **Step 4:** All tests pass
- [ ] **Step 5:** Commit: `feat(cluster): ClusterEngine::launch and ::attach`

### Task M1.8: ClusterEngine — `list` + `down` + `kill` + `sync` + `probe` + `import`

**Files:**
- Modify: `src/cluster/cluster_engine.cpp`
- Create: `tests/unit/cluster/cluster_engine_list_test.cpp`
- Create: `tests/unit/cluster/cluster_engine_down_test.cpp`
- Create: `tests/unit/cluster/cluster_engine_kill_test.cpp`
- Create: `tests/unit/cluster/cluster_engine_sync_test.cpp`
- Create: `tests/unit/cluster/cluster_engine_probe_test.cpp`
- Create: `tests/unit/cluster/cluster_engine_import_test.cpp`

- [ ] **Step 1:** Write one test file per command covering happy paths and the error rows from spec Section 10's matrix relevant to that command (e.g. `down` tests the "external scancel" row; `sync` tests the "reconciler detects gone jobid" row)
- [ ] **Step 2:** Implement each method; keep files focused — if `cluster_engine.cpp` grows past ~500 lines, extract per-command private methods to `cluster_engine_<cmd>.cpp`
- [ ] **Step 3:** All tests pass
- [ ] **Step 4:** Commit: `feat(cluster): ClusterEngine list/down/kill/sync/probe/import`

### Task M1.9: Watcher event decode (pure-logic portion)

**Files:**
- Create: `include/tash/cluster/watcher.h`
- Create: `src/cluster/watcher.cpp`
- Create: `tests/unit/cluster/watcher_test.cpp`

- [ ] **Step 1:** Write `watcher_test.cpp` covering: event line parse (`<workspace>/<instance> <event> <ts> <payload>`), dedup via (instance, ts), state transition rules per event type (claude_stopped → idle; window_exited → exited; etc.), backoff sequence calculation (1s → 2s → 4s → ... → 30s capped → abandon after 3min), stop-token cancels the decode loop promptly
- [ ] **Step 2:** Implement the pure logic: `EventDecoder::decode(std::string_view line) → std::optional<Event>` and `WatcherLoop::step(...)` that takes a line source, emits events, updates registry, calls notifier
- [ ] **Step 3:** Tests pass (no real ssh — line source is a fake in-memory channel)
- [ ] **Step 4:** Commit: `feat(cluster): watcher event decoder and dedup logic`

### Task M1.10: Builtin `cluster` dispatches to ClusterEngine (Tier-1 integration via fakes)

**Files:**
- Modify: `src/builtins/cluster.cpp`
- Create: `tests/unit/cluster/cluster_builtin_test.cpp`

- [ ] **Step 1:** Write `cluster_builtin_test.cpp` that constructs a ClusterEngine with fakes, wraps it in the builtin's argv-parsing logic, and asserts: `cluster up -r a100 -t 12h` parses correctly; `cluster list` formats output exactly matching the expected string; `cluster attach repoA/1` resolves correctly; every `cluster <cmd> --help` renders; unknown subcommand → error with usage; missing required flag → error with usage
- [ ] **Step 2:** Rewrite the stub `builtin_cluster` to parse argv and dispatch to a ClusterEngine reference injected via `ShellState` (add a `std::shared_ptr<ClusterEngine> cluster_engine` slot to ShellState under a TASH_CLUSTER guard). At builtin-call time, if the slot is empty, construct with real seams on demand (constructor will happen in M2 when the real seams exist — for now, return "not implemented" if slot is empty and the user is not in demo mode)
- [ ] **Step 3:** Tests pass
- [ ] **Step 4:** Commit: `feat(cluster): cluster builtin dispatches to ClusterEngine`

### Task M1.11: Demo mode wiring

**Files:**
- Create: `tests/fakes/scenarios/demo.json`
- Create: `include/tash/cluster/demo_mode.h`
- Create: `src/cluster/demo_mode.cpp`
- Create: `tests/unit/cluster/demo_mode_test.cpp`
- Modify: `src/startup.cpp` (detect `TASH_CLUSTER_DEMO=1`, construct demo ClusterEngine with in-memory scenario-driven fakes, install into ShellState)

- [ ] **Step 1:** Write the demo scenario file per spec Section 7
- [ ] **Step 2:** Write tests that exercise `demo up → demo list → demo launch → demo list → demo attach (exec recorded) → demo down` via the demo engine
- [ ] **Step 3:** Implement `build_demo_engine(Registry&)` returning an owned ClusterEngine wired to in-memory fakes that read the scenario
- [ ] **Step 4:** Modify startup to detect `TASH_CLUSTER_DEMO=1` and inject the demo engine
- [ ] **Step 5:** Run `TASH_CLUSTER_DEMO=1 ./build/tash.out -c 'cluster up -r a100 -t 1h && cluster list && cluster down utah-notchpeak:1'` — must succeed end-to-end against the fake
- [ ] **Step 6:** Commit: `feat(cluster): TASH_CLUSTER_DEMO=1 runs end-to-end offline`

### Task M1.12: Coverage gate

**Files:**
- Modify: `cmake/coverage.cmake` (or equivalent) to include a gate rule for `src/cluster/`

- [ ] **Step 1:** Read tash's existing coverage gate mechanism
- [ ] **Step 2:** Add a check that `src/cluster/` line coverage ≥85% after `ctest`; fail CI if below
- [ ] **Step 3:** Run `ctest` + coverage to verify threshold is met (add tests as needed to bring it up)
- [ ] **Step 4:** Commit: `build(cluster): enforce 85% line coverage on src/cluster/`

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

- [ ] **Step 1:** Capture or craft recordings (anonymized) — can be synthesized from public SLURM doc examples if no CHPC capture is available at plan-time. Each fixture is a verbatim stdout capture + a sidecar `.expected.json` describing the parsed result
- [ ] **Step 2:** Write `slurm_ops_parser_test.cpp` that loops over each fixture, parses it, asserts the JSON shape
- [ ] **Step 3:** Implement real `SlurmOps` with `sbatch/squeue/sinfo/scancel` command builders + stdout parsers; takes an `ISshClient&` and constructs the argv, the actual fork+exec happens inside `ISshClient::run`
- [ ] **Step 4:** All parser tests pass
- [ ] **Step 5:** Commit: `feat(cluster): real SlurmOps with parsers + golden recordings`

### Task M2.2: Real SshClient with ControlMaster lifecycle

**Files:**
- Create: `src/cluster/ssh_client.cpp`
- Create: `tests/integration/cluster/ssh_master_reuse_test.cpp`
- Create: `tests/integration/cluster/control_master_recovery_test.cpp`
- Create: `tests/integration/cluster/ssh_client_fixture.h`

- [ ] **Step 1:** Write integration test that spawns a loopback sshd (or asyncssh server) bound to `127.0.0.1:<random-port>` with a test ed25519 key; test runs 10 `ssh_client.run()` invocations and asserts only one master spawned
- [ ] **Step 2:** Write the recovery test: kill the master process mid-run; next op reopens; socket file removed and recreated
- [ ] **Step 3:** Implement `SshClient` that builds argv `ssh -o ControlMaster=auto -o ControlPath=<path> -o ControlPersist=yes ...`, forks/execs, captures stdout/stderr, enforces timeout
- [ ] **Step 4:** All integration tests pass
- [ ] **Step 5:** Commit: `feat(cluster): real SshClient with ControlMaster multiplexing`

### Task M2.3: Real TmuxOps

**Files:**
- Create: `src/cluster/tmux_ops.cpp`
- Create: `tests/unit/cluster/tmux_ops_argv_test.cpp`
- Create: `tests/integration/cluster/tmux_ops_integration_test.cpp`
- Create: `tests/fixtures/recordings/tmux/list-windows-basic.txt`
- Create: `tests/fixtures/recordings/tmux/list-sessions-many.txt`

- [ ] **Step 1:** Argv tests exhaustively cover shell-escaping for session/window/command with spaces, quotes, backticks, `$`, `;`, unicode, newlines
- [ ] **Step 2:** Integration tests use the tmux stub binary to assert that `new_session / new_window / list_sessions / kill_window` send the right argv
- [ ] **Step 3:** Implement real `TmuxOps` via `ssh <host> tmux <subcmd>` composition; parse `list-windows`/`list-sessions` output using the recordings
- [ ] **Step 4:** All tests pass
- [ ] **Step 5:** Commit: `feat(cluster): real TmuxOps with argv quoting and parsers`

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

- [ ] **Step 1:** Write each stub binary as a bash script ≤50 lines that reads `$TASH_FAKE_SCENARIO`, matches by argv, emits canned stdout + exit code
- [ ] **Step 2:** Make them executable
- [ ] **Step 3:** Write `integration_fixture.h` that sets `PATH=<build>/tests/fakes/bin:$PATH` and `TASH_FAKE_SCENARIO` per test
- [ ] **Step 4:** One smoke test that calls `ssh` via the fixture and asserts scenario-driven output
- [ ] **Step 5:** Commit: `test(cluster): Tier-2 stub binaries and scenario runtime`

### Task M2.5: Tier-2 end-to-end integration tests

**Files:**
- Create: `tests/integration/cluster/up_down_roundtrip_test.cpp`
- Create: `tests/integration/cluster/launch_attach_detach_test.cpp`
- Create: `tests/integration/cluster/multi_workspace_test.cpp`
- Create: `tests/integration/cluster/multi_allocation_test.cpp`
- Create: `tests/integration/cluster/registry_reconcile_test.cpp`
- Create: `tests/integration/cluster/cli_help_and_errors_test.cpp`
- Create: `tests/integration/cluster/demo_mode_smoke_test.cpp`

- [ ] **Step 1:** Each test file corresponds to a scenario file under `tests/fakes/scenarios/` with a timeline
- [ ] **Step 2:** Tests launch tash via `fork/exec` with the stubbed PATH, run commands, assert stdout + registry file contents
- [ ] **Step 3:** All tests pass
- [ ] **Step 4:** Commit: `test(cluster): Tier-2 end-to-end integration suite`

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

- [ ] **Step 1:** Tests use the `osascript` / `notify-send` stub binaries to assert the right argv + body were sent
- [ ] **Step 2:** Implement `NotifierFactory::create()` returning the right impl at runtime; conditional compilation for platform
- [ ] **Step 3:** Tests pass on both linux and macOS CI
- [ ] **Step 4:** Commit: `feat(cluster): platform-specific desktop notifiers`

### Task M3.2: ClusterWatcherHookProvider lifecycle

**Files:**
- Create: `include/tash/plugins/cluster_watcher_hook_provider.h`
- Create: `src/plugins/cluster_watcher_hook_provider.cpp`
- Create: `tests/unit/cluster/watcher_hook_provider_test.cpp`
- Modify: `cmake/plugin_list.cmake` (add `cluster_watcher_hook_provider`)
- Modify: `src/plugins/plugin_registry.cpp` (register it when TASH_CLUSTER is defined)

- [ ] **Step 1:** Tests cover: `on_startup` reconciles registry and spawns a watcher thread per running allocation; `on_exit` cancels stop-tokens and joins (with a 2s backstop assertion); `on_after_command` updates last-prompt time
- [ ] **Step 2:** Implement provider owning a `std::vector<std::thread>` and per-thread `StopToken`
- [ ] **Step 3:** Tests pass
- [ ] **Step 4:** Commit: `feat(cluster): watcher hook provider lifecycle wiring`

### Task M3.3: End-to-end notification test via scenario timeline

**Files:**
- Create: `tests/integration/cluster/notification_end_to_end_test.cpp`
- Create: `tests/fakes/scenarios/claude_stopped.json`

- [ ] **Step 1:** Scenario timeline: sbatch → job starts → claude_stopped event after N seconds → notifier stub records call
- [ ] **Step 2:** Test asserts the desktop notification and bell were both invoked with the expected payload
- [ ] **Step 3:** Commit: `test(cluster): end-to-end notification delivery`

### Task M3.4: Tmux silence + window-death fallback detection

**Files:**
- Modify: `src/cluster/watcher.cpp`
- Create: `tests/unit/cluster/watcher_fallback_test.cpp`

- [ ] **Step 1:** Tests cover: tmux window disappears → `window_exited` event; window silent for `notify_silence_sec` → `silence_threshold` event; silence event dedup (don't re-fire for the same silence streak)
- [ ] **Step 2:** Add polling loop that runs `tmux list-windows -F ...` through SshClient at a tunable interval, diffs against last-seen state, emits fallback events
- [ ] **Step 3:** Tests pass
- [ ] **Step 4:** Commit: `feat(cluster): watcher fallbacks via tmux silence + window death`

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

- [ ] **Step 1:** Tests cover every positional slot: subcommand (from static list), `--via <cluster>` (from config), `--profile` (from config), `<resource>` (from config), `<workspace>/<instance>` (from registry); typo → did-you-mean suggestions; empty current-word returns full candidate list
- [ ] **Step 2:** Implement provider reading config + registry
- [ ] **Step 3:** Tests pass
- [ ] **Step 4:** Commit: `feat(cluster): completion provider for every positional slot`

### Task M4.2: Safety-hook integration for destructive commands

**Files:**
- Modify: `src/builtins/cluster.cpp`
- Modify: `src/plugins/safety_hook_provider.cpp` (or equivalent) — register `cluster down` and `cluster kill` as confirmable
- Create: `tests/unit/cluster/cluster_safety_test.cpp`

- [ ] **Step 1:** Tests cover: `cluster down` without `-y` prompts; prompt shows a preview of what will be canceled; `-y` bypasses; `cluster kill` likewise
- [ ] **Step 2:** Implement
- [ ] **Step 3:** Tests pass
- [ ] **Step 4:** Commit: `feat(cluster): destructive commands go through safety hook`

### Task M4.3: `cluster doctor` + `cluster probe` diagnostics

**Files:**
- Modify: `src/cluster/cluster_engine.cpp` (add `doctor(...)` and `probe(...)` methods if not yet present)
- Modify: `src/builtins/cluster.cpp`
- Create: `tests/unit/cluster/cluster_engine_doctor_test.cpp`

- [ ] **Step 1:** `doctor` checks: SSH reachability, `sbatch` presence on cluster, `tmux` presence, ControlMaster socket writable, known-clusters in `~/.ssh/config`. Each check reports OK/WARN/FAIL with a fix hint
- [ ] **Step 2:** `probe` runs `sinfo` on each route for a resource, reports idle nodes + partition state
- [ ] **Step 3:** Tests cover each check passing and each failing path
- [ ] **Step 4:** Commit: `feat(cluster): doctor and probe diagnostics`

### Task M4.4: Per-subcommand help text + error audit

**Files:**
- Modify: `src/builtins/cluster.cpp`
- Create: `tests/unit/cluster/cluster_help_test.cpp`

- [ ] **Step 1:** Every `cluster <cmd> --help` renders a help message with usage, flags, examples
- [ ] **Step 2:** Audit every user-visible error string for `tash: cluster: <msg>` format
- [ ] **Step 3:** Tests snapshot-test the help output (golden strings)
- [ ] **Step 4:** Commit: `feat(cluster): per-subcommand help and error format audit`

---

## M5 — Docs + real-cluster smoke suite

Goal: documentation and a user-runnable smoke test against a real cluster.

### Task M5.1: User-facing `docs/cluster.md`

**Files:**
- Create: `docs/cluster.md`

- [ ] **Step 1:** Write walkthrough: config setup → first `cluster up` → launch → attach → down. Include example `config.toml` snippets per spec Section 5
- [ ] **Step 2:** Commit: `docs(cluster): user walkthrough for cluster subsystem`

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
