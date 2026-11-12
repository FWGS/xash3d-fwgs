#!/usr/bin/env bash
# Wake the agent when a Swiss SD appears or returns with swiss-evidence.txt.
# Intentionally skips Extra/Stream/Expansion deep scans (those hang udisks/find).
set -euo pipefail
ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$ROOT"

SKIP='Extra|Stream|Expansion'
prev="$(lsblk -nb -o NAME,SIZE,TRAN,RM,HOTPLUG,TYPE 2>/dev/null | md5sum | awk '{print $1}')"

evidence_on_mount() {
	local m="$1" f
	for f in \
		"$m/xash3d/valve/logs/swiss-evidence.txt" \
		"$m/valve/logs/swiss-evidence.txt" \
		"$m/swiss-evidence.txt"
	do
		[[ -f "$f" ]] && return 0
	done
	return 1
}

while true; do
	sleep 10
	# Desktop drop-file: DEVICE=/dev/sdX + YES=1 → flash without chat.
	if [[ -f "$HOME/Desktop/XASH3D-GC-FLASH-DEVICE.txt" ]]; then
		if scripts/gamecube-swiss-flash-from-dropfile.sh; then
			:
		fi
	fi
	# Evidence from an off-host GC run (copy swiss-evidence.txt to Desktop).
	if [[ -f "$HOME/Desktop/XASH3D-GC-SWISS-EVIDENCE.txt" || -f "$HOME/Desktop/swiss-evidence.txt" ]]; then
		if scripts/gamecube-swiss-ingest-from-dropfile.sh; then
			:
		fi
	fi
	cur="$(lsblk -nb -o NAME,SIZE,TRAN,RM,HOTPLUG,TYPE 2>/dev/null | md5sum | awk '{print $1}')"
	if [[ "$cur" != "$prev" ]]; then
		prev="$cur"
		if scripts/gamecube-swiss-list-flash-targets.sh >/tmp/gc-swiss-flash-cand.txt 2>&1; then
			echo "AGENT_LOOP_WAKE_swiss_sd {\"prompt\":\"USB/removable flash candidate appeared — run gamecube-swiss-hw-status.sh + list-flash-targets; stage/dd smoke img if safe; continue toward swiss-evidence.txt. Do not mark goal complete without real HW evidence.\"}"
		fi
		for m in /run/media/tim/*; do
			[[ -d "$m" ]] || continue
			b="$(basename "$m")"
			[[ "$b" =~ ^($SKIP)$ ]] && continue
			fstype="$(findmnt -no FSTYPE "$m" 2>/dev/null || true)"
			case "$fstype" in
				vfat|msdos|fat|fat32)
					echo "AGENT_LOOP_WAKE_swiss_sd {\"prompt\":\"FAT mount $b appeared — stage smoke if needed, or ingest swiss-evidence.txt if present. Do not mark goal complete without real HW evidence.\"}"
					;;
			esac
		done
	fi
	# Evidence only on non-archive mounts (never walk Expansion/Extra/Stream).
	for m in /run/media/tim/*; do
		[[ -d "$m" ]] || continue
		b="$(basename "$m")"
		[[ "$b" =~ ^($SKIP)$ ]] && continue
		if evidence_on_mount "$m"; then
			echo "AGENT_LOOP_WAKE_swiss_sd {\"prompt\":\"swiss-evidence.txt on $b — run gamecube-swiss-hw-complete.sh / evidence ingest immediately.\"}"
			sleep 60
			break
		fi
	done
done
