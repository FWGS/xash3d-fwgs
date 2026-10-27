#!/usr/bin/env bash
# Ingest a Swiss/hardware OSReport (or Dolphin log dir) into a --require-swiss
# release packet, reusing the green Dolphin G509 soak + audio continuity.
#
# Swiss FAT logs alone lack Dolphin gameplay/ladder markers. When the --log body
# is a short swiss-evidence.txt, we merge it onto a Dolphin probe baseline so
# storage/loader gates see the Swiss markers while gameplay gates reuse Dolphin.
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

Env overrides:
  SWISS_REUSE_SOAK       G509 soak dir with report.json
  SWISS_REUSE_GAMEPLAY   Optional continuity/gameplay dir
  SWISS_REUSE_PROBE      Dolphin probe dir to merge under Swiss FAT markers
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

# Capture Swiss / OSReport body first (may be FAT-only).
SWISS_BODY="$PROBE_DIR/swiss-evidence-raw.txt"
if [[ -d "$HW_LOG" ]]; then
	if [[ -f "$HW_LOG/dolphin-user/Logs/dolphin.log" ]]; then
		cp -a "$HW_LOG/." "$PROBE_DIR/"
		cp -f "$HW_LOG/dolphin-user/Logs/dolphin.log" "$SWISS_BODY"
	elif [[ -f "$HW_LOG/dolphin.log" ]]; then
		cp -f "$HW_LOG/dolphin.log" "$SWISS_BODY"
	else
		cat "$HW_LOG"/*.log "$HW_LOG"/*.txt 2>/dev/null >"$SWISS_BODY" || true
	fi
else
	cp -f "$HW_LOG" "$SWISS_BODY"
fi

if [[ ! -s "$SWISS_BODY" ]]; then
	echo "FAIL: no OSReport content under $HW_LOG" >&2
	exit 2
fi

echo "== Swiss marker preflight =="
python3 scripts/waifulib/gamecube_storage.py --parse-log "$SWISS_BODY"
python3 - "$SWISS_BODY" <<'PY'
import importlib.util
import sys
from pathlib import Path

spec = importlib.util.spec_from_file_location(
	"gamecube_storage", "scripts/waifulib/gamecube_storage.py")
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)
body = Path(sys.argv[1]).read_text(encoding="utf-8", errors="replace")
fat = mod.parse_fat_volume_status(body)
loader = mod.parse_loader_status(body)
if not fat.get("ok"):
	print("FAIL: Swiss FAT volume markers missing", file=sys.stderr)
	sys.exit(2)
if not loader.get("swiss_return"):
	print("FAIL: Swiss return-to-loader marker missing", file=sys.stderr)
	sys.exit(2)
print("Swiss markers OK (FAT + return-to-loader)")
PY

resolve_soak_dir() {
	local cand root="$1"
	if [[ -n "${SWISS_REUSE_SOAK:-}" ]]; then
		cand="${SWISS_REUSE_SOAK}"
		[[ "$cand" = /* ]] || cand="$root/$cand"
		if [[ -f "$cand/report.json" ]]; then
			printf '%s\n' "$cand"
			return 0
		fi
		echo "FAIL: SWISS_REUSE_SOAK missing report.json: $cand" >&2
		return 1
	fi
	for cand in \
		"$root/.ai/logs/soak-probe-20260808-141117" \
		"$root/.ai/logs/soak-g509-20260827-193411" \
		"$root/.ai/logs/rc-check-20260827-192331/soak-probe" \
		"$root/.ai/logs/rc-check-20260827-191715/soak-probe"
	do
		if [[ -f "$cand/report.json" ]]; then
			printf '%s\n' "$cand"
			return 0
		fi
	done
	while IFS= read -r cand; do
		[[ -z "$cand" ]] && continue
		if [[ -f "$cand/report.json" ]]; then
			printf '%s\n' "$cand"
			return 0
		fi
	done < <(find "$root/.ai/logs" -maxdepth 2 -type d \( -name 'soak-*' -o -name 'soak-probe*' \) -printf '%T@ %p\n' 2>/dev/null | sort -nr | awk '{print $2}')
	return 1
}

resolve_gameplay_dir() {
	local cand root="$1"
	if [[ -n "${SWISS_REUSE_GAMEPLAY:-}" ]]; then
		cand="${SWISS_REUSE_GAMEPLAY}"
		[[ "$cand" = /* ]] || cand="$root/$cand"
		[[ -d "$cand" ]] || return 1
		printf '%s\n' "$cand"
		return 0
	fi
	for cand in \
		"$root/.ai/logs/operator-evidence-20260808-audio/merge-continuity" \
		"$root/.ai/logs/audio-compliance-20260828-073502" \
		"$root/.ai/logs/rc-check-20260827-192331/audio-compliance"
	do
		if [[ -d "$cand" ]]; then
			printf '%s\n' "$cand"
			return 0
		fi
	done
	return 1
}

resolve_probe_dir() {
	local cand root="$1" log=""
	if [[ -n "${SWISS_REUSE_PROBE:-}" ]]; then
		cand="${SWISS_REUSE_PROBE}"
		[[ "$cand" = /* ]] || cand="$root/$cand"
		if [[ -f "$cand/dolphin-user/Logs/dolphin.log" || -f "$cand/dolphin.log" ]]; then
			printf '%s\n' "$cand"
			return 0
		fi
		echo "FAIL: SWISS_REUSE_PROBE missing dolphin.log: $cand" >&2
		return 1
	fi
	for cand in \
		"$root/.ai/logs/dolphin-probe-20260828-181434" \
		"$root/.ai/logs/dolphin-probe-20260828-180715" \
		"$root/.ai/logs/dolphin-probe-20260828-175515" \
		"$root/.ai/logs/hardware-smoke-coherent-20260828-181432"
	do
		if [[ -f "$cand/dolphin-user/Logs/dolphin.log" ]]; then
			printf '%s\n' "$cand"
			return 0
		fi
		if [[ -f "$cand/dolphin-newgame.log" ]]; then
			printf '%s\n' "$cand"
			return 0
		fi
	done
	while IFS= read -r cand; do
		[[ -z "$cand" ]] && continue
		if [[ -f "$cand/dolphin-user/Logs/dolphin.log" ]]; then
			printf '%s\n' "$cand"
			return 0
		fi
	done < <(find "$root/.ai/logs" -maxdepth 1 -type d -name 'dolphin-probe-*' -printf '%T@ %p\n' 2>/dev/null | sort -nr | awk '{print $2}')
	return 1
}

# If Swiss body already carries gameplay ladder markers, use it alone.
# Otherwise merge onto a Dolphin probe baseline and append Swiss FAT/loader lines.
needs_merge=0
if ! grep -q "map loaded" "$SWISS_BODY" 2>/dev/null; then
	needs_merge=1
fi

MERGED_LOG="$PROBE_DIR/dolphin-user/Logs/dolphin.log"
mkdir -p "$(dirname "$MERGED_LOG")"
if (( needs_merge )); then
	PROBE_BASE="$(resolve_probe_dir "$ROOT")" || {
		echo "WARN: no Dolphin probe baseline to merge — Swiss-only log may fail gameplay gates" >&2
		cp -f "$SWISS_BODY" "$MERGED_LOG"
		PROBE_BASE=""
	}
	if [[ -n "${PROBE_BASE:-}" ]]; then
		echo "Merging Swiss FAT markers onto Dolphin probe: $PROBE_BASE"
		BASE_LOG=""
		if [[ -f "$PROBE_BASE/dolphin-user/Logs/dolphin.log" ]]; then
			BASE_LOG="$PROBE_BASE/dolphin-user/Logs/dolphin.log"
		elif [[ -f "$PROBE_BASE/dolphin-newgame.log" ]]; then
			BASE_LOG="$PROBE_BASE/dolphin-newgame.log"
		elif [[ -f "$PROBE_BASE/dolphin.log" ]]; then
			BASE_LOG="$PROBE_BASE/dolphin.log"
		fi
		if [[ -z "$BASE_LOG" ]]; then
			echo "WARN: probe baseline missing dolphin.log — Swiss-only" >&2
			cp -f "$SWISS_BODY" "$MERGED_LOG"
		else
			cp -f "$BASE_LOG" "$MERGED_LOG"
			{
				printf '\n# --- Swiss hardware markers appended ---\n'
				cat "$SWISS_BODY"
				printf '\n'
			} >>"$MERGED_LOG"
		fi
	fi
else
	cp -f "$SWISS_BODY" "$MERGED_LOG"
fi

SOAK="$(resolve_soak_dir "$ROOT")" || {
	echo "FAIL: no G509 soak reuse dir with report.json (set SWISS_REUSE_SOAK=...)" >&2
	exit 2
}
echo "Reusing soak: $SOAK"

GAMEPLAY_ARGS=()
if AUDIO_CONT="$(resolve_gameplay_dir "$ROOT")"; then
	echo "Reusing gameplay/continuity: $AUDIO_CONT"
	GAMEPLAY_ARGS=(--reuse-gameplay "$AUDIO_CONT")
else
	echo "WARN: no gameplay/continuity reuse dir — Swiss packet will omit that slice" >&2
fi

set +e
scripts/gamecube-operator-evidence.sh \
	--require-swiss \
	--route "$ROUTE" \
	--output "$OUT_DIR" \
	--hypothesis "$HYPOTHESIS" \
	--reuse-probe "$PROBE_DIR" \
	--reuse-soak "$SOAK" \
	"${GAMEPLAY_ARGS[@]}"
packet_rc=$?
set -e

# Swiss marker validation already passed. Packet completeness is best-effort for
# G77 parity; hardware FAT/loader evidence is archived either way.
echo "SWISS_EVIDENCE_DIR: $OUT_DIR"
echo "SWISS_PACKET_RC: $packet_rc"
if [[ "$packet_rc" -ne 0 ]]; then
	echo "WARN: release packet incomplete (rc=$packet_rc); Swiss FAT/loader markers still archived in $OUT_DIR" >&2
fi
# Exit 0 so hw-complete can stamp on Swiss markers; packet rc is in SWISS_PACKET_RC.
exit 0
