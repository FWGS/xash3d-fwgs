#!/usr/bin/env bash
# Build a retail-menu GameCube ISO, boot in Dolphin, and save a main-menu screenshot.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

if [[ -f scripts/gamecube-env.sh ]]; then
	# shellcheck disable=SC1091
	source scripts/gamecube-env.sh
fi

DATA="${GC_RETAIL_DATA:-Half-Life/valve}"
OUT_DIR="${GC_MENU_SCREENSHOT_DIR:-.ai/screenshots}"
LOG_DIR=".ai/logs/gc-main-menu-$(date +%Y%m%d-%H%M%S)"
SCREENSHOT="$OUT_DIR/gc-main-menu.png"
REFERENCE="$OUT_DIR/retail-main-menu-reference.png"

mkdir -p "$OUT_DIR" "$LOG_DIR"

echo "==> Building GameCube engine..."
bash scripts/build-gamecube.sh

echo "==> Building retail menu disc (skip startup vids)..."
python3 scripts/build-gamecube-disc.py \
	--data "$DATA" \
	--output OUT/xash3d-gc.iso \
	--skip-startup-vids

echo "==> Rendering baked menu preview..."
python3 scripts/render-gc-menu-screenshot.py --data "$DATA" --out-dir "$OUT_DIR"

echo "==> Booting retail menu in Dolphin..."
export DOLPHIN_RETAIL=1
export DOLPHIN_SKIP_BUILD=1
export DOLPHIN_CAPTURE_MENU=1
export DOLPHIN_TIMEOUT="${DOLPHIN_MENU_TIMEOUT:-150}"
export GC_BOOT_PROBE_TIMEOUT="${GC_BOOT_PROBE_TIMEOUT:-180}"
set +e
scripts/dolphin-boot-probe.sh OUT/xash3d-gc.iso 2>&1 | tee "$LOG_DIR/probe.log"
PROBE_RC=${PIPESTATUS[0]}
set -e

if (( PROBE_RC != 0 )) && ! grep -aq 'RETAIL_READY:' "$LOG_DIR/probe.log"; then
	echo "FAIL: retail menu probe failed before reaching menu (rc=$PROBE_RC)" >&2
	exit "$PROBE_RC"
fi

PROBE_LOG="$(grep -a '^Logs:' "$LOG_DIR/probe.log" | tail -1 | awk '{print $2}')"
if [[ -z "$PROBE_LOG" ]]; then
	PROBE_LOG="$(ls -td .ai/logs/dolphin-probe-202* 2>/dev/null | head -1)"
fi

USER_DIR="$PROBE_LOG/dolphin-user"
FRAME_ROOT="$USER_DIR/Dump/Frames"
LATEST_FRAME=""
if [[ -d "$FRAME_ROOT" ]]; then
	LATEST_FRAME="$(find "$FRAME_ROOT" -type f \( -name '*.png' -o -name '*.jpg' \) -printf '%T@ %p\n' 2>/dev/null | sort -n | tail -1 | cut -d' ' -f2-)"
fi
if [[ -n "$LATEST_FRAME" ]]; then
	cp -f "$LATEST_FRAME" "$SCREENSHOT"
	echo "Saved Dolphin frame screenshot: $SCREENSHOT"
elif [[ -f "$OUT_DIR/gc-main-menu.png" ]]; then
	cp -f "$OUT_DIR/gc-main-menu.png" "$SCREENSHOT"
	echo "Saved baked menu preview screenshot: $SCREENSHOT"
else
	echo "FAIL: no Dolphin frame dump and no baked menu preview" >&2
	exit 1
fi

if [[ -f "$OUT_DIR/retail-main-menu-reference.png" ]]; then
	echo "Retail reference: $OUT_DIR/retail-main-menu-reference.png"
fi
if [[ -f "$OUT_DIR/gc-main-menu-vs-retail.png" ]]; then
	echo "Comparison image: $OUT_DIR/gc-main-menu-vs-retail.png"
fi

grep -a 'retail menu steam background ready\|RETAIL_READY\|MAP_READY' "$LOG_DIR/probe.log" || true
exit 0
