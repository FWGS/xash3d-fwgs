#!/usr/bin/env bash
# Ingest swiss-evidence.txt dropped onto the Desktop after an off-host GC run.
# Accepts either:
#   ~/Desktop/XASH3D-GC-SWISS-EVIDENCE.txt   (preferred; renamed after ingest)
#   ~/Desktop/swiss-evidence.txt            (same)
# Runs gamecube-swiss-hw-complete.sh --log on a preserved copy under .ai/logs/.
set -euo pipefail
ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$ROOT"

LOG=".ai/logs/swiss-evidence-drop.log"
mkdir -p "$(dirname "$LOG")"
log() { echo "$(date -u +%Y-%m-%dT%H:%M:%SZ) $*" | tee -a "$LOG"; }

pick=""
for cand in \
	"$HOME/Desktop/XASH3D-GC-SWISS-EVIDENCE.txt" \
	"$HOME/Desktop/swiss-evidence.txt"
do
	if [[ -f "$cand" ]]; then
		pick="$cand"
		break
	fi
done

if [[ -z "$pick" ]]; then
	exit 0
fi

# Ignore empty / placeholder stubs. Require engine-format Swiss markers
# (see GCube_WriteSwissEvidenceLog / GCube_AppendSwissEvidence).
if [[ ! -s "$pick" ]]; then
	log "WARN: empty evidence drop $pick — leave for operator"
	exit 0
fi
if ! grep -qE 'FAT (preferred volume|volume ready|shutdown)|map loaded |playable present |swissquit' "$pick" 2>/dev/null; then
	log "WARN: $pick lacks engine Swiss markers — leave for operator"
	exit 0
fi

stamp=$(date +%Y%m%d-%H%M%S)
outdir=".ai/logs/swiss-hw-drop-ingest-${stamp}"
mkdir -p "$outdir"
cp -a "$pick" "$outdir/swiss-evidence.txt"
log "==> Ingesting Desktop evidence drop $pick → $outdir"

if scripts/gamecube-swiss-hw-complete.sh --log "$outdir/swiss-evidence.txt"; then
	mv -f "$pick" "${pick}.done-${stamp}"
	log "OK ingest complete — moved drop to ${pick}.done-${stamp}"
	command -v notify-send >/dev/null 2>&1 && \
		notify-send -u critical "Xash3D GC" "swiss-evidence.txt ingested from Desktop drop" || true
	echo "AGENT_LOOP_WAKE_swiss_sd {\"prompt\":\"Desktop swiss-evidence drop ingested via hw-complete — verify G38/HW markers and mark goal complete only if PASS.\"}"
	exit 0
fi

log "FAIL: hw-complete rejected $outdir/swiss-evidence.txt"
exit 1
