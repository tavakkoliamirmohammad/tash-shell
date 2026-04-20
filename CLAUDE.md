# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build, Test, Run

```sh
# Configure + build (tests ON by default)
cmake -B build && cmake --build build -j "$(getconf _NPROCESSORS_ONLN)"

# Run the shell
./build/tash.out

# Full test suite
ctest --test-dir build --output-on-failure -j "$(getconf _NPROCESSORS_ONLN)"

# Single test binary (gtest) — filter to one case
./build/test_tokenizer --gtest_filter='Tokenizer.HandlesQuotedStrings'
./build/test_integration --gtest_filter='integration/Basic.*'

# List everything ctest knows about (useful when hunting a prefixed name)
ctest --test-dir build -N | head
```

Common build toggles (all cache variables — pass with `-D`):

| Flag | Effect |
|---|---|
| `-DBUILD_TESTS=OFF` | Skip the whole test tree (release builds) |
| `-DTASH_SANITIZERS=ON` | ASan + UBSan; pair with `-DCMAKE_BUILD_TYPE=RelWithDebInfo` |
| `-DTASH_COVERAGE=ON` | `--coverage` instrumentation (GCC/Clang only) |
| `-DTASH_STATIC=ON` | `-static` link (really only works on Linux/musl) |
| `-DTASH_ENABLE_FUZZER=ON` | Build `tash_parser_fuzzer` (clang required, set `-DCMAKE_CXX_COMPILER=clang++`) |

Fuzzer:

```sh
cmake -B build-fuzz -DTASH_ENABLE_FUZZER=ON -DCMAKE_CXX_COMPILER=clang++ -DBUILD_TESTS=OFF
cmake --build build-fuzz --target tash_parser_fuzzer
./build-fuzz/tash_parser_fuzzer fuzz/corpus -max_total_time=60
```

## Architecture — what you need to know before editing

### Execution pipeline (the single most important flow)

`src/repl.cpp` drives the loop. Each input line walks through:

1. **Replxx** (highlighting/hints/completion) — `src/ui/*.cpp`
2. **History expansion** (`!!`, `!n`)
3. **AI interception** — `@ai …` or trailing `?` routes to `src/ai/ai_handler.cpp` instead of the normal executor
4. **Multiline continuation** — unclosed quotes, trailing `|`/`&&`, backslash continuation
5. **Parse operators** (`&&`, `||`, `;`) into `CommandSegment`s — `src/core/parser.cpp`
6. For each segment: variable expansion → command substitution → redirections (incl. heredocs) → tokenize → strip quotes → alias expansion → glob → auto-cd check → dispatch
7. **Dispatch** in `src/core/executor.cpp`: builtin table, background, pipeline, or `fork`/`exec` via `src/core/process.cpp`
8. `"did you mean?"` on exit code 127 (Damerau-Levenshtein over `PATH` + builtins)

The IR lives in `include/tash/shell.h` — `CommandSegment`, `Command`, `PipelineSegment`, `Redirection` (with heredoc fields inlined), and the composed `ShellState { CoreState core; AiState ai; ExecutionState exec; }`. Touch this header and the whole tree recompiles — prefer adding to existing substructs over introducing new globals.

### ShellState (composition, not inheritance)

`ShellState` is the single mutable shell context threaded through nearly every call site. Recent refactors (see commit `bfd2a84`) deleted all backward-compat shims — **every access is `state.core.X` / `state.ai.X` / `state.exec.X`**. Do not reintroduce proxy fields, wrapper functions, or umbrella accessors; migrate every call site in the same PR.

Notable fields:
- `exec.in_subshell` — set in a forked subshell child so `execute_command_line` skips history recording (SQLite forbids cross-fork DB sharing; parent records the subshell command as a whole).
- `exec.skip_execution` — the safety hook's "before command" path flips this to skip `execve`.
- `exec.traps` — POSIX trap table; signum `0` is the `EXIT` pseudo-signal.

### Plugin architecture

`include/tash/plugin.h` defines four provider interfaces and a priority-based `PluginRegistry`:

- `ICompletionProvider` — fish (1,056 cmds), fig (715 cmds), manpage `--help` fallback, and builtins
- `IPromptProvider` — starship, default two-line prompt
- `IHistoryProvider` — SQLite-backed + Atuin bridge
- `IHookProvider` — lifecycle (`on_startup`, `on_exit`, `on_config_reload`) **and** per-command (`on_before_command`, `on_after_command`). All five are pure-virtual by design — implementers opt out with `{}`, they don't inherit a silent no-op.

Registry is populated in `src/startup.cpp` via `register_default_plugins()`.

### Adding a plugin (zero-touch registry)

Each plugin registers itself in `cmake/plugin_list.cmake` — **append one `tash_register_plugin(...)` call at the bottom**, nothing else. That function (defined in `cmake/plugins.cmake`) appends to `SHELL_SOURCES`, records a test spec, and `tash_finalize_plugin_tests()` materializes the `test_<NAME>` binary once `shell_lib` exists. This pattern exists to avoid per-PR merge conflicts on a central source list.

`REQUIRES TASH_AI_ENABLED` / `REQUIRES TASH_SQLITE_ENABLED` gates a plugin on optional deps. `TEST_STANDALONE` means the test does not link `shell_lib` — use it when the test compiles its own subset of sources. `TEST_AI_AWARE` adds the AI define + libcurl link when AI is on.

### CMake layout

The root `CMakeLists.txt` is thin (~100 lines). The real work is in `cmake/`:

- `features.cmake` — default build type, `find_package()` for libcurl (required), SQLite3 (optional), nlohmann_json (optional; fetched if missing). Sets `TASH_AI_ENABLED` / `TASH_SQLITE_ENABLED`.
- `fetch_deps.cmake` — `FetchContent` for replxx (always) + nlohmann_json fallback.
- `plugins.cmake` + `plugin_list.cmake` — plugin registry described above.
- `tests.cmake` — builds `shell_lib` (shared with tests; same sources as `tash.out` minus `src/main.cpp`), then per-plugin test executables via `tash_finalize_plugin_tests()`, then a single `test_integration` binary that globs `tests/integration/test_*.cpp` with `CONFIGURE_DEPENDS`.
- `fuzzer.cmake`, `sanitizers.cmake` — optional toggles.

`data/ai_models.json` is embedded into the binary at build time via `cmake/ai_models_embedded.cpp.in`. The env var `TASH_AI_MODELS_JSON` overrides at runtime — useful for tests and ops.

### Tests

~779 tests across 24 unit + 41 integration files. Integration tests use `tests/test_helpers.h` (`run_shell(...)`) which writes input to a tmp file, pipes it into `$TASH_SHELL_BIN` (set by CTest to `$<TARGET_FILE:tash.out>`), and captures stdout+stderr. `count_occurrences()` exists because GNU readline echoes input to stdout on Linux when stdin is a file — tests that check "did the command run" need to distinguish echo from real output.

Parser gets both property tests (`test_parser_properties.cpp`) and a libFuzzer harness (`fuzz/parser_fuzz.cpp`) that exercises only the parse-only surface (`src/core/parser.cpp` + `src/util/io.cpp`).

## Conventions — things that will bite you

- **Parser error format**: all parse errors emit `tash: error: L:C: <msg>` (see commit `55faeba`). Match this exactly when adding new error paths.
- **Diagnostic output**: use `tash::io::debug` for subsystem diagnostics (see commit `d3ea8c4`). Don't write to `stderr` directly in signal/history/bg/plugin/config code.
- **Signal-handler safety**: `fg_child_pid` is `std::atomic<pid_t>` and is `static_assert`'d to be lock-free at header scope — anything you read from a signal handler must honor that contract.
- **C++17, 4-space indent, `-Wall -Wextra`**. Keep functions focused.
- **No `backward-compat` layer**: when you change an API, migrate every caller in the same PR. The codebase explicitly rejects proxy fields / umbrella headers / wrapper functions.
- **macOS vs Linux**: `COLOR_FLAG` differs (`-G` vs `--color=auto`). CI covers both (ubuntu-22.04/24.04, macos-14/15, Fedora, Alpine, manylinux_2_28).
- **manylinux baseline**: Linux release artifacts are built in `quay.io/pypa/manylinux_2_28_x86_64`; libcurl 7.61+ is the floor (AlmaLinux 8 / manylinux_2_28) — do not rely on newer curl APIs.

## CI lanes (`.github/workflows/build.yml`)

`test` (matrix: ubuntu-22.04/24.04, macos-14/15), `build-fedora`, `build-alpine`, `build-manylinux` (dry-run of the release artifact build on every PR), `sanitizer` (ASan+UBSan), `coverage` (lcov with a 10% floor — ratchet up over time, not down), `fuzz` (60-second libFuzzer run on every PR). Before proposing "new" CI, grep these — most commonly-requested lanes are already there.

FetchContent tarballs are pre-fetched with curl retries in each job to dodge intermittent codeload 504s; `TASH_FC_ARGS` points CMake at the pre-fetched dirs.

## Bugfix / feature PR checklist

1. **End-to-end wiring**: a PR's title/description is a contract — every claim should be verifiable in the diff before rebase/merge.
2. **Prefer an established library** over a hand-rolled implementation when doing heavy lifting.
3. **No backward-compat shims** — migrate all call sites.
4. **Tests or a reproduction** for every behavior change. Integration tests go in `tests/integration/test_*.cpp` and are auto-discovered.
