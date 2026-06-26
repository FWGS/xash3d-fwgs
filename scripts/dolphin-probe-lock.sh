#!/usr/bin/env bash
# Exclusive lock for Dolphin boot probes. Sourced by dolphin-boot-probe.sh.
set -uo pipefail

if ! command -v flock >/dev/null 2>&1; then
	return 0 2>/dev/null || exit 0
fi

ROOT="${ROOT:-$(git rev-parse --show-toplevel)}"
mkdir -p "$ROOT/.ai"
exec 9>"$ROOT/.ai/dolphin-probe.lock"
if flock -w 10 9; then
	return 0 2>/dev/null || exit 0
fi

stale=false
if [[ -f "$ROOT/.ai/dolphin-probe.lock" ]]; then
	lock_age=$(( $(date +%s) - $(stat -c %Y "$ROOT/.ai/dolphin-probe.lock" 2>/dev/null || echo 0) ))
	(( lock_age > 300 )) && stale=true
fi
if ! pgrep -f "dolphin" >/dev/null 2>&1; then
	stale=true
fi
if [[ "$stale" != true ]]; then
	echo "HOST_FAILURE: another Dolphin boot probe is already running."
	return 2 2>/dev/null || exit 2
fi

echo "WARNING: Removing stale dolphin-probe lock file."
rm -f "$ROOT/.ai/dolphin-probe.lock"
exec 9>"$ROOT/.ai/dolphin-probe.lock"
if flock -n 9; then
	return 0 2>/dev/null || exit 0
fi
echo "HOST_FAILURE: could not acquire Dolphin probe lock after cleanup."
return 2 2>/dev/null || exit 2
