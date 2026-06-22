#!/usr/bin/env bash
set -uo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

ISO_PATH="$ROOT/OUT/xash3d-gc.iso"
LOG_DIR=".ai/logs/dolphin-probe-$(date +%Y%m%d-%H%M%S)"
USER_DIR="$ROOT/$LOG_DIR/dolphin-user"
TIMEOUT_SEC="${DOLPHIN_TIMEOUT:-60}"

mkdir -p "$USER_DIR/Config"

cat > "$USER_DIR/Config/Dolphin.ini" <<'EOF'
[Core]
CPUCore = 0
CPUThread = False
DSPHLE = True
FastDiscSpeed = True
[Interface]
ConfirmStop = False
EOF

cat > "$USER_DIR/Config/Logger.ini" <<'EOF'
[Logs]
BOOT = True
CORE = True
DVD = True
OSREPORT = True
OSREPORT_HLE = True
PowerPC = True
[Options]
Verbosity = 4
WriteToConsole = True
WriteToFile = True
WriteToWindow = False
EOF

echo "==> Building GameCube disc image..."
if ! python3 scripts/build-gamecube-disc.py --output "$ISO_PATH"; then
    echo "FAIL: Disc build failed."
    exit 1
fi

DOLPHIN_CMD=()
DOLPHIN_IS_FLATPAK=0
if command -v dolphin-emu >/dev/null 2>&1; then
	DOLPHIN_CMD=(dolphin-emu -u "$USER_DIR" -l -b -e "$ISO_PATH" -v Null)
elif command -v dolphin >/dev/null 2>&1; then
	DOLPHIN_CMD=(dolphin -u "$USER_DIR" -l -b -e "$ISO_PATH" -v Null)
elif command -v flatpak >/dev/null 2>&1 && \
	flatpak info org.DolphinEmu.dolphin-emu >/dev/null 2>&1; then
	# Dolphin's Flatpak has no home-directory access by default. Grant only this
	# repository so it can read the ISO and use the isolated probe profile.
	DOLPHIN_CMD=(flatpak run --filesystem="$ROOT" org.DolphinEmu.dolphin-emu
		-u "$USER_DIR" -l -b -e "$ISO_PATH" -v Null)
	DOLPHIN_IS_FLATPAK=1
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

# Flatpak's wrapper can exit while the emulator process remains in the app
# sandbox. Stop the instance launched by this bounded probe.
if (( DOLPHIN_IS_FLATPAK )); then
	flatpak kill org.DolphinEmu.dolphin-emu >/dev/null 2>&1 || true
fi

echo "==> Analyzing probe results..."
GUEST_MARKER="Xash3D GameCube: bootstrap"
READY_MARKER="Xash3D GameCube: engine subsystems ready"
GUEST_FOUND=0
READY_FOUND=0
grep -rqsF "$GUEST_MARKER" "$LOG_DIR/" && GUEST_FOUND=1
grep -rqsF "$READY_MARKER" "$LOG_DIR/" && READY_FOUND=1

if (( READY_FOUND )); then
	if grep -rEiq 'Host_Error|Sys_Error|fatal error|guest.*(crash|abort)' "$LOG_DIR/"; then
		echo "GUEST_FAILURE: Engine readiness was observed, followed by a guest error."
		echo "Logs: $LOG_DIR"
		exit 3
	fi
	echo "ENGINE_READY: Xash3D initialized its GameCube subsystems."
	echo "Logs: $LOG_DIR"
	exit 0
elif (( GUEST_FOUND )) && grep -rEiq 'Host_Error|Sys_Error|Xash Error:|fatal error|out of memory' "$LOG_DIR/"; then
	echo "GUEST_FAILURE: Bootstrap was followed by a guest-engine error."
	echo "Logs: $LOG_DIR"
	exit 3
elif grep -rEiq 'Unknown instruction|Invalid read from|IntCPU:|apploader.*(fail|error)' "$LOG_DIR/"; then
	echo "BOOT_FAILURE: Dolphin reached the disc but the guest image failed before bootstrap."
	echo "Logs: $LOG_DIR"
	exit 3
elif (( DOLPHIN_EXIT == 124 || DOLPHIN_EXIT == 137 )); then
	echo "INCONCLUSIVE_TIMEOUT: No guest bootstrap within ${TIMEOUT_SEC}s."
	echo "Logs: $LOG_DIR"
	exit 4
elif (( DOLPHIN_EXIT != 0 )); then
	echo "HOST_FAILURE: Dolphin exited $DOLPHIN_EXIT before guest bootstrap."
	echo "Logs: $LOG_DIR"
	exit 2
else
	echo "INCONCLUSIVE_EXIT: Dolphin exited without reaching engine readiness."
	echo "Logs: $LOG_DIR"
	exit 4
fi
