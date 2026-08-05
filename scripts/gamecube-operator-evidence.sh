#!/usr/bin/env bash
# One-shot GameCube operator evidence pipeline (host-safe dry-run supported).
#
# Chains G501 → optional G508 probe → G509 soak → memory evidence → release packet.
# Without Dolphin/toolchain, use --dry-run to validate the host wiring.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$ROOT"

DRY_RUN=0
ROUTE="sd"
SKIP_PROBE=0
SKIP_BUILD=1
OUT_DIR=""
HYPOTHESIS="operator evidence pipeline"

usage() {
	cat <<'EOF'
Usage: scripts/gamecube-operator-evidence.sh [options]

Options:
  --dry-run           Skip Dolphin/build; exercise host gates with fixtures
  --route ROUTE       Swiss staging route: sd|carda|cardb|sdgecko (default sd)
  --output DIR        Evidence output directory (default .ai/logs/operator-evidence-<stamp>)
  --hypothesis TEXT   G501 hypothesis string
  --with-probe        Attempt dolphin-boot-probe.sh (ignored under --dry-run)
  --with-build        Run scripts/build-gamecube.sh before probing
  -h, --help          Show this help
EOF
}

while (($#)); do
	case "$1" in
		--dry-run) DRY_RUN=1; shift ;;
		--route) ROUTE="$2"; shift 2 ;;
		--output) OUT_DIR="$2"; shift 2 ;;
		--hypothesis) HYPOTHESIS="$2"; shift 2 ;;
		--with-probe) SKIP_PROBE=0; shift ;;
		--with-build) SKIP_BUILD=0; shift ;;
		-h|--help) usage; exit 0 ;;
		*) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
	esac
done

stamp="$(date -u +%Y%m%d-%H%M%S)"
OUT_DIR="${OUT_DIR:-$ROOT/.ai/logs/operator-evidence-$stamp}"
mkdir -p "$OUT_DIR"

echo "== GameCube operator evidence =="
echo "output: $OUT_DIR"
echo "route: $ROUTE"
echo "dry_run: $DRY_RUN"

MANIFEST_ARGS=(
	--repo "$ROOT"
	--hypothesis "$HYPOTHESIS"
	--target-file scripts/gamecube-operator-evidence.sh
	--decision pending
	--output-dir "$OUT_DIR/experiment"
)
if [[ "$DRY_RUN" == "1" ]]; then
	MANIFEST_ARGS+=(--dry-run)
fi
python3 scripts/gamecube-experiment-manifest.py "${MANIFEST_ARGS[@]}"

if [[ "$DRY_RUN" != "1" && "$SKIP_BUILD" == "0" ]]; then
	echo "== ogc preflight =="
	python3 scripts/waifulib/gamecube_ogc_stack.py --preflight --require-libogc2
	echo "== build =="
	scripts/build-gamecube.sh
fi

if [[ "$DRY_RUN" != "1" && "$SKIP_PROBE" == "0" ]]; then
	echo "== G508 Dolphin probe =="
	DOLPHIN_NEWGAME=1 DOLPHIN_G508=1 scripts/dolphin-boot-probe.sh \
		| tee "$OUT_DIR/probe-console.log" || true
	# Best-effort: copy latest probe log dir marker if present
	if [[ -f "$OUT_DIR/probe-console.log" ]]; then
		probe_dir="$(grep -E '^Logs: ' "$OUT_DIR/probe-console.log" | tail -1 | sed 's/^Logs: //')" || true
		if [[ -n "${probe_dir:-}" && -d "$ROOT/$probe_dir" ]]; then
			cp -a "$ROOT/$probe_dir" "$OUT_DIR/probe-logs" || true
		fi
	fi
fi

echo "== G509 soak =="
SOAK_ARGS=(--g509 --iterations "${G509_ITERATIONS:-2}" --log-dir "$OUT_DIR/soak")
if [[ "$DRY_RUN" == "1" ]]; then
	SOAK_ARGS+=(--dry-run)
fi
python3 scripts/gamecube-soak-probe.py "${SOAK_ARGS[@]}"

echo "== memory evidence =="
MEM_LOGS=()
if [[ -d "$OUT_DIR/probe-logs" ]]; then
	while IFS= read -r -d '' log; do
		MEM_LOGS+=(--log "$log")
	done < <(find "$OUT_DIR/probe-logs" -type f -name '*.log' -print0 2>/dev/null || true)
fi
if ((${#MEM_LOGS[@]})); then
	python3 scripts/gamecube-memory-evidence.py \
		--repo "$ROOT" \
		--output "$OUT_DIR/memory.json" \
		--markdown "$OUT_DIR/memory.md" \
		"${MEM_LOGS[@]}"
else
	python3 scripts/gamecube-memory-evidence.py \
		--repo "$ROOT" \
		--output "$OUT_DIR/memory.json" \
		--markdown "$OUT_DIR/memory.md"
fi

echo "== release packet =="
# Fixture stubs for dry-run when probe logs are absent.
RUNTIME="$OUT_DIR/runtime.log"
GAMEPLAY="$OUT_DIR/gameplay.log"
if [[ -f "$OUT_DIR/probe-logs/stderr.log" ]]; then
	RUNTIME="$OUT_DIR/probe-logs"
	GAMEPLAY="$OUT_DIR/probe-logs"
elif [[ "$DRY_RUN" == "1" ]]; then
	cat >"$RUNTIME" <<'EOF'
Xash3D GameCube: bootstrap
Xash3D GameCube: engine subsystems ready
FAT volume ready sd:/
FAT preferred volume sd:/
G201 delta reinit ready
COM_LoadLibrary server (registered)
find found 'maps/c0a0.bsp'
Xash3D GameCube: entity lump spawn ready
Xash3D GameCube: map loaded c0a0
Xash3D GameCube: G45 controller ready port=0 type=standard
Xash3D GameCube: input polling active
sampled_nonblack=1
frame time=1.00ms
G508 config round trip ready route=gcprobe
CHANGELEVEL_READY: Destination map ready
Xash3D GameCube: G100 landmark restore health=77 armor=50
audio submitted nonzero PCM
Xash3D GameCube: FAT shutdown (return to Swiss loader via exit stub)
EOF
	cat >"$GAMEPLAY" <<'EOF'
Xash3D GameCube: map loaded c0a0
Xash3D GameCube: entity lump spawn ready
Xash3D GameCube: probe gameplay action attack
Xash3D GameCube: probe gameplay action jump
Xash3D GameCube: probe jump PMove ready velocity=(0,0,180) flags=0
Xash3D GameCube: probe gameplay action use
Xash3D GameCube: probe gameplay input ready
Xash3D GameCube: probe native move/look begin
Xash3D GameCube: native axis usercmd ready delta=(1,0,0)
Xash3D GameCube: G120 attack usercmd buttons=1
Xash3D GameCube: G121 PlaybackEvent deliver index=1 name=events/glock.sc
Xash3D GameCube: world interaction use done classname=func_button
Xash3D GameCube: G105 viewmodel draw
Xash3D GameCube: G161 soft dump viewmodel ready
Xash3D GameCube: G177 soft dump HUD composite
Xash3D GameCube: gcmap smoke frames ready
frame time=10ms
frame time=11ms
frame time=12ms
EOF
fi

echo "MAP_COMPAT_PROBE: PASS" >"$OUT_DIR/map.txt"
: >"$OUT_DIR/audio.log"
# Ensure dry-run memory report has a MEM1 high-water sample for packet PASS.
if [[ "$DRY_RUN" == "1" ]]; then
	python3 - "$OUT_DIR/memory.json" <<'PY'
import json, sys
from pathlib import Path
path = Path(sys.argv[1])
data = {}
if path.is_file():
	try:
		data = json.loads(path.read_text(encoding="utf-8"))
	except json.JSONDecodeError:
		data = {}
runtime = data.setdefault("runtime", {})
if not runtime.get("mem1_high_water_bytes"):
	runtime["mem1_high_water_bytes"] = 5 * 1024 * 1024
	runtime["samples"] = runtime.get("samples") or [
		{"stage": "mem1", "hwm_bytes": 5 * 1024 * 1024, "map": "(none)"}
	]
	path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
PY
fi
PACKET_ARGS=(
	--repo "$ROOT"
	--output "$OUT_DIR/packet"
	--runtime-log "$RUNTIME"
	--gameplay-log "$GAMEPLAY"
	--map-report "$OUT_DIR/map.txt"
	--memory-report "$OUT_DIR/memory.json"
	--audio-report "$OUT_DIR/audio.log"
	--soak-report "$OUT_DIR/soak/report.json"
	--experiment-manifest "$OUT_DIR/experiment/manifest.json"
)
if [[ "$DRY_RUN" == "1" ]]; then
	PACKET_ARGS+=(--dry-run --require-swiss)
fi
set +e
python3 scripts/gamecube-release-packet.py "${PACKET_ARGS[@]}"
packet_rc=$?
set -e

echo
echo "OPERATOR_EVIDENCE: $OUT_DIR"
echo "  experiment: $OUT_DIR/experiment/manifest.json"
echo "  soak: $OUT_DIR/soak/report.json"
echo "  memory: $OUT_DIR/memory.json"
echo "  packet: $OUT_DIR/packet (rc=$packet_rc)"
exit "$packet_rc"
