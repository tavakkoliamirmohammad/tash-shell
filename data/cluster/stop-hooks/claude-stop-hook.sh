#!/usr/bin/env bash
# Tash cluster — Claude Code `Stop` hook.
#
# Invoked by Claude Code on the cluster compute node when the session stops
# (idle, awaiting user input, or finished). Writes a one-line JSON event so
# the laptop-side watcher — which is holding `ssh <cluster> tail -F ...` —
# picks it up and fires a desktop notification + bell.
#
# Tash sets these env vars when it launches each instance in a tmux window:
#
#   TASH_CLUSTER_WORKSPACE   workspace name (e.g. "repoA")
#   TASH_CLUSTER_INSTANCE    instance id or name (e.g. "1", "feature-x")
#   TASH_CLUSTER_EVENT_DIR   event root (default: $HOME/.tash-cluster/events)
#
# Event file layout:
#
#   <TASH_CLUSTER_EVENT_DIR>/<workspace>/<instance>.event   # JSON lines, append-only
#
# Payload shape:
#
#   {"ts":"<ISO-8601 UTC>","instance":"<ws>/<inst>","kind":"stopped","detail":"awaiting input"}

set -euo pipefail

ws="${TASH_CLUSTER_WORKSPACE:-unknown}"
inst="${TASH_CLUSTER_INSTANCE:-unknown}"
event_dir="${TASH_CLUSTER_EVENT_DIR:-$HOME/.tash-cluster/events}"

mkdir -p -- "$event_dir/$ws"

ts="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
printf '{"ts":"%s","instance":"%s/%s","kind":"stopped","detail":"awaiting input"}\n' \
    "$ts" "$ws" "$inst" >> "$event_dir/$ws/$inst.event"
