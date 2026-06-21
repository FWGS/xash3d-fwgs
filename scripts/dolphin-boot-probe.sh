#!/usr/bin/env bash
set -uo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

ISO_PATH="OUT/xash3d-gc.iso"
LOG_DIR=".ai/logs/dolphin-probe-$(date +%Y%m%d-%H%M%S)"
TIMEOUT_SEC="${DOLPHIN_TIMEOUT:-60}"

mkdir -p "$LOG_DIR"

echo "==> Building GameCube disc image..."
if ! python3 scripts/build-gamecube-disc.py --output "$ISO_PATH"; then
    echo "FAIL: Disc build failed."
    exit 1
fi

DOLPHIN_CMD=()
DOLPHIN_LOG_DIR=""
if command -v dolphin-emu >/dev/null 2>&1; then
	DOLPHIN_CMD=(dolphin-emu --batch --exec "$ISO_PATH")
	DOLPHIN_LOG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/dolphin-emu/Logs"
elif command -v dolphin >/dev/null 2>&1; then
	DOLPHIN_CMD=(dolphin --batch --exec "$ISO_PATH")
	DOLPHIN_LOG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/dolphin-emu/Logs"
elif command -v flatpak >/dev/null 2>&1 && \
	flatpak info org.DolphinEmu.dolphin-emu >/dev/null 2>&1; then
	DOLPHIN_CMD=(flatpak run org.DolphinEmu.dolphin-emu --batch --exec "$ISO_PATH")
	DOLPHIN_LOG_DIR="$HOME/.var/app/org.DolphinEmu.dolphin-emu/config/dolphin-emu/Logs"
else
	echo "HOST_FAILURE: Dolphin executable or Flatpak was not found."
	exit 2
fi

echo "==> Launching bounded Dolphin boot probe (${TIMEOUT_SEC}s)..."
set +e
timeout --signal=TERM --kill-after=5 "$TIMEOUT_SEC" "${DOLPHIN_CMD[@]}" \
	>"$LOG_DIR/stdout.log" 2>"$LOG_DIR/stderr.log"
DOLPHIN_EXIT=$?
set -e

# Copy Dolphin's internal logs if available
if [[ -d "$DOLPHIN_LOG_DIR" ]]; then
	cp "$DOLPHIN_LOG_DIR"/*.log "$LOG_DIR/" 2>/dev/null || true
fi

echo "==> Analyzing probe results..."
GUEST_MARKER="Xash3D GameCube: bootstrap"
GUEST_FOUND=0
grep -rqsF "$GUEST_MARKER" "$LOG_DIR/" && GUEST_FOUND=1

if (( GUEST_FOUND )); then
	if grep -rEiq 'Host_Error|Sys_Error|fatal error|guest.*(crash|abort)' "$LOG_DIR/"; then
		echo "GUEST_FAILURE: Bootstrap was observed, followed by a guest-engine error."
		echo "Logs: $LOG_DIR"
		exit 3
	fi
	echo "GUEST_REACHED: Xash3D bootstrap was observed."
	echo "Logs: $LOG_DIR"
	exit 0
elif (( DOLPHIN_EXIT == 124 || DOLPHIN_EXIT == 137 )); then
	echo "INCONCLUSIVE_TIMEOUT: No guest bootstrap within ${TIMEOUT_SEC}s."
	echo "Logs: $LOG_DIR"
	exit 4
elif (( DOLPHIN_EXIT != 0 )); then
	echo "HOST_FAILURE: Dolphin exited $DOLPHIN_EXIT before guest bootstrap."
	echo "Logs: $LOG_DIR"
	exit 2
else
	echo "INCONCLUSIVE_EXIT: Dolphin exited cleanly without guest bootstrap."
	echo "Logs: $LOG_DIR"
	exit 4
fi
