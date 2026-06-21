#!/usr/bin/env bash
# dolphin-boot-probe.sh - Build a GameCube disc and run a bounded Dolphin boot probe.
# Captures logs and distinguishes host vs guest failures.
set -uo pipefail

ISO_PATH="OUT/xash3d-gc.iso"
LOG_DIR=".ai/logs/dolphin-probe-$(date +%Y%m%d-%H%M%S)"
TIMEOUT_SEC="${DOLPHIN_TIMEOUT:-60}"

mkdir -p "$LOG_DIR"

echo "==> Building GameCube disc image..."
if ! python3 scripts/build-gamecube-disc.py --output "$ISO_PATH"; then
    echo "FAIL: Disc build failed."
    exit 1
fi

DOLPHIN_CMD=""
if command -v dolphin-emu &>/dev/null; then
    DOLPHIN_CMD="dolphin-emu"
elif command -v dolphin &>/dev/null; then
    DOLPHIN_CMD="dolphin"
else
    echo "FAIL: Dolphin emulator not found in PATH."
    exit 1
fi

echo "==> Launching bounded Dolphin boot probe (${TIMEOUT_SEC}s)..."
timeout "$TIMEOUT_SEC" "$DOLPHIN_CMD" --batch --exec "$ISO_PATH" \
    > "$LOG_DIR/stdout.log" 2> "$LOG_DIR/stderr.log"
DOLPHIN_EXIT=$?

# Copy Dolphin's internal logs if available
DOLPHIN_LOG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/Dolphin/Log"
if [ -d "$DOLPHIN_LOG_DIR" ]; then
    cp "$DOLPHIN_LOG_DIR"/*.log "$LOG_DIR/" 2>/dev/null || true
fi

echo "==> Analyzing probe results..."
GUEST_MARKER="Xash3D GameCube: bootstrap"
GUEST_FOUND=false
if grep -rq "$GUEST_MARKER" "$LOG_DIR/" 2>/dev/null; then
    GUEST_FOUND=true
fi

if [ "$GUEST_FOUND" = true ]; then
    echo "PASS: Guest-engine reached bootstrap. OSReport output captured."
    if grep -rq "Warning\|Error\|Fatal\|crash\|abort" "$LOG_DIR/" 2>/dev/null; then
        echo "NOTE: Guest reported warnings/errors. Inspect $LOG_DIR for details."
    fi
elif [ $DOLPHIN_EXIT -eq 124 ]; then
    echo "TIMEOUT: Probe exceeded ${TIMEOUT_SEC}s. Guest may be running or hung."
elif [ $DOLPHIN_EXIT -ne 0 ]; then
    echo "FAIL: Emulator-host failure. Dolphin exited with code $DOLPHIN_EXIT."
    echo "       Check $LOG_DIR/stderr.log for host crashes or missing dependencies."
else
    echo "INCONCLUSIVE: Dolphin exited cleanly but guest bootstrap was not observed."
fi

echo "==> Logs preserved at $LOG_DIR"
exit 0
