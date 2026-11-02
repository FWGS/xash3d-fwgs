#!/usr/bin/env bash
# Exclusive lock for Dolphin boot probes. Sourced by dolphin-boot-probe.sh.
set -uo pipefail

if ! command -v flock >/dev/null 2>&1; then
	return 0 2>/dev/null || exit 0
fi

ROOT="${ROOT:-$(git rev-parse --show-toplevel)}"
mkdir -p "$ROOT/.ai"
exec 9>"$ROOT/.ai/dolphin-probe.lock"
if flock -n 9; then
	return 0 2>/dev/null || exit 0
fi
echo "HOST_FAILURE: another Dolphin boot probe is already running."
return 2 2>/dev/null || exit 2
