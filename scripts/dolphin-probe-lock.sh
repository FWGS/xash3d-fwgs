#!/usr/bin/env bash
# Exclusive lock for Dolphin boot probes. Sourced by dolphin-boot-probe.sh.
#
# Host-wide by default: every checkout and worktree on this machine shares
# one lock, so probes started from different trees (or agents) queue
# instead of killing each other's Dolphin.  DOLPHIN_PROBE_LOCK overrides the
# path; DOLPHIN_PROBE_LOCK_WAIT=<seconds> waits for the lock instead of
# failing at once (0, the default, keeps the old fail-fast behaviour).
set -uo pipefail

if ! command -v flock >/dev/null 2>&1; then
	return 0 2>/dev/null || exit 0
fi

ROOT="${ROOT:-$(git rev-parse --show-toplevel)}"
mkdir -p "$ROOT/.ai"
if [[ -n "${DOLPHIN_PROBE_LOCK:-}" ]]; then
	PROBE_LOCK_FILE="$DOLPHIN_PROBE_LOCK"
elif [[ -d /dev/shm && -w /dev/shm ]]; then
	PROBE_LOCK_FILE=/dev/shm/xash3d-gc-dolphin-probe.lock
else
	PROBE_LOCK_FILE="$ROOT/.ai/dolphin-probe.lock"
fi
exec 9>"$PROBE_LOCK_FILE"
if (( ${DOLPHIN_PROBE_LOCK_WAIT:-0} > 0 )); then
	if flock -w "$DOLPHIN_PROBE_LOCK_WAIT" 9; then
		return 0 2>/dev/null || exit 0
	fi
elif flock -n 9; then
	return 0 2>/dev/null || exit 0
fi
echo "HOST_FAILURE: another Dolphin boot probe is already running (lock $PROBE_LOCK_FILE)."
return 2 2>/dev/null || exit 2
