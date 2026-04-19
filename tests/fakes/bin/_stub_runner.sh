#!/usr/bin/env bash
# Shared runner for tash cluster Tier-2 stub binaries.
#
# Each stub in this dir is a wrapper that invokes:
#   _stub_runner.sh <stub-name> "$@"
#
# Behaviour:
#   1. Source $TASH_FAKE_SCENARIO (if set and readable) so the test can
#      preload <UPPER>_STDOUT / <UPPER>_STDERR / <UPPER>_EXIT env vars.
#   2. Append an invocation line to $TASH_FAKE_LOG (if set): "[stub] argv=...".
#   3. Write the scripted stdout and stderr.
#   4. Exit with the scripted code (default 0).
#
# For `ssh`, extra `ssh_stdout_for_argv_contains_<TOKEN>` env vars let
# tests branch on the remote command — for example:
#     ssh_stdout_for_argv_contains_squeue="..."
# When the remote argv contains "squeue", that value overrides SSH_STDOUT.

set -euo pipefail

name="$1"; shift
upper="$(printf '%s' "$name" | tr '[:lower:]' '[:upper:]')"

log="${TASH_FAKE_LOG:-/dev/null}"
{
    printf '[%s] ' "$name"
    for a in "$@"; do printf '%s|' "$a"; done
    printf '\n'
} >> "$log"

if [[ -n "${TASH_FAKE_SCENARIO:-}" && -r "${TASH_FAKE_SCENARIO}" ]]; then
    # shellcheck disable=SC1090
    source "${TASH_FAKE_SCENARIO}"
fi

# ssh-specific per-subcommand branching. If any argv token matches a
# known routing key, swap the stdout/exit vars. The test can set e.g.
#   ssh_stdout_squeue="..." ssh_exit_squeue="0"
# to override just the ssh-invokes-squeue case.
if [[ "$name" == "ssh" ]]; then
    for arg in "$@"; do
        # Only inspect the first token that matches a known SLURM command.
        case "$arg" in
            *sbatch*)  swap_stdout="${ssh_stdout_sbatch:-}"; swap_exit="${ssh_exit_sbatch:-}";;
            *squeue*)  swap_stdout="${ssh_stdout_squeue:-}"; swap_exit="${ssh_exit_squeue:-}";;
            *sinfo*)   swap_stdout="${ssh_stdout_sinfo:-}";  swap_exit="${ssh_exit_sinfo:-}";;
            *scancel*) swap_stdout="${ssh_stdout_scancel:-}";swap_exit="${ssh_exit_scancel:-}";;
            *tmux*)    swap_stdout="${ssh_stdout_tmux:-}";   swap_exit="${ssh_exit_tmux:-}";;
            *)         continue;;
        esac
        [[ -n "${swap_stdout:-}" ]] && SSH_STDOUT="$swap_stdout"
        [[ -n "${swap_exit:-}" ]]   && SSH_EXIT="$swap_exit"
        break
    done
fi

stdout_var="${upper}_STDOUT"
stderr_var="${upper}_STDERR"
exit_var="${upper}_EXIT"

printf '%s' "${!stdout_var:-}"
printf '%s' "${!stderr_var:-}" >&2
exit "${!exit_var:-0}"
