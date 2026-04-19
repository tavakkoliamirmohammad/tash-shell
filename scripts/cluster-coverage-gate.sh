#!/usr/bin/env bash
# Cluster subsystem coverage gate.
#
# Runs gcov against the .gcda files produced by the cluster plugins
# under $BUILD_DIR, aggregates line coverage across src/cluster/,
# prints a per-file + total summary, and exits non-zero if the total
# falls below the threshold.
#
# Requirements:
#   - The build must have been configured with -DTASH_COVERAGE=ON.
#   - ctest must have run (so .gcda files exist for each TU).
#   - gcov in $PATH (comes with every GCC install).
#
# Usage:
#   scripts/cluster-coverage-gate.sh [--threshold PCT]
# Environment overrides:
#   BUILD_DIR       (default: build)
#   SOURCE_DIR      (default: current working directory's git root)
#   THRESHOLD_PCT   (default: 85)
#
# Exit codes:
#   0  total coverage >= threshold
#   1  total coverage <  threshold
#   2  no coverage data found (misconfigured build?)

set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
SOURCE_DIR="${SOURCE_DIR:-$(pwd)}"
THRESHOLD_PCT="${THRESHOLD_PCT:-85}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --threshold) THRESHOLD_PCT="$2"; shift 2;;
        --threshold=*) THRESHOLD_PCT="${1#*=}"; shift;;
        *) echo "unknown arg: $1" >&2; exit 2;;
    esac
done

if ! command -v gcov >/dev/null 2>&1; then
    echo "tash: coverage gate: gcov not found in PATH" >&2
    exit 2
fi

# Where the shell_lib compile artifacts live. gcov reads .gcda files
# (runtime counts) alongside .gcno (arc graph). The CMakeFiles layout
# puts them next to the .cpp.o files. Resolve an absolute path because
# we cd into a tmp dir before running gcov (to keep .gcov artifacts
# isolated from the build tree).
if [[ -d "$BUILD_DIR" ]]; then
    BUILD_DIR="$(cd "$BUILD_DIR" && pwd)"
fi
ARTIFACT_DIR="${BUILD_DIR}/CMakeFiles/shell_lib.dir/src/cluster"
if [[ ! -d "$ARTIFACT_DIR" ]]; then
    echo "tash: coverage gate: no cluster artifact dir at $ARTIFACT_DIR" >&2
    echo "       (did you configure with -DTASH_COVERAGE=ON and run ctest?)" >&2
    exit 2
fi

shopt -s nullglob
gcda_files=( "$ARTIFACT_DIR"/*.gcda )
if [[ ${#gcda_files[@]} -eq 0 ]]; then
    echo "tash: coverage gate: no .gcda files under $ARTIFACT_DIR" >&2
    echo "       (run ctest so cluster code executes and emits counts)" >&2
    exit 2
fi

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT
cd "$TMP_DIR"

total_lines=0
total_covered=0

printf '%-40s %10s %10s %8s\n' "file" "lines" "covered" "pct"
printf '%-40s %10s %10s %8s\n' "----" "-----" "-------" "---"

for gcda in "${gcda_files[@]}"; do
    # gcov emits "<file>.gcov" in CWD. The -n flag suppresses that —
    # we only want the summary on stdout.
    out="$(gcov -n "$gcda" 2>/dev/null || true)"

    # gcov output groups per source file with:
    #   File '…/src/cluster/foo.cpp'
    #   Lines executed:XX.XX% of N
    #
    # Walk the output: when we see a File: line naming a src/cluster
    # path, pair it with the next Lines executed line.
    while IFS= read -r line; do
        if [[ $line =~ ^File\ \'(.*)\' ]]; then
            cur_file="${BASH_REMATCH[1]}"
        elif [[ $line =~ ^Lines\ executed:([0-9.]+)%\ of\ ([0-9]+) ]]; then
            pct="${BASH_REMATCH[1]}"
            n="${BASH_REMATCH[2]}"
            # Only count src/cluster/ .cpp files (ignore headers + other).
            if [[ ${cur_file:-} =~ /src/cluster/[^/]+\.cpp$ ]]; then
                # covered = round(pct/100 * n). Use bc for fractional math.
                covered=$(echo "scale=4; ($pct/100)*$n" | bc)
                covered_int=$(printf '%.0f' "$covered")
                total_lines=$((total_lines + n))
                total_covered=$((total_covered + covered_int))
                short=$(basename "$cur_file")
                printf '%-40s %10d %10d %7.2f%%\n' "$short" "$n" "$covered_int" "$pct"
            fi
        fi
    done <<<"$out"
done

if [[ $total_lines -eq 0 ]]; then
    echo "tash: coverage gate: 0 instrumented lines found for src/cluster/" >&2
    exit 2
fi

total_pct=$(echo "scale=2; ($total_covered*100)/$total_lines" | bc)

echo
printf 'total: %d/%d lines covered = %s%%\n' "$total_covered" "$total_lines" "$total_pct"
printf 'threshold: %s%%\n' "$THRESHOLD_PCT"

# bc returns 1 for true, 0 for false.
if [[ $(echo "$total_pct < $THRESHOLD_PCT" | bc) -eq 1 ]]; then
    echo "FAIL: src/cluster/ line coverage $total_pct% below threshold $THRESHOLD_PCT%" >&2
    exit 1
fi
echo "PASS"
exit 0
