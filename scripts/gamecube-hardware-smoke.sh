#!/usr/bin/env bash
# Minimal G77 hardware smoke prep + Dolphin proxy validation.
# Physical GameCube / Swiss steps are documented for the operator; this script
# automates everything that can run on the host without EXI hardware.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT" || exit 1

STAMP="$(date +%Y%m%d-%H%M%S)"
LOG_DIR="${G77_LOG_DIR:-.ai/logs/hardware-smoke-$STAMP}"
SUMMARY="$LOG_DIR/summary.md"
BUILD=0
BUILD_DISC=0
DOLPHIN_PROXY=1
SMOKE_MAP="${G77_SMOKE_MAP:-c0a0e}"
CHANGELEVEL_ROUTE="${G77_CHANGELEVEL_ROUTE:-c0a0e:c1a0}"

usage() {
	cat <<'EOF'
Usage: scripts/gamecube-hardware-smoke.sh [--build] [--build-disc] [--no-dolphin]

Prepares G77 hardware smoke evidence:
  1. Optional GameCube build + smoke ISO
  2. Hardware handoff manifest (artifact hashes for Dolphin/hardware parity)
  3. Dolphin proxy: New Game + changelevel continuity (when assets + Dolphin exist)
  4. Operator checklist for physical Swiss validation

Environment:
  G77_LOG_DIR            Output directory (default: .ai/logs/hardware-smoke-<stamp>)
  G77_SMOKE_MAP          Boot smoke map (default: c0a0e)
  G77_CHANGELEVEL_ROUTE  Proxy changelevel FROM:TO (default: c0a0e:c1a0)
  DOLPHIN_TIMEOUT        Probe timeout seconds (default: 180)
EOF
}

while (( $# > 0 )); do
	case "$1" in
		--build) BUILD=1 ;;
		--build-disc) BUILD=1; BUILD_DISC=1 ;;
		--no-dolphin) DOLPHIN_PROXY=0 ;;
		-h|--help) usage; exit 0 ;;
		*) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
	esac
	shift
done

mkdir -p "$LOG_DIR"
COMMIT="$(git rev-parse --short HEAD)"
DIRTY=""
if ! git diff --quiet || ! git diff --cached --quiet; then
	DIRTY="-dirty"
fi

echo "==> G77 hardware smoke prep commit=${COMMIT}${DIRTY} log=$LOG_DIR"

if (( BUILD )); then
	echo "==> Building GameCube artifacts"
	scripts/build-gamecube.sh >"$LOG_DIR/build-gamecube.log" 2>&1 || {
		echo "FAIL: build — see $LOG_DIR/build-gamecube.log" >&2
		exit 1
	}
fi

if (( BUILD_DISC )); then
	if [[ ! -d Half-Life/valve ]]; then
		echo "FAIL: --build-disc requires Half-Life/valve" >&2
		exit 1
	fi
	echo "==> Building smoke ISO"
	scripts/build-gamecube-disc.py --smoke-map "$SMOKE_MAP" \
		>"$LOG_DIR/build-disc.log" 2>&1 || {
		echo "FAIL: disc build — see $LOG_DIR/build-disc.log" >&2
		exit 1
	}
fi

echo "==> Hardware handoff manifest"
G38_LOG_DIR="$LOG_DIR/handoff" \
	scripts/gamecube-hardware-handoff.sh >"$LOG_DIR/handoff.log" 2>&1

echo "==> G56 boot checklist preflight"
python3 scripts/gamecube-hardware-boot-check.py \
	--log-dir "$LOG_DIR/boot-check" >"$LOG_DIR/boot-check.log" 2>&1 \
	|| echo "WARN: G56 boot checklist reported failures (see boot-check.log)"

DOLPHIN_STATUS="SKIPPED"
DOLPHIN_NOTE="--no-dolphin or missing prerequisites"
if (( DOLPHIN_PROXY )); then
	if [[ -f OUT/bin/boot.dol ]] && [[ -d Half-Life/valve ]]; then
		# shellcheck source=scripts/gamecube-env.sh
		source scripts/gamecube-env.sh 2>/dev/null || true
		if command -v "$DOLPHIN_EXECUTABLE" &>/dev/null \
			|| [[ "${DOLPHIN_EXECUTABLE:-}" == flatpak:* ]]; then
			echo "==> Dolphin proxy: New Game + changelevel ($CHANGELEVEL_ROUTE)"
			FROM="${CHANGELEVEL_ROUTE%%:*}"
			TO="${CHANGELEVEL_ROUTE##*:}"
			export DOLPHIN_TIMEOUT="${DOLPHIN_TIMEOUT:-180}"
			export DOLPHIN_NEWGAME=1
			export DOLPHIN_G508=1
			if DOLPHIN_CHANGELEVEL="$TO" \
				DOLPHIN_CHANGELEVEL_FROM="$FROM" \
				scripts/dolphin-boot-probe.sh \
				>"$LOG_DIR/dolphin-newgame.log" 2>&1; then
				DOLPHIN_STATUS="PASS"
				DOLPHIN_NOTE="Dolphin proxy reached readiness markers"
			else
				DOLPHIN_STATUS="FAIL"
				DOLPHIN_NOTE="Dolphin proxy failed — see dolphin-newgame.log"
			fi
		else
			DOLPHIN_NOTE="Dolphin executable not found"
		fi
	else
		DOLPHIN_NOTE="missing OUT/bin/boot.dol or Half-Life/valve (use --build --build-disc)"
	fi
fi

python3 -m pytest tests/test_gamecube_host.py -q \
	>"$LOG_DIR/host-tests.log" 2>&1 || true
HOST_TESTS="$(tail -1 "$LOG_DIR/host-tests.log" 2>/dev/null || echo 'unknown')"

cat >"$SUMMARY" <<EOF
# G77 Hardware Smoke Summary

- Generated: $(date -u +"%Y-%m-%dT%H:%M:%SZ")
- Commit: \`${COMMIT}${DIRTY}\`
- Log directory: \`$LOG_DIR\`
- Handoff manifest: \`$LOG_DIR/handoff/artifact-manifest.tsv\`
- Dolphin proxy status: **$DOLPHIN_STATUS** — $DOLPHIN_NOTE
- Host tests: $HOST_TESTS

## Physical hardware steps (operator)

1. Copy \`OUT/bin/boot.dol\` (or burn \`OUT/xash3d-gc.iso\`) to Swiss / SD2SP2 / SD Gecko.
2. Stage legal \`Half-Life/valve\` assets to \`<vol>:/xash3d/valve/\`.
3. Boot → confirm OSReport bootstrap + nonblack gameplay on \`$SMOKE_MAP\`.
4. Run New Game → one changelevel (\`$CHANGELEVEL_ROUTE\`) → quit to Swiss.
5. Capture Swiss FAT markers or \`scripts/gamecube-swiss-evidence.sh --log /path/to/sd\`.
6. Record evidence in \`$LOG_DIR/handoff/evidence-template.md\` and port plan G77 section.

## Dolphin/hardware parity artifacts

Same commit and \`boot.dol\` hash must be used for both Dolphin proxy and hardware runs.
See \`docs/GAMECUBE_HARDWARE_VALIDATION.md\` for failure labels.

## Next

- Attach completed hardware evidence to close G77.
- Reassemble release packet with \`--require-swiss\` when hardware log is available.
EOF

cat "$SUMMARY"
if [[ "$DOLPHIN_STATUS" == "FAIL" ]]; then
	exit 1
fi
