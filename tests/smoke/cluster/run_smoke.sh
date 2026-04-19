#!/usr/bin/env bash
# Tash Cluster real-cluster smoke suite.
#
# See tests/smoke/cluster/README.md for context.
# Prereqs: a filled-in tests/smoke/cluster/smoke-profile.toml and a
# working ssh alias that matches the profile's [[clusters]] ssh_host.
#
# Exit codes:
#   0  all steps passed
#   1  one or more steps failed
#   2  setup / profile missing / tash binary missing

set -u

here="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(cd "$here/../../.." && pwd)"
tash_bin="${TASH_SMOKE_BIN:-$repo_root/build/tash.out}"
profile="$here/smoke-profile.toml"

# ── Setup ───────────────────────────────────────────────────────

if [[ ! -x "$tash_bin" ]]; then
    echo "tash binary not found: $tash_bin" >&2
    echo "  build first with: cmake -B build -DBUILD_TESTS=ON && cmake --build build" >&2
    exit 2
fi
if [[ ! -f "$profile" ]]; then
    echo "smoke profile not found: $profile" >&2
    echo "  copy smoke-profile.toml.template, fill in FIXMEs, re-run" >&2
    exit 2
fi

# Isolate from the user's real config.
smoke_home="$(mktemp -d)"
trap 'rm -rf "$smoke_home"' EXIT
mkdir -p "$smoke_home/cluster"
cp "$profile" "$smoke_home/cluster/config.toml"
export TASH_CLUSTER_HOME="$smoke_home/cluster"

# ── Helpers ─────────────────────────────────────────────────────

pass=0; fail=0; skip=0; first_fail=""

# Run one tash command via a temp script file (tash has no -c flag).
# On success: return 0, echo stdout to stderr for diagnostics.
# On failure: return non-zero + echo combined stdout+stderr.
run_tash() {
    local script
    script="$(mktemp)"
    printf '%s\n' "$*" > "$script"
    "$tash_bin" "$script" 2>&1
    local rc=$?
    rm -f "$script"
    return $rc
}

# step "<label>" <expected-exit> <cmd>        — runs, labels pass/fail
step() {
    local label="$1"; shift
    local want_exit="$1"; shift
    local t0 t1 output rc
    t0=$(date +%s%N)
    output="$(run_tash "$*")"
    rc=$?
    t1=$(date +%s%N)
    local dt=$(( (t1 - t0) / 1000000 ))    # ms
    if [[ "$rc" == "$want_exit" ]]; then
        printf '[ok  ] %-32s (%dms)\n' "$label" "$dt"
        pass=$(( pass + 1 ))
        return 0
    else
        printf '[FAIL] %-32s (%dms, rc=%d, wanted %d)\n' "$label" "$dt" "$rc" "$want_exit"
        [[ -n "$output" ]] && sed 's/^/       /' <<<"$output"
        fail=$(( fail + 1 ))
        [[ -z "$first_fail" ]] && first_fail="$label"
        return 1
    fi
}

# Like `step`, but also greps for a substring in the tash output.
step_grep() {
    local label="$1"; shift
    local want_exit="$1"; shift
    local needle="$1"; shift
    local output rc
    output="$(run_tash "$*" 2>&1)"
    rc=$?
    if [[ "$rc" == "$want_exit" && "$output" == *"$needle"* ]]; then
        printf '[ok  ] %-32s\n' "$label"
        pass=$(( pass + 1 ))
        return 0
    else
        printf '[FAIL] %-32s  (rc=%d, wanted %d, needle="%s")\n' \
            "$label" "$rc" "$want_exit" "$needle"
        [[ -n "$output" ]] && sed 's/^/       /' <<<"$output"
        fail=$(( fail + 1 ))
        [[ -z "$first_fail" ]] && first_fail="$label"
        return 1
    fi
}

skip_step() {
    printf '[skip] %-32s  (%s)\n' "$1" "$2"
    skip=$(( skip + 1 ))
}

# ── The actual smoke ────────────────────────────────────────────

t_start=$(date +%s)
echo "smoke profile:  $profile"
echo "tash binary:    $tash_bin"
echo "isolated home:  $TASH_CLUSTER_HOME"
echo

step       "cluster connect"                0  "cluster connect smoke"           || exit 1
step       "cluster doctor"                 0  "cluster doctor smoke"            || exit 1
step       "cluster up -r smoke-res"        0  "cluster up -r smoke-res -t 00:15:00" || exit 1

# Grab the alloc id from `cluster list` for later cleanup.
alloc_id="$(run_tash 'cluster list' | awk '/^smoke:[0-9]+/ {print $1; exit}')"
if [[ -z "$alloc_id" ]]; then
    printf '[FAIL] cluster list did not surface an allocation id\n'
    exit 1
fi

step_grep  "cluster list shows alloc"       0  "$alloc_id"   "cluster list"       || exit 1
step       "cluster launch smoke"           0  "cluster launch --workspace smoke --preset smoke" || {
    echo "aborting — leaving allocation $alloc_id in place for diagnosis"
    exit 1
}
step_grep  "cluster list shows inst"        0  "smoke"       "cluster list"       || true

# Attach via expect — skipped if expect isn't available.
if command -v expect >/dev/null 2>&1; then
    if "$here/expect/attach_detach.exp" "$tash_bin" "$TASH_CLUSTER_HOME" "smoke/1"
    then
        printf '[ok  ] %-32s\n' "cluster attach via expect"
        pass=$(( pass + 1 ))
    else
        printf '[FAIL] %-32s\n' "cluster attach via expect"
        fail=$(( fail + 1 ))
        [[ -z "$first_fail" ]] && first_fail="cluster attach"
    fi
else
    skip_step "cluster attach via expect" "expect not installed"
fi

step       "cluster sync"                   0  "cluster sync smoke"               || true
step       "cluster kill smoke/1"           0  "cluster kill smoke/1 -y"          || true
step       "cluster down -y"                0  "cluster down $alloc_id -y"        || true
step_grep  "cluster list is empty"          0  "no allocations" "cluster list"    || true
step       "cluster disconnect"             0  "cluster disconnect smoke"         || true

# ── Summary ─────────────────────────────────────────────────────

t_end=$(date +%s)
elapsed=$(( t_end - t_start ))
echo
if (( fail == 0 )); then
    printf 'smoke passed — %d ok, %d fail, %d skip  (%ds total)\n' \
        "$pass" "$fail" "$skip" "$elapsed"
    exit 0
else
    printf 'smoke FAILED — %d ok, %d fail, %d skip  (%ds total; first fail: %s)\n' \
        "$pass" "$fail" "$skip" "$elapsed" "$first_fail"
    exit 1
fi
