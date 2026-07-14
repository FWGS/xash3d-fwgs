#!/usr/bin/env bash

# Whole-script timeout wrapper.
# The Dolphin process itself may have its own timeout, but the shell probe can
# still hang around locks, log tails, analyzers, or escaped child processes.
if [ "${GC_BOOT_PROBE_INNER:-0}" != "1" ]; then
    PROBE_TIMEOUT="${GC_BOOT_PROBE_TIMEOUT:-240}"
    export GC_BOOT_PROBE_INNER=1
    exec timeout --foreground --signal=TERM --kill-after=10 "$PROBE_TIMEOUT" "$0" "$@"
fi

cleanup_boot_probe_processes() {
    # Only clean the probe/emulator family for this repo.
    pkill -TERM -f 'dolphin-emu|DolphinQt|xash3d-gc.iso' 2>/dev/null || true
    sleep 1
    pkill -KILL -f 'dolphin-emu|DolphinQt|xash3d-gc.iso' 2>/dev/null || true
}

cleanup_boot_probe_locks() {
    rm -f .ai/dolphin-probe.lock .ai/goal-supervisor.lock .ai/goal-loop.lock 2>/dev/null || true
}

on_boot_probe_exit() {
    rc=$?
    cleanup_boot_probe_processes
    cleanup_boot_probe_locks
    exit "$rc"
}

trap on_boot_probe_exit EXIT INT TERM

set -uo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"
if [[ -f scripts/gamecube-env.sh ]]; then
	source scripts/gamecube-env.sh
fi

# shellcheck source=scripts/dolphin-probe-lock.sh
source scripts/dolphin-probe-lock.sh || exit $?
# shellcheck source=scripts/dolphin-probe-common.sh
source scripts/dolphin-probe-common.sh || exit $?

LOG_DIR=".ai/logs/dolphin-probe-$(date +%Y%m%d-%H%M%S)"
ISO_PATH="$ROOT/$LOG_DIR/xash3d-gc.iso"
PREBUILT_ISO="${1:-${DOLPHIN_PREBUILT_ISO:-}}"
if [[ -n "$PREBUILT_ISO" && "$PREBUILT_ISO" != /* ]]; then
	PREBUILT_ISO="$ROOT/$PREBUILT_ISO"
fi
USER_DIR="$ROOT/$LOG_DIR/dolphin-user"
DOLPHIN_RETAIL="${DOLPHIN_RETAIL:-0}"
DOLPHIN_NEWGAME="${DOLPHIN_NEWGAME:-0}"
if (( DOLPHIN_NEWGAME )); then
	# Retail New Game probes need full valve assets and gamecube.cfg newgame override.
	DOLPHIN_RETAIL=1
	DOLPHIN_SKIP_INTRO=1
fi
if [[ "$DOLPHIN_RETAIL" == "1" ]]; then
	TIMEOUT_SEC="${DOLPHIN_TIMEOUT:-240}"
elif (( DOLPHIN_NEWGAME )); then
	TIMEOUT_SEC="${DOLPHIN_TIMEOUT:-240}"
else
	# Entity spawn on large smoke maps (c1a0) needs headroom after BSP load.
	TIMEOUT_SEC="${DOLPHIN_TIMEOUT:-180}"
fi
FRAME_SAMPLE_SEC="${DOLPHIN_FRAME_SAMPLE_SEC:-8}"
if (( DOLPHIN_NEWGAME )); then
	SMOKE_MAP="${DOLPHIN_SMOKE_MAP:-c0a0}"
	FRAME_SAMPLE_SEC="${DOLPHIN_FRAME_SAMPLE_SEC:-16}"
else
	SMOKE_MAP="${DOLPHIN_SMOKE_MAP:-c0a0e}"
fi
DOLPHIN_WORLD_RENDER="${DOLPHIN_WORLD_RENDER:-0}"
GC_FATAL_TEST="${GC_FATAL_TEST:-0}"
GUEST_MARKER="Xash3D GameCube: bootstrap"
READY_MARKER="Xash3D GameCube: engine subsystems ready"
RETAIL_MENU_MARKER="Xash3D GameCube: retail menu steam background ready"
RETAIL_MENU_INTERACTIVE_MARKER="Xash3D GameCube: retail menu button text ready"
INTRO_MARKER="Xash3D GameCube: intro AVI decoded first frame"
MAP_MARKER="Xash3D GameCube: map loaded ${SMOKE_MAP}"
PLAY_READY_MARKER="Xash3D GameCube: play start ready ${SMOKE_MAP}"
FRAME_ARMED_MARKER="Xash3D GameCube: frame budget samples armed after map ready"
INPUT_MARKER="Xash3D GameCube: input polling active"
G45_READY_MARKER="Xash3D GameCube: G45 controller ready"
G45_WAIT_MARKER="Xash3D GameCube: G45 controller waiting"
G37_FATAL_MARKER="G37: Intentional fatal error triggered"
DOLPHIN_MMU="${DOLPHIN_MMU:-True}"

mkdir -p "$USER_DIR/Config"

cat > "$USER_DIR/Config/Dolphin.ini" <<EOF
[Core]
CPUCore = 0
CPUThread = False
DSPHLE = True
FastDiscSpeed = True
MMU = ${DOLPHIN_MMU}
AccurateCPUCache = ${DOLPHIN_MMU}
SIDevice0 = 6
SIDevice1 = 0
SIDevice2 = 0
SIDevice3 = 0
[Interface]
ConfirmStop = False
EOF

if [[ "$DOLPHIN_RETAIL" == "1" ]]; then
	cat > "$USER_DIR/Config/GFX.ini" <<'EOF'
[Settings]
DumpFrames = True
DumpFramesSilent = True
DumpFramesAsImages = True
EOF
fi

cat > "$USER_DIR/Config/Logger.ini" <<'EOF'
[Logs]
BOOT = True
CORE = True
DVD = False
OSREPORT = True
OSREPORT_HLE = True
PowerPC = False
[Options]
Verbosity = 4
WriteToConsole = True
WriteToFile = True
WriteToWindow = False
EOF

echo "==> Building GameCube engine and DOL..."
SKIP_ENGINE=0
SKIP_DISC=0
if [[ "${DOLPHIN_SKIP_BUILD:-0}" == "1" && -f "$PREBUILT_ISO" ]]; then
	SKIP_ENGINE=1
	# New Game needs valve/gamecube.cfg rewritten with `newgame` — never reuse ISO.
	# Retail menu validation may reuse OUT/xash3d-gc.iso; smoke maps must rebuild.
	if (( DOLPHIN_NEWGAME )); then
		SKIP_DISC=0
	elif [[ "$DOLPHIN_RETAIL" == "1" ]] || [[ "${DOLPHIN_REUSE_ISO:-0}" == "1" ]]; then
		SKIP_DISC=1
	fi
fi

if (( SKIP_DISC )); then
	echo "==> Using pre-built ISO (DOLPHIN_SKIP_BUILD=1): $PREBUILT_ISO"
	mkdir -p "$(dirname "$ISO_PATH")"
	cp -f "$PREBUILT_ISO" "$ISO_PATH"
else
	if (( SKIP_ENGINE )); then
		echo "==> Skipping engine rebuild (DOLPHIN_SKIP_BUILD=1); rebuilding disc image..."
	else
		if ! bash scripts/build-gamecube.sh; then
			echo "FAIL: Engine build failed."
			exit 1
		fi
	fi

	echo "==> Building GameCube disc image..."
	BUILD_ARGS=(--output "$ISO_PATH")
	if [[ "$DOLPHIN_RETAIL" == "1" ]]; then
		SMOKE_MAP=""
		BUILD_ARGS+=(--data Half-Life/valve)
		echo "==> Retail disc mode (full valve assets, no smoke map)"
		if [[ "${DOLPHIN_SKIP_INTRO:-0}" == "1" ]]; then
			BUILD_ARGS+=(--skip-startup-vids)
			echo "==> Skipping startup cinematic for faster menu validation"
		fi
		if (( DOLPHIN_NEWGAME )); then
			BUILD_ARGS+=(--probe-newgame)
		fi
	elif [[ -n "$SMOKE_MAP" ]]; then
		BUILD_ARGS+=(--smoke-map "$SMOKE_MAP")
		if (( DOLPHIN_WORLD_RENDER )); then
			BUILD_ARGS+=(--world-render)
			echo "==> World render probe mode (gcworldrender in gamecube.cfg)"
		fi
	fi
	if ! python3 scripts/build-gamecube-disc.py "${BUILD_ARGS[@]}"; then
		echo "FAIL: Disc build failed."
		exit 1
	fi
fi

DOLPHIN_CMD=()
DOLPHIN_IS_FLATPAK=0
GUEST_ARGS=()
if (( GC_FATAL_TEST )); then
	GUEST_ARGS+=("-gc_fatal_test" "1")
fi
if (( DOLPHIN_NEWGAME )); then
	GUEST_ARGS+=("-gcnewgame")
	SMOKE_MAP="${DOLPHIN_SMOKE_MAP:-c0a0}"
	MAP_MARKER="Xash3D GameCube: map loaded ${SMOKE_MAP}"
	echo "==> New Game probe mode (expect map ${SMOKE_MAP})"
fi
append_guest_args() {
	local -n _cmd="$1"
	if ((${#GUEST_ARGS[@]})); then
		_cmd+=(-- "${GUEST_ARGS[@]}")
	fi
}

DOLPHIN_VIDEO_BACKEND="${DOLPHIN_VIDEO:-Null}"
DOLPHIN_BATCH_MODE=1
if [[ "$DOLPHIN_RETAIL" == "1" ]]; then
	DOLPHIN_VIDEO_BACKEND="${DOLPHIN_VIDEO:-OpenGL}"
	if [[ "${DOLPHIN_CAPTURE_MENU:-0}" == "1" ]]; then
		DOLPHIN_BATCH_MODE=0
		TIMEOUT_SEC="${DOLPHIN_TIMEOUT:-120}"
	fi
fi
DOLPHIN_MODE_ARGS=()
if (( DOLPHIN_BATCH_MODE )); then
	DOLPHIN_MODE_ARGS=(-l -b)
else
	DOLPHIN_MODE_ARGS=(-l)
fi

if [[ "${DOLPHIN_EXECUTABLE:-}" == flatpak:* ]]; then
	DOLPHIN_FLATPAK_ID="${DOLPHIN_EXECUTABLE#flatpak:}"
	DOLPHIN_CMD=(flatpak run --filesystem="$ROOT" "$DOLPHIN_FLATPAK_ID"
		-u "$USER_DIR" "${DOLPHIN_MODE_ARGS[@]}" -e "$ISO_PATH" -v "$DOLPHIN_VIDEO_BACKEND")
	append_guest_args DOLPHIN_CMD
	DOLPHIN_IS_FLATPAK=1
elif [[ -n "${DOLPHIN_EXECUTABLE:-}" ]]; then
	DOLPHIN_CMD=("$DOLPHIN_EXECUTABLE" -u "$USER_DIR" "${DOLPHIN_MODE_ARGS[@]}" -e "$ISO_PATH" -v "$DOLPHIN_VIDEO_BACKEND")
	append_guest_args DOLPHIN_CMD
elif command -v dolphin-emu >/dev/null 2>&1; then
	DOLPHIN_CMD=(dolphin-emu -u "$USER_DIR" "${DOLPHIN_MODE_ARGS[@]}" -e "$ISO_PATH" -v "$DOLPHIN_VIDEO_BACKEND")
	append_guest_args DOLPHIN_CMD
elif command -v dolphin >/dev/null 2>&1; then
	DOLPHIN_CMD=(dolphin -u "$USER_DIR" "${DOLPHIN_MODE_ARGS[@]}" -e "$ISO_PATH" -v "$DOLPHIN_VIDEO_BACKEND")
	append_guest_args DOLPHIN_CMD
elif command -v flatpak >/dev/null 2>&1 && \
	flatpak info "${DOLPHIN_FLATPAK_ID:-org.DolphinEmu.dolphin-emu}" >/dev/null 2>&1; then
	DOLPHIN_CMD=(flatpak run --filesystem="$ROOT" "${DOLPHIN_FLATPAK_ID:-org.DolphinEmu.dolphin-emu}"
		-u "$USER_DIR" "${DOLPHIN_MODE_ARGS[@]}" -e "$ISO_PATH" -v "$DOLPHIN_VIDEO_BACKEND")
	append_guest_args DOLPHIN_CMD
	DOLPHIN_IS_FLATPAK=1
else
	echo "HOST_FAILURE: Dolphin executable or Flatpak was not found."
	exit 2
fi

cleanup_flatpak_dolphin() {
	if (( DOLPHIN_IS_FLATPAK )); then
		flatpak kill "${DOLPHIN_FLATPAK_ID:-org.DolphinEmu.dolphin-emu}" >/dev/null 2>&1 || true
		pkill -TERM -f "dolphin.*${USER_DIR}" >/dev/null 2>&1 || true
		sleep 1
		pkill -KILL -f "dolphin.*${USER_DIR}" >/dev/null 2>&1 || true
	fi
}

echo "==> Launching bounded Dolphin boot probe (${TIMEOUT_SEC}s, MMU=${DOLPHIN_MMU})..."
set +e
if (( DOLPHIN_IS_FLATPAK )); then
	probe_wait_flatpak
else
	probe_wait_native
fi
set -e

sleep 2
if (( DOLPHIN_IS_FLATPAK )); then
	cleanup_flatpak_dolphin
	trap - EXIT
	wait "$DOLPHIN_WRAPPER_PID" >/dev/null 2>&1 || true
fi

echo "==> Analyzing probe results..."
LOG_FILES=("$LOG_DIR/stdout.log" "$LOG_DIR/stderr.log")
GUEST_FOUND=0 READY_FOUND=0 MAP_FOUND=0 INPUT_FOUND=0
grep -aqsF "$GUEST_MARKER" "${LOG_FILES[@]}" && GUEST_FOUND=1
grep -aqsF "$READY_MARKER" "${LOG_FILES[@]}" && READY_FOUND=1
grep -aqsF "$INPUT_MARKER" "${LOG_FILES[@]}" && INPUT_FOUND=1
if [[ -n "$SMOKE_MAP" ]]; then
	grep -aqsF "$MAP_MARKER" "${LOG_FILES[@]}" && MAP_FOUND=1
fi

if (( GC_FATAL_TEST )) && probe_log_has "$G37_FATAL_MARKER" && probe_log_has "$GUEST_MARKER"; then
	echo "G37_VERIFIED: Intentional fatal error triggered and breadcrumb reported."
	echo "Logs: $LOG_DIR"
	finalize_probe g37_verified 0
fi

if [[ "$DOLPHIN_RETAIL" == "1" ]] && (( READY_FOUND )) && (( ! DOLPHIN_NEWGAME )) && \
	( probe_log_has "$RETAIL_MENU_INTERACTIVE_MARKER" || probe_log_has "$RETAIL_MENU_MARKER" ); then
	probe_guest_error && probe_fail_guest guest_failure "GUEST_FAILURE: Retail boot reached menu, followed by a guest error."
	if probe_log_has "$INTRO_MARKER"; then
		echo "RETAIL_READY: Half-Life retail boot played intro AVI and reached the interactive menu on GameCube."
	else
		echo "RETAIL_READY: Half-Life retail boot reached the interactive menu on GameCube (intro AVI marker not seen)."
	fi
	probe_report_g45
	echo "Logs: $LOG_DIR"
	finalize_probe retail_ready 0
fi

if (( MAP_FOUND )) && (( INPUT_FOUND )); then
	probe_guest_error && probe_fail_guest guest_failure "GUEST_FAILURE: Map load was observed, followed by a guest error."
	echo "MAP_READY: Xash3D loaded ${SMOKE_MAP} on GameCube with interactive input."
	probe_report_g45
	echo "Logs: $LOG_DIR"
	finalize_probe map_ready 0
fi

if (( MAP_FOUND )) && ! (( INPUT_FOUND )); then
	probe_guest_error && probe_fail_guest guest_failure "GUEST_FAILURE: Map load was observed, followed by a guest error."
	echo "MAP_LOADED_NO_INPUT: Map ${SMOKE_MAP} loaded but input polling marker was not found."
	echo "Logs: $LOG_DIR"
	finalize_probe map_loaded_no_input 0
fi

if (( READY_FOUND )) && [[ -z "$SMOKE_MAP" ]] && [[ "$DOLPHIN_RETAIL" != "1" ]]; then
	probe_guest_error && probe_fail_guest guest_failure "GUEST_FAILURE: Engine readiness was observed, followed by a guest error."
	echo "ENGINE_READY: Xash3D initialized its GameCube subsystems."
	echo "Logs: $LOG_DIR"
	finalize_probe engine_ready 0
fi

if (( GUEST_FOUND )) && probe_guest_error && (( ! GC_FATAL_TEST )); then
	probe_fail_guest guest_failure "GUEST_FAILURE: Bootstrap was followed by a guest-engine error."
fi

if grep -aEiq 'Unknown instruction|Invalid read from|IntCPU:|apploader.*(fail|error)' "${LOG_FILES[@]}"; then
	probe_fail_guest boot_failure "BOOT_FAILURE: Dolphin reached the disc but the guest image failed before bootstrap."
fi

if (( DOLPHIN_EXIT == 124 || DOLPHIN_EXIT == 137 )); then
	if [[ -n "$SMOKE_MAP" ]] && (( READY_FOUND )); then
		echo "MAP_TIMEOUT: Engine readiness was observed, but ${SMOKE_MAP} did not load within ${TIMEOUT_SEC}s."
	elif (( GUEST_FOUND )); then
		echo "GUEST_TIMEOUT: Bootstrap was observed, but engine readiness was not reached within ${TIMEOUT_SEC}s."
	else
		echo "INCONCLUSIVE_TIMEOUT: No guest bootstrap within ${TIMEOUT_SEC}s."
	fi
	grep -ahF 'OSREPORT' "${LOG_FILES[@]}" | tail -1 | sed 's/^/Last guest log: /'
	echo "Logs: $LOG_DIR"
	finalize_probe map_timeout 4
fi

if (( DOLPHIN_EXIT != 0 )); then
	if (( GUEST_FOUND )) && (( ! GC_FATAL_TEST )); then
		probe_fail_guest guest_failure "GUEST_FAILURE: Dolphin exited $DOLPHIN_EXIT after guest bootstrap."
	fi
	if (( ! GUEST_FOUND )); then
		probe_fail_guest host_failure "HOST_FAILURE: Dolphin exited $DOLPHIN_EXIT before guest bootstrap."
	fi
fi

if (( ! MAP_FOUND )) && (( ! READY_FOUND )) && (( ! GUEST_FOUND )); then
	echo "INCONCLUSIVE_EXIT: Dolphin exited $DOLPHIN_EXIT without reaching engine readiness."
	(( GUEST_FOUND )) && grep -ahF 'OSREPORT' "${LOG_FILES[@]}" | tail -1 | sed 's/^/Last guest log: /'
	echo "Logs: $LOG_DIR"
	finalize_probe inconclusive_exit 4
fi
