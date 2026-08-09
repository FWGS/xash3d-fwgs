#!/usr/bin/env bash
# Ingest a Swiss/hardware OSReport (or Dolphin log dir) into a --require-swiss
# release packet, reusing the green Dolphin G509 soak + audio continuity.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$ROOT"

HW_LOG=""
OUT_DIR=""
ROUTE="sd"
STAGE_ROOT=""
HYPOTHESIS="Swiss FAT + return-to-loader with Dolphin continuity"

usage() {
	cat <<'EOF'
Usage: scripts/gamecube-swiss-evidence.sh --log FILE|DIR [options]

Options:
  --log PATH       Swiss OSReport, swiss-evidence.txt, probe dir, or mounted SD root
  --output DIR     Evidence output (default .ai/logs/operator-evidence-swiss-<stamp>)
  --route ROUTE    sd|carda|cardb|sdgecko (default sd)
  --stage ROOT     Optional: copy OUT/bin/boot.dol into ROOT/apps/xash3d-gc/
                   and print valve layout hints (does not copy Half-Life assets)
  --hypothesis TEXT
                   G501 hypothesis (default Swiss FAT + Dolphin continuity)
  -h, --help       Show help

Expected markers for PASS (OSReport or SD file):
  FAT volume ready sd:/   (or carda:/ / cardb:/)
  FAT preferred volume …
  FAT shutdown (return to Swiss loader via exit stub)

On hardware with FAT, the DOL also writes:
  <vol>/xash3d/valve/logs/swiss-evidence.txt
Copy that file off the SD, or pass the SD mount as --log.
EOF
}

while (($#)); do
	case "$1" in
		--log) HW_LOG="$2"; shift 2 ;;
		--output) OUT_DIR="$2"; shift 2 ;;
		--route) ROUTE="$2"; shift 2 ;;
		--stage) STAGE_ROOT="$2"; shift 2 ;;
		--hypothesis) HYPOTHESIS="$2"; shift 2 ;;
		-h|--help) usage; exit 0 ;;
		*) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
	esac
done

if [[ -z "$HW_LOG" && -z "$STAGE_ROOT" ]]; then
	usage >&2
	exit 2
fi

if [[ -n "$STAGE_ROOT" ]]; then
	[[ "$STAGE_ROOT" = /* ]] || STAGE_ROOT="$ROOT/$STAGE_ROOT"
	mkdir -p "$STAGE_ROOT/apps/xash3d-gc" \
		"$STAGE_ROOT/xash3d/valve/save" \
		"$STAGE_ROOT/xash3d/valve/logs" \
		"$STAGE_ROOT/xash3d/valve/screenshots"
	if [[ -f "$ROOT/OUT/bin/boot.dol" ]]; then
		cp -f "$ROOT/OUT/bin/boot.dol" "$STAGE_ROOT/apps/xash3d-gc/boot.dol"
		echo "Staged: $STAGE_ROOT/apps/xash3d-gc/boot.dol"
	else
		echo "WARN: OUT/bin/boot.dol missing — build first" >&2
	fi
	echo "Copy legal Half-Life valve/ assets to: $STAGE_ROOT/xash3d/valve/"
	echo "Swiss: launch apps/xash3d-gc/boot.dol, play, quit back to Swiss."
	echo "Then copy: $STAGE_ROOT/xash3d/valve/logs/swiss-evidence.txt"
	echo "  (or remount SD and: scripts/gamecube-swiss-evidence.sh --log $STAGE_ROOT)"
fi

if [[ -z "$HW_LOG" ]]; then
	exit 0
fi

[[ "$HW_LOG" = /* ]] || HW_LOG="$ROOT/$HW_LOG"
if [[ ! -e "$HW_LOG" ]]; then
	echo "FAIL: --log not found: $HW_LOG" >&2
	exit 2
fi

# Resolve SD mount / layout dirs to the on-card swiss-evidence.txt when present.
if [[ -d "$HW_LOG" ]]; then
	for cand in \
		"$HW_LOG/xash3d/valve/logs/swiss-evidence.txt" \
		"$HW_LOG/valve/logs/swiss-evidence.txt" \
		"$HW_LOG/swiss-evidence.txt"
	do
		if [[ -f "$cand" ]]; then
			echo "Using FAT swiss-evidence log: $cand"
			HW_LOG="$cand"
			break
		fi
	done
fi

stamp="$(date -u +%Y%m%d-%H%M%S)"
OUT_DIR="${OUT_DIR:-$ROOT/.ai/logs/operator-evidence-swiss-$stamp}"
PROBE_DIR="$OUT_DIR/swiss-probe-logs"
mkdir -p "$PROBE_DIR/dolphin-user/Logs"

if [[ -d "$HW_LOG" ]]; then
	if [[ -f "$HW_LOG/dolphin-user/Logs/dolphin.log" ]]; then
		cp -a "$HW_LOG/." "$PROBE_DIR/"
	elif [[ -f "$HW_LOG/dolphin.log" ]]; then
		cp -f "$HW_LOG/dolphin.log" "$PROBE_DIR/dolphin-user/Logs/dolphin.log"
	else
		# Concatenate any *.log / *.txt as OSReport body
		cat "$HW_LOG"/*.log "$HW_LOG"/*.txt 2>/dev/null \
			> "$PROBE_DIR/dolphin-user/Logs/dolphin.log" || true
	fi
else
	cp -f "$HW_LOG" "$PROBE_DIR/dolphin-user/Logs/dolphin.log"
fi

if [[ ! -s "$PROBE_DIR/dolphin-user/Logs/dolphin.log" ]]; then
	echo "FAIL: no OSReport content under $HW_LOG" >&2
	exit 2
fi

echo "== Swiss marker preflight =="
python3 scripts/waifulib/gamecube_storage.py --parse-log \
	"$PROBE_DIR/dolphin-user/Logs/dolphin.log"

AUDIO_CONT="${ROOT}/.ai/logs/operator-evidence-20260808-audio/merge-continuity"
SOAK="${ROOT}/.ai/logs/soak-probe-20260808-141117"
if [[ ! -d "$SOAK" ]]; then
	echo "FAIL: missing G509 soak reuse dir: $SOAK" >&2
	exit 2
fi

GAMEPLAY_ARGS=()
if [[ -d "$AUDIO_CONT" ]]; then
	GAMEPLAY_ARGS=(--reuse-gameplay "$AUDIO_CONT")
fi

exec scripts/gamecube-operator-evidence.sh \
	--require-swiss \
	--route "$ROUTE" \
	--output "$OUT_DIR" \
	--hypothesis "$HYPOTHESIS" \
	--reuse-probe "$PROBE_DIR" \
	--reuse-soak "$SOAK" \
	"${GAMEPLAY_ARGS[@]}"
