#!/usr/bin/env bash
# One-shot: ingest Swiss hardware evidence and stamp G38/HW-012 docs.
# Does not invent markers — requires a real OSReport / swiss-evidence.txt.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

LOG=""
ROUTE="sd"
DRY=0
OUT_DIR=""
BOOT_ONLY=0

usage() {
	cat <<'EOF'
Usage: scripts/gamecube-swiss-hw-complete.sh --log FILE|DIR [--route sd|carda|cardb] [--dry-run] [--boot-only]

After a real Swiss run (boot → play → quit), remount the SD and run:

  scripts/gamecube-swiss-hw-complete.sh --log /path/to/sd

This will:
  1. Run scripts/gamecube-swiss-evidence.sh --require-swiss
  2. Require FAT + return-to-loader markers (and map loaded + entity spawn
     unless --boot-only)
  3. Append a dated HW-012/G38 note to docs/GAMECUBE_PORT_PLAN.md
  4. Update .ai/tasks/gc-port-current.md NEXT pointer

Refuses to mark complete unless FAT/Swiss markers parse as PASS.
EOF
}

while (( $# > 0 )); do
	case "$1" in
		--log) LOG="$2"; shift ;;
		--route) ROUTE="$2"; shift ;;
		--dry-run) DRY=1 ;;
		--boot-only) BOOT_ONLY=1 ;;
		--output) OUT_DIR="$2"; shift ;;
		-h|--help) usage; exit 0 ;;
		*) echo "Unknown: $1" >&2; usage >&2; exit 2 ;;
	esac
	shift
done

if [[ -z "$LOG" ]]; then
	usage >&2
	exit 2
fi
[[ "$LOG" = /* ]] || LOG="$ROOT/$LOG"
if [[ ! -e "$LOG" ]]; then
	echo "FAIL: --log not found: $LOG" >&2
	exit 2
fi

EVIDENCE_ARGS=(--log "$LOG" --route "$ROUTE")
if [[ -n "$OUT_DIR" ]]; then
	EVIDENCE_ARGS+=(--output "$OUT_DIR")
fi

if (( DRY )); then
	echo "DRY-RUN: would run gamecube-swiss-evidence.sh ${EVIDENCE_ARGS[*]}"
	# Still parse markers so dry-run reports readiness
	RESOLVED="$LOG"
	if [[ -d "$LOG" ]]; then
		for cand in \
			"$LOG/xash3d/valve/logs/swiss-evidence.txt" \
			"$LOG/valve/logs/swiss-evidence.txt" \
			"$LOG/swiss-evidence.txt"
		do
			if [[ -f "$cand" ]]; then
				RESOLVED="$cand"
				break
			fi
		done
	fi
	if [[ -f "$RESOLVED" ]]; then
		python3 scripts/waifulib/gamecube_storage.py --parse-log "$RESOLVED" || true
	else
		echo "DRY-RUN: no swiss-evidence.txt under $LOG yet"
	fi
	exit 0
fi

echo "==> Ingest Swiss evidence"
INGEST_LOG="$(mktemp)"
set +e
scripts/gamecube-swiss-evidence.sh "${EVIDENCE_ARGS[@]}" >"$INGEST_LOG" 2>&1
ingest_rc=$?
set -e
cat "$INGEST_LOG"
EVIDENCE_OUT="$(awk '/^SWISS_EVIDENCE_DIR:|^OPERATOR_EVIDENCE:|^output:/ {print $NF; exit}' "$INGEST_LOG" || true)"
rm -f "$INGEST_LOG"
# Prefer newest swiss evidence dir if parse of stdout failed
if [[ -z "${EVIDENCE_OUT:-}" || ! -d "${EVIDENCE_OUT:-}" ]]; then
	EVIDENCE_OUT="$(ls -dt .ai/logs/operator-evidence-swiss-* .ai/logs/swiss-ingest-* 2>/dev/null | head -1 || true)"
fi
if [[ -z "${EVIDENCE_OUT:-}" || ! -d "$EVIDENCE_OUT" ]]; then
	echo "FAIL: swiss evidence packet not created (ingest_rc=$ingest_rc)" >&2
	exit 1
fi
if [[ "$ingest_rc" -ne 0 ]]; then
	echo "FAIL: gamecube-swiss-evidence.sh exited $ingest_rc" >&2
	exit 1
fi

# Require Swiss markers (prefer raw FAT log; fall back to merged probe log)
MARKER_LOG=""
for cand in \
	"$EVIDENCE_OUT/swiss-probe-logs/swiss-evidence-raw.txt" \
	"$EVIDENCE_OUT/swiss-probe-logs/dolphin-user/Logs/dolphin.log" \
	"$EVIDENCE_OUT/swiss-probe-logs/dolphin.log"
do
	if [[ -f "$cand" ]]; then
		MARKER_LOG="$cand"
		break
	fi
done
if [[ -z "$MARKER_LOG" ]]; then
	echo "FAIL: no marker log in $EVIDENCE_OUT" >&2
	exit 1
fi


PARSE_JSON_FILE="$(mktemp)"
python3 scripts/waifulib/gamecube_storage.py --parse-log "$MARKER_LOG" >"$PARSE_JSON_FILE" || true
cat "$PARSE_JSON_FILE"
python3 - "$PARSE_JSON_FILE" "$BOOT_ONLY" <<'PY'
import json, sys
data = json.load(open(sys.argv[1], encoding="utf-8"))
boot_only = sys.argv[2] == "1"
fat_ok = bool(data.get("fat", {}).get("ok"))
swiss = bool(data.get("loader", {}).get("swiss_return"))
playable = data.get("playable") or {}
if not fat_ok:
	print("FAIL: FAT volume markers missing/not ok", file=sys.stderr)
	sys.exit(1)
if not swiss:
	print("FAIL: Swiss return-to-loader marker missing", file=sys.stderr)
	sys.exit(1)
if not boot_only and not playable.get("ok"):
	print("FAIL: playable markers missing (need map loaded + entity lump spawn ready)", file=sys.stderr)
	print("      Re-run Swiss: New Game → wait for map → quit; or pass --boot-only", file=sys.stderr)
	sys.exit(1)
if playable.get("ok"):
	maps = ",".join(playable.get("map_loaded") or [])
	print(f"Swiss markers OK (FAT + return-to-loader + playable maps={maps})")
else:
	print("Swiss markers OK (FAT + return-to-loader; boot-only)")
PY
rm -f "$PARSE_JSON_FILE"

DOL_HASH="$(sha256sum OUT/bin/boot.dol 2>/dev/null | awk '{print $1}' || echo missing)"
STAMP="$(date -u +%Y-%m-%d)"
NOTE="**Hardware evidence ${STAMP}:** Swiss route=${ROUTE}; boot.dol sha256 \`${DOL_HASH:0:8}…\`; packet \`$EVIDENCE_OUT\`. TODO-HW-012/G38 operator run recorded via gamecube-swiss-hw-complete.sh."

python3 - <<PY
from pathlib import Path
import re
root = Path("$ROOT")
plan = root / "docs/GAMECUBE_PORT_PLAN.md"
text = plan.read_text(encoding="utf-8")
needle = "Dated Swiss operator evidence still required."
note = """$NOTE"""
if note.split(";")[0] in text:
    print("port plan already has this stamp")
else:
    if needle in text:
        text = text.replace(needle, note + "\n  Was: " + needle, 1)
    else:
        # Append under TODO-HW-012 block
        text = re.sub(
            r"(- \[ \] \*\*TODO-HW-012[^\n]*\n(?:  [^\n]*\n)*)",
            r"\1  " + note + "\n",
            text,
            count=1,
        )
    plan.write_text(text, encoding="utf-8")
    print("updated", plan)

task = root / ".ai/tasks/gc-port-current.md"
tt = task.read_text(encoding="utf-8")
line = f"- Swiss evidence packet: \`$EVIDENCE_OUT\` (route=$ROUTE)\n"
if line not in tt:
    tt = tt.replace(
        "**NEXT**",
        "**NEXT**\n" + line,
        1,
    )
    task.write_text(tt, encoding="utf-8")
    print("updated", task)
print("PASS: Swiss evidence ingested at", "$EVIDENCE_OUT")
PY
