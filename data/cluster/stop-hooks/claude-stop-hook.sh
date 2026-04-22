#!/usr/bin/env bash
# Tash cluster — Claude Code `Stop` hook. Invoked on the compute node
# when Claude pauses; appends a one-line JSON event to a per-instance
# file that the laptop-side ssh-tail watcher picks up. See docs/cluster.md
# for the full pipeline + event schema.

set -euo pipefail

ws="${TASH_CLUSTER_WORKSPACE:-unknown}"
inst="${TASH_CLUSTER_INSTANCE:-unknown}"

# TASH_CLUSTER_EVENT_DIR often arrives as the literal string
# "$HOME/.tash-cluster/events" — tash sets env vars with their values
# single-quoted through the srun wrap, so shell-expansion-time chars
# like `$HOME` aren't resolved at assignment. `eval echo` re-evaluates
# the value through one pass of parameter + tilde expansion, giving
# us an absolute on-disk path.
raw_event_dir="${TASH_CLUSTER_EVENT_DIR:-$HOME/.tash-cluster/events}"
event_dir=$(eval echo "$raw_event_dir")

mkdir -p -- "$event_dir/$ws"

ts="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
printf '{"ts":"%s","instance":"%s/%s","kind":"stopped","detail":"awaiting input"}\n' \
    "$ts" "$ws" "$inst" >> "$event_dir/$ws/$inst.event"
