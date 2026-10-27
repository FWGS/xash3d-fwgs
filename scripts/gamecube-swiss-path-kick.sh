#!/usr/bin/env bash
# Oneshot helper for systemd path units: flash and/or ingest Desktop drops,
# then nudge status + notify. Safe to run when nothing is pending (no-op).
set -euo pipefail
ROOT="$(git rev-parse --show-toplevel 2>/dev/null || echo "$HOME/Desktop/xash3d-gc")"
cd "$ROOT"

mkdir -p .ai/logs
LOG=".ai/logs/swiss-path-kick.log"
log() { echo "$(date -u +%Y-%m-%dT%H:%M:%SZ) $*" | tee -a "$LOG"; }

log "==> path-kick mounts=$(ls /run/media/tim 2>/dev/null | tr '\n' ' ')"

if [[ -f "$HOME/Desktop/XASH3D-GC-FLASH-DEVICE.txt" ]]; then
	log "flash dropfile present"
	scripts/gamecube-swiss-flash-from-dropfile.sh || log "flash dropfile: non-zero exit"
fi

if [[ -f "$HOME/Desktop/XASH3D-GC-SWISS-EVIDENCE.txt" || -f "$HOME/Desktop/swiss-evidence.txt" ]]; then
	log "evidence dropfile present"
	scripts/gamecube-swiss-ingest-from-dropfile.sh || log "evidence ingest: non-zero exit"
fi

# New FAT mounts (non-archive): nudge notify so operator sees stage opportunity.
for m in /run/media/tim/*; do
	[[ -d "$m" ]] || continue
	b="$(basename "$m")"
	case "$b" in Extra|Stream|Expansion) continue ;; esac
	fstype="$(findmnt -no FSTYPE "$m" 2>/dev/null || true)"
	case "$fstype" in
		vfat|msdos|fat|fat32)
			if [[ -d "$m/EFI" || -d "$m/efi" ]]; then
				continue
			fi
			log "FAT candidate mount: $b"
			command -v notify-send >/dev/null 2>&1 && \
				notify-send -u critical "Xash3D GC" "FAT mount $b — oneshot/sd-watch should stage (or run hw-status)" || true
			echo "AGENT_LOOP_WAKE_swiss_sd {\"prompt\":\"systemd path saw FAT mount $b — run gamecube-swiss-hw-status.sh; stage/ingest; do not complete without real swiss-evidence.txt.\"}"
			;;
	esac
done

exit 0
