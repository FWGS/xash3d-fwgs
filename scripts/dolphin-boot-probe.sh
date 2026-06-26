#!/usr/bin/env bash
set -uo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"
if [[ -f scripts/gamecube-env.sh ]]; then
	source scripts/gamecube-env.sh
fi

# shellcheck source=scripts/dolphin-probe-lock.sh
source scripts/dolphin-probe-lock.sh

ISO_PATH="$ROOT/OUT/xash3d-gc.iso"
LOG_DIR=".ai/logs/dolphin-probe-$(date +%Y%m%d-%H%M%S)"
USER_DIR="$ROOT/$LOG_DIR/dolphin-user"
TIMEOUT_SEC="${DOLPHIN_TIMEOUT:-60}"
FRAME_SAMPLE_SEC="${DOLPHIN_FRAME_SAMPLE_SEC:-8}"
SMOKE_MAP="${DOLPHIN_SMOKE_MAP:-c0a0e}"
GC_FATAL_TEST="${GC_FATAL_TEST:-0}"
GUEST_MARKER="Xash3D GameCube: bootstrap"
READY_MARKER="Xash3D GameCube: engine subsystems ready"
MAP_MARKER="Xash3D GameCube: map loaded ${SMOKE_MAP}"
INPUT_MARKER="Xash3D GameCube: input polling active"
G45_READY_MARKER="Xash3D GameCube: G45 controller ready"
G45_WAIT_MARKER="Xash3D GameCube: G45 controller waiting"
G37_FATAL_MARKER="G37: Intentional fatal error triggered"

probe_log_has() {
	local needle="$1"
	[[ -f "$LOG_DIR/stderr.log" ]] && grep -aqsF "$needle" "$LOG_DIR/stderr.log"
	[[ -f "$LOG_DIR/stdout.log" ]] && grep -aqsF "$needle" "$LOG_DIR/stdout.log"
}

probe_guest_error() {
	grep -aEiq 'Host_Error|Sys_Error|Xash Error|_Mem_Alloc: out of memory|fatal error|guest.*(crash|abort)' \
		"$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null
}

finalize_probe() {
	local status="$1"
	local exit_code="$2"
	python3 scripts/dolphin-probe-analyze.py \
		--repo "$ROOT" \
		--log-dir "$LOG_DIR" \
		--smoke-map "$SMOKE_MAP" \
		--probe-status "$status" \
		--update-state
	exit "$exit_code"
}

mkdir -p "$USER_DIR/Config"

cat > "$USER_DIR/Config/Dolphin.ini" <<'EOF'
[Core]
CPUCore = 0
CPUThread = False
DSPHLE = True
FastDiscSpeed = True
SIDevice0 = 6
SIDevice1 = 0
SIDevice2 = 0
SIDevice3 = 0
[Interface]
ConfirmStop = False
EOF

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
if ! bash scripts/build-gamecube.sh; then
    echo "FAIL: Engine build failed."
    exit 1
fi

echo "==> Building GameCube disc image..."
BUILD_ARGS=(--output "$ISO_PATH")
if [[ -n "$SMOKE_MAP" ]]; then
	BUILD_ARGS+=(--smoke-map "$SMOKE_MAP")
fi
if ! python3 scripts/build-gamecube-disc.py "${BUILD_ARGS[@]}"; then
    echo "FAIL: Disc build failed."
    exit 1
fi

DOLPHIN_CMD=()
DOLPHIN_IS_FLATPAK=0
EXTRA_ARGS=()
if (( GC_FATAL_TEST )); then
	EXTRA_ARGS+=("-gc_fatal_test" "1")
fi

if [[ "${DOLPHIN_EXECUTABLE:-}" == flatpak:* ]]; then
	DOLPHIN_FLATPAK_ID="${DOLPHIN_EXECUTABLE#flatpak:}"
	DOLPHIN_CMD=(flatpak run --filesystem="$ROOT" "$DOLPHIN_FLATPAK_ID"
		-u "$USER_DIR" -l -b -e "$ISO_PATH" -v Null "${EXTRA_ARGS[@]}")
	DOLPHIN_IS_FLATPAK=1
elif [[ -n "${DOLPHIN_EXECUTABLE:-}" ]]; then
	DOLPHIN_CMD=("$DOLPHIN_EXECUTABLE" -u "$USER_DIR" -l -b -e "$ISO_PATH" -v Null "${EXTRA_ARGS[@]}")
elif command -v dolphin-emu >/dev/null 2>&1; then
	DOLPHIN_CMD=(dolphin-emu -u "$USER_DIR" -l -b -e "$ISO_PATH" -v Null "${EXTRA_ARGS[@]}")
elif command -v dolphin >/dev/null 2>&1; then
	DOLPHIN_CMD=(dolphin -u "$USER_DIR" -l -b -e "$ISO_PATH" -v Null "${EXTRA_ARGS[@]}")
elif command -v flatpak >/dev/null 2>&1 && \
	flatpak info "${DOLPHIN_FLATPAK_ID:-org.DolphinEmu.dolphin-emu}" >/dev/null 2>&1; then
	# Dolphin's Flatpak has no home-directory access by default. Grant only this
	# repository so it can read the ISO and use the isolated probe profile.
	DOLPHIN_CMD=(flatpak run --filesystem="$ROOT" "${DOLPHIN_FLATPAK_ID:-org.DolphinEmu.dolphin-emu}"
		-u "$USER_DIR" -l -b -e "$ISO_PATH" -v Null "${EXTRA_ARGS[@]}")
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

echo "==> Launching bounded Dolphin boot probe (${TIMEOUT_SEC}s)..."
set +e
if (( DOLPHIN_IS_FLATPAK )); then
	flatpak kill "${DOLPHIN_FLATPAK_ID:-org.DolphinEmu.dolphin-emu}" >/dev/null 2>&1 || true
	trap cleanup_flatpak_dolphin EXIT
	"${DOLPHIN_CMD[@]}" >"$LOG_DIR/stdout.log" 2>"$LOG_DIR/stderr.log" &
	DOLPHIN_WRAPPER_PID=$!
	DOLPHIN_EXIT=124
	DEADLINE=$(($(date +%s) + TIMEOUT_SEC))
	MAP_READY_AT=0
	while (( $(date +%s) < DEADLINE )); do
		if probe_log_has "$MAP_MARKER" && probe_log_has "$INPUT_MARKER"; then
			if (( MAP_READY_AT == 0 )); then
				MAP_READY_AT=$(date +%s)
			fi
			if probe_guest_error; then
				DOLPHIN_EXIT=3
				break
			fi
			if (( FRAME_SAMPLE_SEC <= 0 || $(date +%s) >= MAP_READY_AT + FRAME_SAMPLE_SEC )); then
				DOLPHIN_EXIT=0
				break
			fi
		elif probe_log_has "$GUEST_MARKER" && probe_guest_error; then
			DOLPHIN_EXIT=3
			break
		fi
		sleep 2
	done
else
	timeout --signal=TERM --kill-after=5 "$TIMEOUT_SEC" "${DOLPHIN_CMD[@]}" \
		>"$LOG_DIR/stdout.log" 2>"$LOG_DIR/stderr.log"
	DOLPHIN_EXIT=$?
fi
set -e

# Flatpak's wrapper can exit while the emulator process remains in the app
# sandbox. Stop the instance launched by this bounded probe.
if (( DOLPHIN_IS_FLATPAK )); then
	cleanup_flatpak_dolphin
	trap - EXIT
	wait "$DOLPHIN_WRAPPER_PID" >/dev/null 2>&1 || true
fi

echo "==> Analyzing probe results..."
LOG_FILES=("$LOG_DIR/stdout.log" "$LOG_DIR/stderr.log")
GUEST_FOUND=0
READY_FOUND=0
MAP_FOUND=0
INPUT_FOUND=0
grep -aqsF "$GUEST_MARKER" "${LOG_FILES[@]}" && GUEST_FOUND=1
grep -aqsF "$READY_MARKER" "${LOG_FILES[@]}" && READY_FOUND=1
grep -aqsF "$INPUT_MARKER" "${LOG_FILES[@]}" && INPUT_FOUND=1
if [[ -n "$SMOKE_MAP" ]]; then
	grep -aqsF "$MAP_MARKER" "${LOG_FILES[@]}" && MAP_FOUND=1
fi

# G37: Check for intentional fatal error verification BEFORE other guest error checks.
# When GC_FATAL_TEST is set, the guest is expected to trigger Sys_Error and halt.
if (( GC_FATAL_TEST )) && probe_log_has "$G37_FATAL_MARKER" && probe_log_has "$GUEST_MARKER"; then
	echo "G37_VERIFIED: Intentional fatal error triggered and breadcrumb reported."
	echo "Logs: $LOG_DIR"
	finalize_probe g37_verified 0
fi

if (( MAP_FOUND )) && (( INPUT_FOUND )); then
	if probe_guest_error; then
		echo "GUEST_FAILURE: Map load was observed, followed by a guest error."
		echo "Logs: $LOG_DIR"
		finalize_probe guest_failure 3
	fi
	echo "MAP_READY: Xash3D loaded ${SMOKE_MAP} on GameCube with interactive input."
	if probe_log_has "$G45_READY_MARKER"; then
		grep -ahF "$G45_READY_MARKER" "${LOG_FILES[@]}" | tail -1
		echo "G45_STATUS: PASS"
	elif probe_log_has "$G45_WAIT_MARKER"; then
		echo "G45_STATUS: WAIT"
	else
		echo "G45_STATUS: WEAK"
	fi
	echo "Logs: $LOG_DIR"
	finalize_probe map_ready 0
fi

# Map loaded but input not detected. This might be a partial success for map loading
# but fails the "interactive" criteria of G19 if no controller is detected/polling.
if (( MAP_FOUND )) && ! (( INPUT_FOUND )); then
	if probe_guest_error; then
		echo "GUEST_FAILURE: Map load was observed, followed by a guest error."
		echo "Logs: $LOG_DIR"
		finalize_probe guest_failure 3
	fi
	echo "MAP_LOADED_NO_INPUT: Map ${SMOKE_MAP} loaded but input polling marker was not found."
	echo "Logs: $LOG_DIR"
	finalize_probe map_loaded_no_input 0
fi

if (( READY_FOUND )) && [[ -z "$SMOKE_MAP" ]]; then
	if probe_guest_error; then
		echo "GUEST_FAILURE: Engine readiness was observed, followed by a guest error."
		echo "Logs: $LOG_DIR"
		finalize_probe guest_failure 3
	fi
	echo "ENGINE_READY: Xash3D initialized its GameCube subsystems."
	echo "Logs: $LOG_DIR"
	finalize_probe engine_ready 0
fi

if (( GUEST_FOUND )) && probe_guest_error; then
	if (( ! GC_FATAL_TEST )); then
		echo "GUEST_FAILURE: Bootstrap was followed by a guest-engine error."
		echo "Logs: $LOG_DIR"
		finalize_probe guest_failure 3
	fi
fi

if grep -aEiq 'Unknown instruction|Invalid read from|IntCPU:|apploader.*(fail|error)' "${LOG_FILES[@]}"; then
	echo "BOOT_FAILURE: Dolphin reached the disc but the guest image failed before bootstrap."
	echo "Logs: $LOG_DIR"
	finalize_probe boot_failure 3
fi

if (( DOLPHIN_EXIT == 124 || DOLPHIN_EXIT == 137 )); then
	if [[ -n "$SMOKE_MAP" ]] && (( READY_FOUND )); then
		echo "MAP_TIMEOUT: Engine readiness was observed, but ${SMOKE_MAP} did not load within ${TIMEOUT_SEC}s."
		grep -ahF 'OSREPORT' "${LOG_FILES[@]}" | tail -1 | sed 's/^/Last guest log: /'
	elif (( GUEST_FOUND )); then
		echo "GUEST_TIMEOUT: Bootstrap was observed, but engine readiness was not reached within ${TIMEOUT_SEC}s."
		grep -ahF 'OSREPORT' "${LOG_FILES[@]}" | tail -1 | sed 's/^/Last guest log: /'
	else
		echo "INCONCLUSIVE_TIMEOUT: No guest bootstrap within ${TIMEOUT_SEC}s."
	fi
	echo "Logs: $LOG_DIR"
	finalize_probe map_timeout 4
fi

if (( DOLPHIN_EXIT != 0 )); then
	if (( GUEST_FOUND )); then
		if (( ! GC_FATAL_TEST )); then
			echo "GUEST_FAILURE: Dolphin exited $DOLPHIN_EXIT after guest bootstrap."
			echo "Logs: $LOG_DIR"
			finalize_probe guest_failure 3
		fi
	fi
	if (( ! GUEST_FOUND )); then
		echo "HOST_FAILURE: Dolphin exited $DOLPHIN_EXIT before guest bootstrap."
		echo "Logs: $LOG_DIR"
		finalize_probe host_failure 2
	fi
fi

if (( ! MAP_FOUND )) && (( ! READY_FOUND )) && (( ! GUEST_FOUND )); then
	echo "INCONCLUSIVE_EXIT: Dolphin exited $DOLPHIN_EXIT without reaching engine readiness."
	if (( GUEST_FOUND )); then
		grep -ahF 'OSREPORT' "${LOG_FILES[@]}" | tail -1 | sed 's/^/Last guest log: /'
	fi
	echo "Logs: $LOG_DIR"
	finalize_probe inconclusive_exit 4
fi
