#!/usr/bin/env bash
# Quiet poller: emit AGENT_LOOP_WAKE only when Swiss media/dropfile appears.
set -euo pipefail
ROOT="$(git rev-parse --show-toplevel 2>/dev/null || echo "$HOME/Desktop/xash3d-gc")"
cd "$ROOT"

wake() {
	echo "AGENT_LOOP_WAKE_swiss_sd {\"prompt\":\"$1\"}"
	sleep 120
}

while true; do
	if [[ -f "$HOME/Desktop/XASH3D-GC-FLASH-DEVICE.txt" ]]; then
		wake "Flash dropfile present — run flash-from-dropfile / oneshot; do not complete without real swiss-evidence.txt."
		continue
	fi
	if [[ -f "$HOME/Desktop/XASH3D-GC-SWISS-EVIDENCE.txt" || -f "$HOME/Desktop/swiss-evidence.txt" ]]; then
		wake "Evidence dropfile present — run ingest-from-dropfile / hw-complete; mark goal complete only on PASS."
		continue
	fi
	for m in /run/media/tim/*; do
		[[ -d "$m" ]] || continue
		b="$(basename "$m")"
		case "$b" in Extra|Stream|Expansion) continue ;; esac
		ft="$(findmnt -no FSTYPE "$m" 2>/dev/null || true)"
		case "$ft" in
			vfat|msdos|fat|fat32)
				if [[ ! -d "$m/EFI" && ! -d "$m/efi" ]]; then
					wake "FAT mount $b appeared — stage/ingest; do not complete without real swiss-evidence.txt."
					continue 2
				fi
				;;
		esac
	done
	# list-flash-targets prints "/dev/sdX  …" at column 0 (no leading spaces).
	if scripts/gamecube-swiss-list-flash-targets.sh 2>/dev/null | grep -qE '^/dev/'; then
		wake "Flash candidate disk appeared — list-flash-targets and stage/dd if safe."
		continue
	fi
	sleep 30
done
