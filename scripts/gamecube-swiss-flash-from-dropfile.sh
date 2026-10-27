#!/usr/bin/env bash
# Watch Desktop drop-file for an explicit Swiss SD flash target.
# File: ~/Desktop/XASH3D-GC-FLASH-DEVICE.txt
#   DEVICE=/dev/sdX
#   YES=1
# Destructive dd of OUT/xash3d-gc-swiss-smoke.img, then rename to .done-<stamp>.
set -euo pipefail
ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$ROOT"

DROP="${XASH_FLASH_DROP:-$HOME/Desktop/XASH3D-GC-FLASH-DEVICE.txt}"
IMG="${SWISS_SMOKE_IMG:-OUT/xash3d-gc-swiss-smoke.img}"
LOG=".ai/logs/swiss-flash-drop.log"
mkdir -p "$(dirname "$LOG")"

log() { echo "$(date -u +%Y-%m-%dT%H:%M:%SZ) $*" | tee -a "$LOG"; }

if [[ ! -f "$IMG" ]]; then
	log "FAIL: missing $IMG"
	exit 1
fi

if [[ ! -f "$DROP" ]]; then
	exit 0
fi

device=""
yes=0
while IFS= read -r line || [[ -n "$line" ]]; do
	line="${line%%#*}"
	line="$(echo "$line" | tr -d ' \t\r')"
	[[ -z "$line" ]] && continue
	case "$line" in
		DEVICE=*) device="${line#DEVICE=}" ;;
		YES=1|YES=yes|YES=YES) yes=1 ;;
		/dev/*) device="$line" ;;
	esac
done <"$DROP"

if [[ -z "$device" ]]; then
	log "WARN: $DROP has no DEVICE=/dev/sdX — leave file for operator"
	exit 0
fi
if (( ! yes )); then
	log "WARN: $DROP missing YES=1 — refusing destructive flash"
	exit 0
fi
if [[ ! -b "$device" ]]; then
	log "FAIL: not a block device: $device"
	exit 1
fi

log "==> Drop-file flash $device from $DROP"
if scripts/gamecube-swiss-sd-wait.sh --dd --device "$device" --yes; then
	stamp=$(date +%Y%m%d-%H%M%S)
	mv -f "$DROP" "${DROP}.done-${stamp}"
	log "OK flashed $device — moved drop file to ${DROP}.done-${stamp}"
	command -v notify-send >/dev/null 2>&1 && \
		notify-send -u critical "Xash3D GC" "Flashed $device — insert into GameCube, boot apps/xash3d-gc/boot.dol" || true
	echo "AGENT_LOOP_WAKE_swiss_sd {\"prompt\":\"Drop-file flashed $device — wait for remount after GC boot and ingest swiss-evidence.txt. Do not mark goal complete without real HW evidence.\"}"
	exit 0
fi
log "FAIL: dd/flash of $device failed"
exit 1
