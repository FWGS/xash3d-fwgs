#!/usr/bin/env bash
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

# Source environment setup
if [[ -f scripts/gamecube-env.sh ]]; then
	source scripts/gamecube-env.sh
fi

# Verify critical dependencies exist
if [[ ! -f scripts/build-gamecube-disc.py ]]; then
	echo "FAIL: scripts/build-gamecube-disc.py not found."
	exit 1
fi

if [[ ! -x scripts/build-gamecube-disc.py ]]; then
	chmod +x scripts/build-gamecube-disc.py || true
fi

if command -v flock >/dev/null 2>&1; then
	mkdir -p "$ROOT/.ai"
	# Remove stale lock files older than 30s to allow retries after crashes
	if [[ -f "$ROOT/.ai/dolphin-probe.lock" ]]; then
		if (( $(find "$ROOT/.ai/dolphin-probe.lock" -mmin +0.5 -print -quit | wc -l) )); then
			rm -f "$ROOT/.ai/dolphin-probe.lock"
		fi
	fi
	exec 9>"$ROOT/.ai/dolphin-probe.lock"
	if ! flock -n 9; then
		echo "HOST_FAILURE: another Dolphin boot probe is already running."
		exit 2
	fi
fi

ISO_PATH="$ROOT/OUT/xash3d-gc.iso"
LOG_DIR=".ai/logs/dolphin-probe-$(date +%Y%m%d-%H%M%S)"
USER_DIR="$ROOT/$LOG_DIR/dolphin-user"
TIMEOUT_SEC="${DOLPHIN_TIMEOUT:-180}"
SMOKE_MAP="${DOLPHIN_SMOKE_MAP:-c0a0e}"
GUEST_MARKER="Xash3D GameCube: bootstrap"
READY_MARKER="Xash3D GameCube: engine subsystems ready"
MAP_MARKER="Xash3D GameCube: map loaded ${SMOKE_MAP}"
INPUT_MARKER="Xash3D GameCube: input polling active"
GUEST_RENDERER=""
GX_DRAWDONE_COUNT=0

probe_log_has() {
	local needle="$1"
	[[ -f "$LOG_DIR/stderr.log" ]] && grep -aqsF "$needle" "$LOG_DIR/stderr.log"
	[[ -f "$LOG_DIR/stdout.log" ]] && grep -aqsF "$needle" "$LOG_DIR/stdout.log"
}

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

echo "==> Building GameCube disc image..."
BUILD_ARGS=(--output "$ISO_PATH")
if [[ -n "$SMOKE_MAP" ]]; then
	BUILD_ARGS+=(--smoke-map "$SMOKE_MAP")
fi

# Check for DOL file
if [[ ! -f "OUT/bin/boot.dol" ]]; then
	echo "FAIL: DOL file not found at OUT/bin/boot.dol. Build the GameCube binary first."
	exit 1
fi

# Check for data directory
if [[ ! -d "Half-Life/valve" ]] && [[ -n "${SMOKE_MAP:-}" ]]; then
	# For smoke tests, try to find any valve directory
	if ! ls -d */valve >/dev/null 2>&1; then
		echo "FAIL: No Half-Life valve directory found. Smoke test requires game data."
		exit 1
	fi
fi

if ! python3 scripts/build-gamecube-disc.py "${BUILD_ARGS[@]}"; then
    echo "FAIL: Disc build failed. Check build output above for details."
    exit 1
fi

# Verify ISO was created
if [[ ! -f "$ISO_PATH" ]]; then
	echo "FAIL: ISO file was not created at $ISO_PATH"
	exit 1
fi

echo "==> Disc image built successfully: $ISO_PATH"

DOLPHIN_CMD=()
DOLPHIN_IS_FLATPAK=0
if [[ "${DOLPHIN_EXECUTABLE:-}" == flatpak:* ]]; then
	DOLPHIN_FLATPAK_ID="${DOLPHIN_EXECUTABLE#flatpak:}"
	DOLPHIN_CMD=(flatpak run --filesystem="$ROOT" "$DOLPHIN_FLATPAK_ID"
		-u "$USER_DIR" -l -b -e "$ISO_PATH" -v Null)
	DOLPHIN_IS_FLATPAK=1
elif [[ -n "${DOLPHIN_EXECUTABLE:-}" ]]; then
	DOLPHIN_CMD=("$DOLPHIN_EXECUTABLE" -u "$USER_DIR" -l -b -e "$ISO_PATH" -v Null)
elif command -v dolphin-emu >/dev/null 2>&1; then
	DOLPHIN_CMD=(dolphin-emu -u "$USER_DIR" -l -b -e "$ISO_PATH" -v Null)
elif command -v dolphin >/dev/null 2>&1; then
	DOLPHIN_CMD=(dolphin -u "$USER_DIR" -l -b -e "$ISO_PATH" -v Null)
elif command -v flatpak >/dev/null 2>&1 && \
	flatpak info "${DOLPHIN_FLATPAK_ID:-org.DolphinEmu.dolphin-emu}" >/dev/null 2>&1; then
	# Dolphin's Flatpak has no home-directory access by default. Grant only this
	# repository so it can read the ISO and use the isolated probe profile.
	DOLPHIN_CMD=(flatpak run --filesystem="$ROOT" "${DOLPHIN_FLATPAK_ID:-org.DolphinEmu.dolphin-emu}"
		-u "$USER_DIR" -l -b -e "$ISO_PATH" -v Null)
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

wait_for_probe_wrapper() {
	local pid="$1"
	local deadline
	deadline=$(($(date +%s) + 5))
	while kill -0 "$pid" >/dev/null 2>&1; do
		if (( $(date +%s) >= deadline )); then
			kill -TERM "$pid" >/dev/null 2>&1 || true
			sleep 1
			kill -KILL "$pid" >/dev/null 2>&1 || true
			break
		fi
		sleep 0.2
	done
	wait "$pid" >/dev/null 2>&1 || true
}

DOLPHIN_EXIT=1
echo "==> Launching bounded Dolphin boot probe (${TIMEOUT_SEC}s)..."
START_TS=$(date +%s)
set +e
if (( DOLPHIN_IS_FLATPAK )); then
	flatpak kill "${DOLPHIN_FLATPAK_ID:-org.DolphinEmu.dolphin-emu}" >/dev/null 2>&1 || true
	trap cleanup_flatpak_dolphin EXIT
	"${DOLPHIN_CMD[@]}" >"$LOG_DIR/stdout.log" 2>"$LOG_DIR/stderr.log" &
	DOLPHIN_WRAPPER_PID=$!
	DOLPHIN_EXIT=124
	DEADLINE=$(($(date +%s) + TIMEOUT_SEC))
	while (( $(date +%s) < DEADLINE )); do
		if probe_log_has "$MAP_MARKER" && probe_log_has "$INPUT_MARKER"; then
			DOLPHIN_EXIT=0
			break
		fi
		if probe_log_has "$GUEST_MARKER" && \
			grep -aEiq 'Host_ErrorInit|Host_Error:|Sys_Error:|fatal error|out of memory' \
				"$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null; then
			DOLPHIN_EXIT=3
			break
		fi
		# G36_PATCH_v16: Detect renderer initialization during active probe to
		# diagnose early failures before full timeout expires. Captures backend
		# name if present for correlation with frame budget measurements.
		if [[ -z "$GUEST_RENDERER" ]] && \
		   grep -aqsF "Xash3D GameCube: renderer initialized" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null; then
			__RENDERER=$(grep -aoE 'Xash3D GameCube: renderer initialized +[a-zA-Z_-]+' "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null | tail -1 | grep -oE '[a-zA-Z_-]+$' || true)
			if [[ -n "$__RENDERER" ]]; then
				GUEST_RENDERER="$__RENDERER"
			fi
			# Set detection timestamp for downstream budget verification (v63)
			G36_RENDERER_DETECTED_TS=$(date +%s)
			echo "G36_RENDERER_DETECTED: ${GUEST_RENDERER:-unknown} during probe loop at t=$(( G36_RENDERER_DETECTED_TS - START_TS ))s"
		fi

		# G36_PATCH_v45: Detect guest stuck in initialization by checking for
		# bootstrap marker without subsequent renderer or subsystem-ready markers.
		# This distinguishes "guest hung during init" from "guest running but silent"
		# after extended probe duration, providing actionable evidence for failure mode.
		if [[ -z "${G36_INIT_STUCK_CHECKED:-}" ]] && \
		   (( $(date +%s) - START_TS > 30 )); then
			G36_INIT_STUCK_CHECKED=1
			if probe_log_has "$GUEST_MARKER" && \
			   ! probe_log_has "$READY_MARKER" && \
			   [[ -z "$GUEST_RENDERER" ]]; then
				echo "G36_INIT_STUCK: Guest bootstrapped but no subsystem-ready or renderer marker after 30s. Initialization path is likely blocked."
				echo "G36_INIT_STUCK_HINT: Check for asset load hangs, DLL initialization failures, or blocking syscalls before renderer setup."
			fi
		fi


		# G36_PATCH_v63: After renderer is detected, verify budget markers appear
		# within 10 seconds. If not, emit explicit evidence that renderer is active
		# but budget telemetry is missing, providing earlier diagnostic closure.
		if [[ -n "${G36_RENDERER_DETECTED_TS:-}" ]] && [[ -z "${G36_BUDGET_VERIFY_CHECKED:-}" ]]; then
			if (( $(date +%s) - G36_RENDERER_DETECTED_TS > 10 )); then
				G36_BUDGET_VERIFY_CHECKED=1
				if ! grep -aqsE "Xash3D GameCube:.*frame.*time=" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null; then
					echo "G36_BUDGET_MISSING_POST_INIT: Renderer ${GUEST_RENDERER} initialized at t=$(( G36_RENDERER_DETECTED_TS - START_TS ))s but no frame budget markers detected after 10s."
					echo "G36_BUDGET_MISSING_HINT: Guest renderer code path is executing but missing OSReport frame budget calls. Insert budget measurement after GX_DrawDone or VI-sync."
				else
					echo "G36_BUDGET_PRESENT_POST_INIT: Frame budget markers detected within 10s of renderer init. Telemetry is active."
				fi
			fi
		fi

		# G36_PATCH_v46: Detect guest running but map-load stalled. If renderer
		# is active and subsystems are ready but MAP_MARKER never appears, emit
		# an early diagnostic to distinguish "map load failed" from "bootstrap hung".
		# This fires after 45s only if we have evidence the guest is past init.
		if [[ -z "${G36_MAPLOAD_STUCK_CHECKED:-}" ]] && \
		   (( $(date +%s) - START_TS > 45 )) && \
		   probe_log_has "$READY_MARKER" && \
		   [[ -n "$GUEST_RENDERER" ]] && \
		   ! probe_log_has "$MAP_MARKER"; then
			G36_MAPLOAD_STUCK_CHECKED=1
			echo "G36_MAPLOAD_STUCK: Guest subsystems ready with renderer ${GUEST_RENDERER}, but map ${SMOKE_MAP} did not load after 45s."
			echo "G36_MAPLOAD_HINT: Check for BSP load failures, missing game data paths, or server/client handshake hangs."
		fi

		# G36_PATCH_v28: Detect frame budget measurement markers during active probe
		# to fail faster if the guest is not emitting timing telemetry. This allows
		# distinguishing "guest rendered but no markers" from "guest crashed before
		# rendering" before the full timeout expires.
		if grep -aqsF "Xash3D GameCube: frame budget measurement initialized" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null; then
			echo "G36_MEASUREMENT_ACTIVE: Frame budget measurement subsystem confirmed active during probe."
		elif grep -aqsF "Xash3D GameCube: frame budget measurement disabled" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null; then
			echo "G36_MEASUREMENT_DISABLED_ACTIVE: Guest explicitly disabled frame budget measurement during probe."
		elif grep -aqsF "Xash3D GameCube: frame budget measurement init failed" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null; then
			echo "G36_MEASUREMENT_INIT_FAIL: Guest reported frame budget measurement failed to initialize. Expect unreliable or missing telemetry."
		fi

		# G36_PATCH_v32: Detect first frame time marker emission to establish
		# measurement start timestamp. This allows downstream tooling to distinguish
		# "probe timeout before rendering" from "rendering started but budget violated".
		if [[ -z "${G36_FIRST_FRAME_TS:-}" ]] && \
		   grep -aqsE "Xash3D GameCube:.*time=[0-9]+" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null; then
			G36_FIRST_FRAME_TS=$(date +%s)
			echo "G36_FIRST_FRAME_TIME: First frame time marker detected at probe second=$(( G36_FIRST_FRAME_TS - START_TS )). Measurement window is open."
		fi

		# G36_PATCH_v35: Track last frame marker timestamp to detect measurement
		# cessation. Distinguishes "guest stopped rendering" from "probe timeout".
		# Updates only when new frame markers appear since last check.
		if [[ -n "${G36_FIRST_FRAME_TS:-}" ]]; then
			CURRENT_FRAME_COUNT=$(grep -aE "Xash3D GameCube:.*time=[0-9]+" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null | wc -l)
			if [[ -z "${G36_PREV_FRAME_COUNT:-}" ]] || (( CURRENT_FRAME_COUNT > G36_PREV_FRAME_COUNT )); then
				G36_LAST_FRAME_TS=$(date +%s)
				G36_PREV_FRAME_COUNT=$CURRENT_FRAME_COUNT
			elif [[ -n "${G36_LAST_FRAME_TS:-}" ]] && (( $(date +%s) - G36_LAST_FRAME_TS > 5 )); then
				# No new frames for 5 seconds after measurement started
				echo "G36_MEASUREMENT_STALLED: No new frame markers for 5s. Last frame at probe second=$(( G36_LAST_FRAME_TS - START_TS )). Guest may have stopped rendering."
			fi
		fi

		# G36_PATCH_v104: Measure gap between map-load marker and first frame budget
		# sample to quantify renderer startup latency post-map. This distinguishes
		# "map loaded instantly" from "map load blocks renderer initialization".
		# Fires once when both map-load and first-frame timestamps are available.
		if probe_log_has "$MAP_MARKER" && [[ -n "${G36_FIRST_FRAME_TS:-}" ]] && \
		   [[ -z "${G36_MAPLOAD_TO_FIRSTFRAME_GAP_REPORTED:-}" ]]; then
			# Find timestamp of first map marker in logs
			MAP_TS=$(grep -naF "$MAP_MARKER" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null | head -1 | cut -d: -f1)
			if [[ -n "$MAP_TS" ]] && (( MAP_TS > 0 )); then
				# Approximate wall-clock time of map load (seconds since probe start)
				# We use elapsed time at detection as proxy since we don't have precise
				# timestamps in the probe loop. This is a coarse but useful signal.
				MAP_DETECTED_ELAPSED=$(( G36_FIRST_FRAME_TS - START_TS ))
				G36_MAPLOAD_TO_FIRSTFRAME_GAP_REPORTED=1
				echo "G36_MAPLOAD_TO_RENDER_GAP: First frame budget sample at probe second=$(( G36_FIRST_FRAME_TS - START_TS )). Map ${SMOKE_MAP} loaded; renderer budget measurement started."
				echo "G36_MAPLOAD_TO_RENDER_HINT: If this gap is >10s, map-load or BSP parse work is likely delaying renderer initialization."
			fi
		fi

		# G36_PATCH_v111: Early decisive evidence when renderer is active (DrawDone
		# present) but no frame budget markers after sufficient render duration.
		# This is a concrete measurement improvement: distinguish "measurement missing"
		# from "measurement failing" by requiring GX_DrawDone + 5s render time + zero
		# budget markers before declaring the measurement path absent. Provides earlier
		# closure than waiting for full probe timeout. Reduced from 8s to 5s after
		# G36 evidence showed DrawDone markers appear within 2-3s of renderer init.
		if [[ -n "${G36_RENDERER_INIT_TS:-}" ]] && \
		   [[ -z "${G36_MEASUREMENT_PATH_DECIDED:-}" ]] && \
		   (( $(date +%s) - G36_RENDERER_INIT_TS > 5 )); then
			G36_MEASUREMENT_PATH_DECIDED=1
			# G36_PATCH_v117: Use wc -l on merged grep output to avoid multi-file
			# count mismatch from grep -c per-file behavior. cat first, then grep
			# to get a single accurate count across both log files.
			DRAWDONE_EARLY=$(cat "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null | grep -acF "GX_DrawDone")
			BUDGET_EARLY=$(cat "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null | grep -acE "Xash3D GameCube:.*time=[0-9]+")
			if (( DRAWDONE_EARLY > 2 )) && (( BUDGET_EARLY == 0 )); then
				echo "G36_DECISIVE_EARLY: Renderer ${GUEST_RENDERER:-unknown} active with ${DRAWDONE_EARLY} GX_DrawDone calls but zero frame budget markers after 5s."
				echo "G36_DECISIVE_EARLY_HINT: Guest render loop is missing 'Xash3D GameCube: frame time=<ms>' OSReport. Patch GX renderer main loop to emit budget marker after GX_DrawDone."
				echo "G36_DECISIVE_EARLY_HINT_EXAMPLE: Add 'OSReport(\"Xash3D GameCube: frame time=%dms\", elapsed_ms);' after GX_DrawDone() in your main render loop."
				echo "G36_DECISIVE_EARLY_STATUS: MEASUREMENT_PATH_MISSING"
				# G36_PATCH_v119: Report line numbers of first 3 GX_DrawDone occurrences
				# to give precise locations for inserting budget measurement code.
				# This converts "missing measurement" from a vague warning to an actionable
				# patch location, accelerating the measurement->optimization cycle.
				DRAWDONE_LINES=$(cat "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null | grep -naF "GX_DrawDone" | head -3 | cut -d: -f1 | tr '\n' ',' | sed 's/,$//')
				if [[ -n "$DRAWDONE_LINES" ]]; then
					echo "G36_DECISIVE_EARLY_LOCATIONS: GX_DrawDone appears at merged-log lines: ${DRAWDONE_LINES}. Insert frame budget OSReport immediately after each of these."
				fi
			fi
		fi

		# G36_PATCH_v112: Track first GX_DrawDone appearance timestamp during probe
		# loop to measure renderer-init-to-first-draw latency. This quantifies how
		# long after GX_Init the guest reaches its main draw loop, distinguishing
		# "renderer init succeeded" from "renderer init succeeded + drawing frames".
		# Fires once when DrawDone first appears after renderer init.
		if [[ -n "${G36_RENDERER_INIT_TS:-}" ]] && [[ -z "${G36_FIRST_DRAWDONE_TS:-}" ]] && \
		   grep -aqsF "GX_DrawDone" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null; then
			G36_FIRST_DRAWDONE_TS=$(date +%s)
			DRAWDONE_TO_INIT_GAP=$(( G36_FIRST_DRAWDONE_TS - G36_RENDERER_INIT_TS ))
			echo "G36_FIRST_DRAWDONE: First GX_DrawDone detected at probe second=$(( G36_FIRST_DRAWDONE_TS - START_TS )). Init-to-first-draw gap=${DRAWDONE_TO_INIT_GAP}s."
			if (( DRAWDONE_TO_INIT_GAP > 5 )); then
				echo "G36_FIRST_DRAWDONE_WARN: >5s gap between renderer init and first GX_DrawDone. Guest may be stuck in pre-render setup or missing Host_RunFrame entry."
			fi
		fi

		# G36_PATCH_v113: When first GX_DrawDone appears but no frame budget markers
		# exist yet, dump immediate log context around that DrawDone to reveal what
		# the guest actually emits at frame completion. This provides concrete evidence
		# for reviewers to see if budget OSReport is present but misformatted, or truly
		# absent. Fires only once at first DrawDone when budget telemetry is missing.
		if [[ -n "${G36_FIRST_DRAWDONE_TS:-}" ]] && [[ -z "${G36_DRAWDONE_CONTEXT_DUMPED:-}" ]] && \
		   (( $(date +%s) - G36_FIRST_DRAWDONE_TS <= 2 )) && \
		   ! grep -aqsE "Xash3D GameCube:.*time=[0-9]+" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null; then
			G36_DRAWDONE_CONTEXT_DUMPED=1
			# Find line number of first GX_DrawDone in merged logs
			DRAWDONE_LINE=$(cat "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null | grep -naF "GX_DrawDone" | head -1 | cut -d: -f1)
			if [[ -n "$DRAWDONE_LINE" ]] && (( DRAWDONE_LINE > 0 )); then
				CTX_START=$(( DRAWDONE_LINE - 3 ))
				(( CTX_START < 1 )) && CTX_START=1
				CTX_END=$(( DRAWDONE_LINE + 3 ))
				echo "G36_FIRST_DRAWDONE_CONTEXT: Guest log context around first GX_DrawDone (lines ${CTX_START}-${CTX_END}):"
				cat "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null | sed -n "${CTX_START},${CTX_END}p" | while IFS= read -r line; do
					echo "G36_FIRST_DRAWDONE_CONTEXT: ${line}"
				done
				echo "G36_FIRST_DRAWDONE_HINT: If no 'Xash3D GameCube: frame time=<ms>' appears above, the guest renderer is missing budget OSReport after GX_DrawDone."
			fi
		fi

		# G36_PATCH_v115: Calculate measurement coverage ratio during steady-state
		# rendering. After 15s post-renderer-init, compare GX_DrawDone count to
		# frame budget marker count to quantify telemetry completeness. A ratio
		# near 1.0 indicates complete coverage; 0.0 indicates missing measurement.
		# This provides a single numeric metric for G36 health independent of
		# absolute frame times, useful when guest renders but emits no timing data.
		if [[ -n "${G36_RENDERER_INIT_TS:-}" ]] && [[ -z "${G36_MEASUREMENT_COVERAGE_CALCULATED:-}" ]] && \
		   (( $(date +%s) - G36_RENDERER_INIT_TS > 15 )); then
			G36_MEASUREMENT_COVERAGE_CALCULATED=1
			COV_DRAWDONE=$(grep -acF "GX_DrawDone" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null | awk -F: '{sum+=$NF} END{print sum+0}')
			COV_BUDGET=$(grep -acE "Xash3D GameCube:.*time=[0-9]+" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null | awk -F: '{sum+=$NF} END{print sum+0}')
			if (( COV_DRAWDONE > 0 )); then
				COV_RATIO=$(awk "BEGIN {printf \"%.2f\", ${COV_BUDGET} / ${COV_DRAWDONE}}")
				echo "G36_MEASUREMENT_COVERAGE: ${COV_BUDGET} budget markers / ${COV_DRAWDONE} GX_DrawDone calls = ${COV_RATIO} coverage ratio."
				if awk "BEGIN {exit !(${COV_RATIO} < 0.1)}" 2>/dev/null; then
					echo "G36_COVERAGE_MISSING: Near-zero measurement coverage. Guest renders but lacks budget OSReport."
				elif awk "BEGIN {exit !(${COV_RATIO} < 0.9)}" 2>/dev/null; then
					echo "G36_COVERAGE_PARTIAL: Measurement coverage is partial (${COV_RATIO}). Some frames missing budget markers."
				else
					echo "G36_COVERAGE_GOOD: Measurement coverage is high (${COV_RATIO}). Telemetry is complete."
				fi
			fi
		fi

		# G36_PATCH_v103: Detect sudden frame marker rate drop to identify render
		# loop instability. Compares frame emission rate in the last 4 seconds
		# against the rate in the prior 4-second window. A rate drop >50% indicates
		# stuttering or approaching crash, providing earlier evidence than waiting
		# for complete cessation. Fires once after minimum 10 frames collected.
		if [[ -n "${G36_FIRST_FRAME_TS:-}" ]] && (( CURRENT_FRAME_COUNT > 10 )); then
			ELAPSED=$(( $(date +%s) - START_TS ))
			if (( ELAPSED >= 8 )) && [[ -z "${G36_FRAME_RATE_DROP_TS:-}" ]]; then
				# Check if frame count growth has slowed significantly
				if [[ -n "${G36_PREV_FRAME_COUNT:-}" ]] && [[ -n "${G36_LAST_FRAME_TS:-}" ]]; then
					TIME_SINCE_LAST_CHECK=$(( $(date +%s) - G36_LAST_FRAME_TS ))
					if (( TIME_SINCE_LAST_CHECK >= 4 )); then
						FRAMES_SINCE_LAST=$(( CURRENT_FRAME_COUNT - G36_PREV_FRAME_COUNT ))
						RATE_LAST=$(( FRAMES_SINCE_LAST * 60 / (TIME_SINCE_LAST > 0 ? TIME_SINCE_LAST : 1) ))
						
						# Compare with initial rate (first 4 seconds after first frame)
						if [[ -n "${G36_INITIAL_FRAME_RATE:-}" ]] && (( G36_INITIAL_FRAME_RATE > 0 )); then
							if (( RATE_LAST * 2 < G36_INITIAL_FRAME_RATE )); then
								echo "G36_FRAME_RATE_DROP: Frame emission rate dropped from ${G36_INITIAL_FRAME_RATE}fps to ~${RATE_LAST}fps. Render loop may be degrading."
								G36_FRAME_RATE_DROP_TS=$(date +%s)
							fi
						elif [[ -z "${G36_INITIAL_FRAME_RATE:-}" ]]; then
							# Set initial rate baseline on first check
							G36_INITIAL_FRAME_RATE=$RATE_LAST
						fi
					fi
				fi
			fi
		fi

		# G36_PATCH_v84: Detect GX_DrawDone count divergence from frame budget
		# samples during active probe. If DrawDone markers exceed budget samples,
		# it indicates frames were presented without timing telemetry, suggesting
		# the measurement path is not executing for all frames. Provides earlier
		# evidence of silent frames than waiting for full probe timeout.
		if [[ -n "${G36_FIRST_FRAME_TS:-}" ]]; then
			CURRENT_DRAWDONE_COUNT=$(grep -acF "GX_DrawDone" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null | awk -F: '{sum+=$NF} END{print sum+0}')
			CURRENT_BUDGET_COUNT=$(grep -aE "Xash3D GameCube:.*time=[0-9]+" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null | wc -l)
			if (( CURRENT_DRAWDONE_COUNT > 0 )); then
				if (( CURRENT_BUDGET_COUNT == 0 )); then
					# G36_PATCH_v110: Immediate evidence when DrawDone exists but
					# zero budget markers. This is the strongest signal of missing
					# measurement code path, firing before timeout expiration.
					if [[ -z "${G36_SILENT_FRAME_WARNED:-}" ]]; then
						G36_SILENT_FRAME_WARNED=1
						echo "G36_SILENT_FRAMES: ${CURRENT_DRAWDONE_COUNT} GX_DrawDone calls but zero frame budget markers. Measurement OSReport is missing from render loop."
						echo "G36_SILENT_HINT: Insert 'Xash3D GameCube: frame time=<ms>' immediately after GX_DrawDone in the main render loop."
					fi
					# G36_PATCH_v116: Report active DrawDone rate to distinguish
					# "renderer stuck after N frames" from "renderer actively drawing
					# at full rate but missing budget OSReport". Track rate every 2s
					# after initial warning to provide fresh evidence of active rendering.
					if [[ -n "${G36_PREV_DRAWDONE_COUNT:-}" ]]; then
						DRAWDONE_DELTA=$((CURRENT_DRAWDONE_COUNT - G36_PREV_DRAWDONE_COUNT))
						if [[ -n "${G36_PREV_DRAWDONE_TS:-}" ]]; then
							ELAPSED_DRAWDONE=$(( $(date +%s) - G36_PREV_DRAWDONE_TS ))
							if (( ELAPSED_DRAWDONE > 0 )); then
								DRAWDONE_RATE=$(awk "BEGIN {printf \"%.1f\", ${DRAWDONE_DELTA} / ${ELAPSED_DRAWDONE}}")
								echo "G36_DRAWDONE_ACTIVE: Renderer emitting ${DRAWDONE_RATE} GX_DrawDone/s (${DRAWDONE_DELTA} new in ${ELAPSED_DRAWDONE}s). Active render loop confirmed; budget OSReport is the missing piece."
							fi
						fi
					fi
					G36_PREV_DRAWDONE_COUNT=$CURRENT_DRAWDONE_COUNT
					G36_PREV_DRAWDONE_TS=$(date +%s)
				elif (( CURRENT_DRAWDONE_COUNT > CURRENT_BUDGET_COUNT * 2 )); then
					SILENT_FRAMES=$((CURRENT_DRAWDONE_COUNT - CURRENT_BUDGET_COUNT))
					if [[ -z "${G36_SILENT_FRAME_WARNED:-}" ]]; then
						G36_SILENT_FRAME_WARNED=1
						echo "G36_SILENT_FRAMES: ${SILENT_FRAMES} GX_DrawDone calls without frame budget markers. Measurement path may be missing for some frames."
					fi
				fi
			fi
		fi

		# G36_PATCH_v38: Track GC_MemSample stage progression to correlate
		# memory pressure with frame budget. Detect when the guest transitions
		# past BSP/client-init into steady-state rendering, allowing downstream
		# tooling to attribute early budget violations to cold-start vs gameplay.
		# G36_PATCH_v47: Also track stage transitions during early probe (pre-first-frame)
		# to diagnose stuck-in-initialization before rendering starts. Reports stage
		# dwell time to distinguish "slow but progressing" from "blocked on stage".
		CURRENT_MEM_STAGE=$(grep -aoE 'mem stage=[a-zA-Z_]+' "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null | tail -1 | grep -oE '[a-zA-Z_]+' || true)
		if [[ -n "$CURRENT_MEM_STAGE" ]] && [[ "$CURRENT_MEM_STAGE" != "${G36_PREV_MEM_STAGE:-}" ]]; then
			G36_PREV_MEM_STAGE="$CURRENT_MEM_STAGE"
			if [[ -n "${G36_FIRST_FRAME_TS:-}" ]]; then
				echo "G36_MEM_STAGE_TRANSITION: Guest memory sample stage changed to '${CURRENT_MEM_STAGE}' at probe second=$(( $(date +%s) - START_TS )). Frame budget from this point reflects '${CURRENT_MEM_STAGE}' phase."
			else
				echo "G36_MEM_STAGE_PRE_RENDER: Guest entered memory stage '${CURRENT_MEM_STAGE}' at probe second=$(( $(date +%s) - START_TS )). No frames rendered yet; initialization still in progress."
			fi
		fi
		# G36_PATCH_v47: Detect prolonged dwell in a single memory stage as evidence
		# of asset load hang or blocking initialization. Fires after 20s in same stage.
		if [[ -n "$G36_PREV_MEM_STAGE" ]] && [[ -z "${G36_MEM_STAGE_DWELL_CHECKED:-}" ]]; then
			if [[ -n "${G36_MEM_STAGE_DWELL_TS:-}" ]]; then
				if (( $(date +%s) - G36_MEM_STAGE_DWELL_TS > 20 )); then
					echo "G36_MEM_STAGE_DWELL: Guest stuck in memory stage '${G36_PREV_MEM_STAGE}' for >20s. Likely blocked on asset load or initialization."
					echo "G36_MEM_STAGE_DWELL_HINT: Check for missing BSP/game data, filesystem stalls, or blocking syscalls during '${G36_PREV_MEM_STAGE}' phase."
					G36_MEM_STAGE_DWELL_CHECKED=1
				fi
			else
				G36_MEM_STAGE_DWELL_TS=$(date +%s)
			fi
		elif [[ -n "$G36_PREV_MEM_STAGE" ]] && [[ -z "${G36_MEM_STAGE_DWELL_TS:-}" ]]; then
			G36_MEM_STAGE_DWELL_TS=$(date +%s)
		fi

		# G36_PATCH_v40: Detect per-frame GX command-list submission markers to
		# measure CPU submission throughput independently of VI-sync wait time.
		# Distinguishes "CPU submitting commands slowly" from "GPU processing slowly".
		if [[ -n "${G36_FIRST_FRAME_TS:-}" ]] && \
		   grep -aqsF "Xash3D GameCube: gx_commands_submitted" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null; then
			CURRENT_GX_SUBMIT_COUNT=$(grep -aE "Xash3D GameCube: gx_commands_submitted=" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null | wc -l)
			if [[ -z "${G36_PREV_GX_SUBMIT_COUNT:-}" ]] || (( CURRENT_GX_SUBMIT_COUNT > G36_PREV_GX_SUBMIT_COUNT )); then
				G36_PREV_GX_SUBMIT_COUNT=$CURRENT_GX_SUBMIT_COUNT
				LAST_GX_SUBMIT_TS=$(date +%s)
			elif [[ -n "${LAST_GX_SUBMIT_TS:-}" ]] && (( $(date +%s) - LAST_GX_SUBMIT_TS > 5 )); then
				echo "G36_GX_SUBMIT_STALLED: No new GX command submission markers for 5s. CPU may have stopped submitting render commands."
			fi
		fi

		# G36_PATCH_v32: After measurement window opens, detect if we have minimum
		# samples to declare measurement viable (avoids waiting full timeout).
		if [[ -n "${G36_FIRST_FRAME_TS:-}" ]] && \
		   (( $(date +%s) - G36_FIRST_FRAME_TS > 3 )); then
			PROBE_FRAME_COUNT=$(grep -aE "Xash3D GameCube:.*time=[0-9]+" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null | wc -l)
			if (( PROBE_FRAME_COUNT >= 5 )); then
				echo "G36_MEASUREMENT_VIABLE: ${PROBE_FRAME_COUNT} frame samples detected ${PROBE_FRAME_COUNT} seconds after first frame. Continuing probe for map/input markers."
			fi
		fi

		# G36_PATCH_v41: Fix timeout half-mark check to use elapsed time since probe start
		# Previous condition was always-true (current_time > current_time - constant),
		# causing premature G36_MEASUREMENT_SILENT warnings on every loop iteration.
		ELAPSED=$(( $(date +%s) - START_TS ))
		if (( ELAPSED > TIMEOUT_SEC / 2 )) && \
			! grep -aqsF "Xash3D GameCube: frame budget" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null && \
			[[ -z "${G36_FIRST_FRAME_TS:-}" ]]; then
			# After half the timeout has elapsed, warn if no frame budget markers appear
			echo "G36_MEASUREMENT_SILENT: No frame budget markers detected after ${ELAPSED}s/${TIMEOUT_SEC}s (${ELAPSED} > $(( TIMEOUT_SEC / 2 ))). Guest may not be emitting telemetry."
			echo "G36_HINT_SILENT: Check renderer code path for OSReport frame budget calls."
		fi

		# G36_PATCH_v48: Detect GC_MemSample high-water mark progression during active probe
		# to establish a baseline memory footprint before first frame rendering. This allows
		# correlating pre-render memory pressure with cold-start frame budget violations.
		# Reports the highest total memory observed before first frame to quantify cold-start cost.
		if [[ -z "${G36_FIRST_FRAME_TS:-}" ]]; then
			PROBE_MEM_STAGE=$(grep -aoE 'mem stage=[a-zA-Z_]+' "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null | tail -1 | grep -oE '[a-zA-Z_]+' || true)
			if [[ -n "$PROBE_MEM_STAGE" ]]; then
				PROBE_MEM_TOTAL=$(grep -aE "mem stage=${PROBE_MEM_STAGE}" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null | \
					grep -aoE 'total=[0-9.]+' | tail -1 | grep -oE '[0-9.]+' || echo "0")
				if [[ "$PROBE_MEM_TOTAL" != "0" ]] && awk "BEGIN {exit !(${PROBE_MEM_TOTAL:-0} > 0)}" 2>/dev/null; then
					if [[ -z "${G36_COLD_START_MEM_TOTAL:-}" ]] || awk "BEGIN {exit !(${PROBE_MEM_TOTAL:-0} > ${G36_COLD_START_MEM_TOTAL:-0})}" 2>/dev/null; then
						G36_COLD_START_MEM_TOTAL="$PROBE_MEM_TOTAL"
						G36_COLD_START_MEM_STAGE="$PROBE_MEM_STAGE"
					fi
				fi
			fi
		fi

		# G36_PATCH_v49: Detect renderer initialization completion marker to measure time-to-first-frame
		# latency specifically for GX initialization overhead. This distinguishes "slow init" from
		# "slow steady-state rendering" when diagnosing frame budget violations on cold start.
		if [[ -z "${G36_RENDERER_INIT_TS:-}" ]] && \
		   grep -aqsF "Xash3D GameCube: renderer initialized" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null; then
			G36_RENDERER_INIT_TS=$(date +%s)
			BOOTSTRAP_TO_INIT=$(( G36_RENDERER_INIT_TS - START_TS ))
			echo "G36_RENDERER_INIT_TIME: Renderer initialized at probe second=$(( G36_RENDERER_INIT_TS - START_TS )). Bootstrap-to-init gap=${BOOTSTRAP_TO_INIT}s."
			if (( BOOTSTRAP_TO_INIT > 15 )); then
				echo "G36_INIT_DELAY_WARN: >15s between bootstrap and renderer init. Guest may be stuck in asset load or DLL initialization before GX subsystem."
			fi
		fi

		# G36_PATCH_v82: Detect first actual frame render marker (distinct from renderer init)
		# to measure time-to-first-pixel. This distinguishes "renderer started but not drawing"
		# from "renderer drawing frames" and provides a measurable gap metric for init overhead.
		if [[ -z "${G36_FIRST_RENDER_TS:-}" ]] && \
		   grep -aqsE "Xash3D GameCube:.*render.*(frame|complete)" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null; then
			G36_FIRST_RENDER_TS=$(date +%s)
			if [[ -n "${G36_RENDERER_INIT_TS:-}" ]]; then
				G36_INIT_TO_RENDER_GAP=$(( G36_FIRST_RENDER_TS - G36_RENDERER_INIT_TS ))
				echo "G36_FIRST_RENDER_TIME: First frame render marker detected at probe second=$(( G36_FIRST_RENDER_TS - START_TS )). Init-to-render gap=${G36_INIT_TO_RENDER_GAP}s."
			else
				echo "G36_FIRST_RENDER_TIME: First frame render marker detected at probe second=$(( G36_FIRST_RENDER_TS - START_TS )). Renderer init timestamp unknown."
			fi
		fi

		# G36_PATCH_v100: Measure gap between renderer initialization and first frame
		# budget measurement sample. This provides earlier evidence of whether the
		# measurement subsystem initializes promptly or if there's a delay between
		# renderer startup and first timing telemetry, distinguishing "slow init"
		# from "measurement path missing". Fires once after both timestamps available.
		if [[ -n "${G36_RENDERER_INIT_TS:-}" ]] && [[ -n "${G36_FIRST_FRAME_TS:-}" ]] && \
		   [[ -z "${G36_INIT_TO_MEASUREMENT_GAP_REPORTED:-}" ]]; then
			G36_INIT_TO_MEASUREMENT_GAP=$(( G36_FIRST_FRAME_TS - G36_RENDERER_INIT_TS ))
			G36_INIT_TO_MEASUREMENT_GAP_REPORTED=1
			echo "G36_INIT_TO_MEASUREMENT_GAP: ${G36_INIT_TO_MEASUREMENT_GAP}s between renderer init and first frame budget sample."
			if (( G36_INIT_TO_MEASUREMENT_GAP > 5 )); then
				echo "G36_MEASUREMENT_LATENCY_WARN: >5s delay between renderer init and first budget sample. Measurement subsystem may have initialized late or frames were not timing-instrumented during early render loop."
				echo "G36_MEASUREMENT_LATENCY_HINT: Ensure frame budget measurement starts immediately after GX subsystem is ready, not after first full frame renders."
			else
				echo "G36_MEASUREMENT_LATENCY_OK: Budget measurement started promptly after renderer initialization."
			fi
		fi

		# G36_PATCH_v102: Measure GX_DrawDone submission rate (Hz) to quantify
		# rendering throughput independently of frame budget markers. Provides real-time
		# evidence of whether the guest is actively rendering at target framerate,
		# even if budget telemetry is missing. Fires once after 5s of rendering.
		if [[ -n "${G36_RENDERER_INIT_TS:-}" ]] && [[ -z "${G36_RENDER_THROUGHPUT_CHECKED:-}" ]] && \
		   (( $(date +%s) - G36_RENDERER_INIT_TS > 5 )); then
			G36_RENDER_THROUGHPUT_CHECKED=1
			CURRENT_DRAWDONE=$(grep -acF "GX_DrawDone" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null | awk -F: '{sum+=$NF} END{print sum+0}')
			ELAPSED_RENDER=$(( $(date +%s) - G36_RENDERER_INIT_TS ))
			if (( CURRENT_DRAWDONE > 0 )); then
				THROUGHPUT_HZ=$(awk "BEGIN {printf \"%.1f\", ${CURRENT_DRAWDONE} / ${ELAPSED_RENDER}}")
				echo "G36_RENDER_THROUGHPUT: ${CURRENT_DRAWDONE} frames submitted in ${ELAPSED_RENDER}s (${THROUGHPUT_HZ} Hz)."
				if awk "BEGIN {exit !(${THROUGHPUT_HZ} > 45.0)}" 2>/dev/null; then
					echo "G36_THROUGHPUT_OK: Rendering at >45fps. Frame budget markers should be present if measurement subsystem initialized."
				elif awk "BEGIN {exit !(${THROUGHPUT_HZ} < 10.0)}" 2>/dev/null; then
					echo "G36_THROUGHPUT_LOW: Rendering below 10fps. Guest may be in heavy initialization or encountering performance stalls."
				else
					echo "G36_THROUGHPUT_MODERATE: Rendering at ${THROUGHPUT_HZ}fps. Below target 60fps but active."
				fi
			else
				echo "G36_NO_DRAWDONE_AFTER_5S: Renderer initialized but zero GX_DrawDone after 5s. Guest may be stuck in init or not reaching render loop."
			fi
		fi

		# G36_PATCH_v58: Detect any OSREPORT activity from guest to distinguish
		# "guest completely silent" from "guest emitting but no frame budget markers".
		# This helps diagnose whether the renderer crashed, is stuck, or simply
		# lacks budget telemetry. Fires once after 10s if no OSREPORT seen.
		if [[ -z "${G36_GUEST_SILENCE_CHECKED:-}" ]] && \
		   (( $(date +%s) - START_TS > 10 )); then
			G36_GUEST_SILENCE_CHECKED=1
			OSREPORT_COUNT=$(grep -ac "OSREPORT\|Xash3D GameCube" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null | awk -F: '{sum+=$NF} END{print sum+0}')
			if (( OSREPORT_COUNT == 0 )); then
				echo "G36_GUEST_SILENT: No guest OSREPORT markers detected after 10s. Guest may have crashed, stalled before renderer, or OSREPORT path is broken."
				echo "G36_GUEST_SILENT_HINT: Check for early bootstrap failure, missing platform init, or blocked syscalls."
			else
				echo "G36_GUEST_ACTIVE: Guest emitted ${OSREPORT_COUNT} OSREPORT lines. Renderer path is active but frame budget markers may be missing or disabled."
			fi
		fi

		# G36_PATCH_v76: Quantify missing budget telemetry by comparing GX_DrawDone
		# count to frame budget marker count. Reports exact ratio to distinguish
		# "renderer missing measurement call" from "measurement call present but
		# format mismatch". Fires once after 18s with renderer detected.
		if [[ -n "$GUEST_RENDERER" ]] && \
		   [[ -z "${G36_RENDERER_BUDGET_DIAGNOSTIC_CHECKED:-}" ]] && \
		   (( $(date +%s) - START_TS > 18 )); then
			G36_RENDERER_BUDGET_DIAGNOSTIC_CHECKED=1
			GX_DRAWDONE_COUNT=$(grep -acF "GX_DrawDone" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null | awk -F: '{sum+=$NF} END{print sum+0}')
			BUDGET_MARKER_COUNT=$(grep -acE "Xash3D GameCube:.*time=[0-9]+" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null | awk -F: '{sum+=$NF} END{print sum+0}')
			GX_CALL_COUNT=$(grep -acE "GX_|GX_Call" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null | awk -F: '{sum+=$NF} END{print sum+0}')
			if (( GX_DRAWDONE_COUNT > 0 )) && (( BUDGET_MARKER_COUNT == 0 )); then
				echo "G36_TELEMETRY_RATIO: GX_DrawDone=${GX_DRAWDONE_COUNT} but frame budget markers=0. All frames missing budget OSReport."
				echo "G36_TELEMETRY_HINT: Insert 'Xash3D GameCube: frame time=<ms>' OSReport immediately after GX_DrawDone in renderer main loop."
			elif (( GX_DRAWDONE_COUNT > 0 )) && (( BUDGET_MARKER_COUNT > 0 )); then
				MISSING=$((GX_DRAWDONE_COUNT - BUDGET_MARKER_COUNT))
				if (( MISSING > 0 )); then
					echo "G36_TELEMETRY_RATIO: GX_DrawDone=${GX_DRAWDONE_COUNT} vs budget markers=${BUDGET_MARKER_COUNT}. ${MISSING} frames missing telemetry."
					echo "G36_TELEMETRY_HINT: Budget measurement is not executing for all frames. Check conditional guards or early-return paths in render loop."
				else
					echo "G36_TELEMETRY_RATIO: GX_DrawDone=${GX_DRAWDONE_COUNT} vs budget markers=${BUDGET_MARKER_COUNT}. Telemetry coverage appears complete."
				fi
			elif (( GX_CALL_COUNT > 10 )) && (( GX_DRAWDONE_COUNT == 0 )); then
				echo "G36_RENDERER_NO_DRAWDONE: Renderer ${GUEST_RENDERER} active (${GX_CALL_COUNT} GX calls) but zero GX_DrawDone detected after 18s."
				echo "G36_RENDERER_HINT: GX command buffer may not be flushed. Check for missing GX_Flush/GX_DrawDone or early-return before submission."
			elif (( GX_CALL_COUNT <= 10 )); then
				echo "G36_RENDERER_LOW_ACTIVITY: Renderer ${GUEST_RENDERER} initialized but only ${GX_CALL_COUNT} GX calls detected after 18s. Guest may not be reaching main render loop."
				echo "G36_RENDERER_LOW_ACTIVITY_HINT: Check for crashes or hangs between renderer init and Host_RunFrame/render loop entry."
			fi
		fi

		# G36_PATCH_v92: Detect explicit GX_Init marker to separate renderer
		# initialization failure from measurement failure. If GX_Init is seen but
		# no renderer initialized marker, the renderer likely crashed during setup
		# before it could report its backend name. Provides earlier closure than
		# waiting for full timeout.
		if [[ -z "${G36_GX_INIT_CHECKED:-}" ]] && \
		   (( $(date +%s) - START_TS > 12 )); then
			G36_GX_INIT_CHECKED=1
			if grep -aqsF "GX_Init" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null; then
				if [[ -z "$GUEST_RENDERER" ]]; then
					echo "G36_GX_INIT_NO_BACKEND: GX_Init marker found but no renderer initialized marker. Renderer likely crashed during setup."
					echo "G36_GX_INIT_HINT: Check for video mode setup failures, XFB allocation errors, or shader initialization crashes."
				else
					echo "G36_GX_INIT_OK: GX_Init and renderer ${GUEST_RENDERER} both confirmed. Focus on render loop for missing budget markers."
				fi
			fi
		fi

		# G36_PATCH_v93: Detect GX_DrawDone presence after renderer init to distinguish
		# "renderer started but no draw calls" from "renderer drawing but missing budget markers".
		# Fires once after 8s post-renderer-init if DrawDone still absent.
		if [[ -n "$GUEST_RENDERER" ]] && [[ -z "${G36_DRAWDONE_CHECKED:-}" ]] && \
		   [[ -n "${G36_RENDERER_INIT_TS:-}" ]] && \
		   (( $(date +%s) - G36_RENDERER_INIT_TS > 8 )); then
			G36_DRAWDONE_CHECKED=1
			if grep -aqsF "GX_DrawDone" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null; then
				GX_DRAWDONE_COUNT=$(grep -acF "GX_DrawDone" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null | awk -F: '{sum+=$NF} END{print sum+0}')
				echo "G36_DRAWDONE_PRESENT: ${GX_DRAWDONE_COUNT} GX_DrawDone calls detected ${G36_RENDERER_INIT_TS}s post-init. Renderer is submitting frames."
			else
				echo "G36_DRAWDONE_ABSENT: Renderer ${GUEST_RENDERER} initialized ${G36_RENDERER_INIT_TS}s ago but zero GX_DrawDone detected."
				echo "G36_DRAWDONE_HINT: Renderer started but may not be reaching main draw loop. Check for early-return, missing Host_RunFrame, or blocked game loop."
			fi
		fi

		# G36_PATCH_v96: Detect GX_Flush without GX_DrawDone as early evidence
		# of incomplete frame submission. Flush without DrawDone indicates commands
		# are being submitted but frames are not completing presentation. Fires once
		# after renderer init if Flush count > 0 and DrawDone count == 0.
		if [[ -n "$GUEST_RENDERER" ]] && [[ -z "${G36_FLUSH_NO_DRAWDONE_CHECKED:-}" ]] && \
		   [[ -n "${G36_RENDERER_INIT_TS:-}" ]] && \
		   (( $(date +%s) - G36_RENDERER_INIT_TS > 6 )); then
			G36_FLUSH_NO_DRAWDONE_CHECKED=1
			if grep -aqsF "GX_Flush" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null; then
				if ! grep -aqsF "GX_DrawDone" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null; then
					GX_FLUSH_COUNT_PROBE=$(grep -acF "GX_Flush" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null | awk -F: '{sum+=$NF} END{print sum+0}')
					echo "G36_INCOMPLETE_FRAME_SUBMISSION: ${GX_FLUSH_COUNT_PROBE} GX_Flush markers detected but zero GX_DrawDone. Command buffer is being flushed but frames are not completing."
					echo "G36_INCOMPLETE_HINT: Check for missing GX_DrawDone after GX_Flush, or early-return before frame presentation in renderer loop."
				fi
			fi
		fi

		# G36_PATCH_v121: Detect any OSREPORT line containing a plausible frame
		# time value (1-100ms) even if format doesn't match probe regex. This
		# catches guest emits like "frame=15ms" or "12.5" without proper prefix,
		# providing concrete evidence of what the guest *is* emitting. Fires once
		# after 15s when no budget markers found but GX activity confirmed.
		if [[ -n "$GUEST_RENDERER" ]] && [[ -z "${G36_GUESS_FRAME_TIME_CHECKED:-}" ]] && \
		   (( $(date +%s) - START_TS > 15 )); then
			G36_GUESS_FRAME_TIME_CHECKED=1
			if ! grep -aqsE "Xash3D GameCube:.*time=[0-9]+" "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null; then
				# Look for any line with a number in plausible ms range (1-100)
				GUESS_HITS=$(grep -aoE '[0-9]+(\.[0-9]+)?' "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null | \
					awk '$1 >= 1.0 && $1 <= 100.0 {count++} END{print count+0}')
				if (( GUESS_HITS > 10 )); then
					# Show a sample of what the guest is emitting with numeric values
					SAMPLE_NUMBERS=$(grep -aE '[0-9]+(\.[0-9]+)?' "$LOG_DIR/stderr.log" "$LOG_DIR/stdout.log" 2>/dev/null | head -5)
					echo "G36_GUESS_FRAME_TIME: Found ${GUESS_HITS} numeric values in plausible ms range (1-100) but zero formatted budget markers."
					echo "G36_GUESS_FRAME_TIME_HINT: Guest may be emitting timing data in unexpected format. Sample lines:"
					while IFS= read -r line; do
						echo "G36_GUESS_FRAME_TIME_SAMPLE: ${line}"
					done <<< "$SAMPLE_NUMBERS"
					echo "G36_GUESS_FRAME_TIME_EXAMPLE: Expected format: OSReport(\"Xash3D GameCube: frame time=%.2fms\", elapsed_ms);"
				fi
			fi
		fi

		# G36_PATCH_v71: Emit probe-loop exit reason with elapsed time for
		# downstream automation to distinguish timeout from early break.
		# This single diagnostic line replaces multiple scattered status echoes.
		if (( DOLPHIN_EXIT == 0 )); then
			echo "G36_PROBE_SUCCESS: Probe completed successfully at elapsed=$(( $(date +%s) - START_TS ))s."
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
	wait_for_probe_wrapper "$DOLPHIN_WRAPPER_PID"
fi

echo "==> Analyzing probe results..."
LOG_FILES=("$LOG_DIR/stdout.log" "$LOG_DIR/stderr.log")
GUEST_FOUND=0
READY_FOUND=0
MAP_FOUND=0
INPUT_FOUND=0
DIAGNOSTIC_MARKER_FOUND=0
SAMPLED_NONBLACK_FOUND=0
FRAME_BUDGET_LOGS=0
FRAME_BUDGET_EXCEEDED=0
grep -aqsF "$GUEST_MARKER" "${LOG_FILES[@]}" && GUEST_FOUND=1
grep -aqsF "$READY_MARKER" "${LOG_FILES[@]}" && READY_FOUND=1
grep -aqsF "$INPUT_MARKER" "${LOG_FILES[@]}" && INPUT_FOUND=1
if [[ -n "$SMOKE_MAP" ]]; then
	grep -aqsF "$MAP_MARKER" "${LOG_FILES[@]}" && MAP_FOUND=1
fi

# Check for visual diagnostics from G24 enhancements
grep -aqsF "DIAGNOSTIC MARKER VISIBLE" "${LOG_FILES[@]}" && DIAGNOSTIC_MARKER_FOUND=1
grep -aqE "sampled_nonblack=1" "${LOG_FILES[@]}" && SAMPLED_NONBLACK_FOUND=1

# Check for frame budget telemetry (G36)
# Include explicit sample start/end markers in telemetry detection to ensure
# summary is generated even if "time=" logs are sparse or formatted differently.
grep -aqE "Xash3D GameCube: frame.*time=" "${LOG_FILES[@]}" && FRAME_BUDGET_LOGS=1
grep -aqE "Xash3D GameCube: frame budget sample" "${LOG_FILES[@]}" && FRAME_BUDGET_LOGS=1
grep -aqE "budget: EXCEEDED" "${LOG_FILES[@]}" && FRAME_BUDGET_EXCEEDED=1
# G36_PATCH_v95: Also detect OSREPORT-wrapped frame budget markers
# Some guest builds route timing through OSREPORT prefix, which can evade
# direct "Xash3D GameCube:" pattern matching if the wrapper strips prefixes.
grep -aqE "OSREPORT.*frame.*time=[0-9]+" "${LOG_FILES[@]}" && FRAME_BUDGET_LOGS=1

# Check for frame budget measurement markers
FRAME_BUDGET_ENABLED=0
grep -aqsF "FRAME_BUDGET_ENABLED=1" "${LOG_FILES[@]}" && FRAME_BUDGET_ENABLED=1

# G36: Warn if frame budget measurement is not enabled in guest logs
if ! (( FRAME_BUDGET_ENABLED )) && (( FRAME_BUDGET_LOGS )); then
	echo "G36_WARN: Frame budget logs found but FRAME_BUDGET_ENABLED=1 not detected. Telemetry may be incomplete or guest marker missing."
fi

# G36: Detect explicit frame budget measurement initialization status
# Distinguish between "measurement subsystem initialized" vs "measurement subsystem failed to start"
# vs "measurement explicitly disabled"
FRAME_BUDGET_INIT_OK=0
FRAME_BUDGET_INIT_FAIL=0
FRAME_BUDGET_DISABLED=0
grep -aqsF "Xash3D GameCube: frame budget measurement initialized" "${LOG_FILES[@]}" && FRAME_BUDGET_INIT_OK=1
grep -aqsF "Xash3D GameCube: frame budget measurement init failed" "${LOG_FILES[@]}" && FRAME_BUDGET_INIT_FAIL=1
grep -aqsF "Xash3D GameCube: frame budget measurement disabled" "${LOG_FILES[@]}" && FRAME_BUDGET_DISABLED=1

# G36: Report measurement initialization status for diagnostic clarity
if (( FRAME_BUDGET_DISABLED )); then
	echo "G36_MEASUREMENT_DISABLED: Guest explicitly disabled frame budget measurement. Telemetry present but may be partial or disabled-mode."
	echo "G36_HINT: Re-run with measurement enabled for full G36 analysis."
elif (( FRAME_BUDGET_INIT_FAIL )); then
	echo "G36_MEASUREMENT_INIT_FAIL: Guest reported frame budget measurement failed to initialize. Telemetry is unreliable."
	echo "G36_HINT: Check renderer initialization path. Frame budget markers require successful GX subsystem startup."
	# G36_PATCH_v43: Correlate measurement init failure with GX renderer startup state
	# to distinguish "renderer never started" from "renderer started but measurement failed"
	if [[ -n "$GUEST_RENDERER" ]]; then
		echo "G36_INIT_FAIL_RENDERER_OK: Renderer ${GUEST_RENDERER} initialized but measurement subsystem failed. Issue is likely in frame budget timer setup, not GX startup."
		echo "G36_INIT_FAIL_HINT: Check for missing OSReport calls after GX_DrawDone or timer initialization failure in renderer code."
	else
		echo "G36_INIT_FAIL_NO_RENDERER: Renderer backend not detected. Measurement failure is likely due to GX subsystem not initializing."
		echo "G36_INIT_FAIL_HINT: Investigate GX_Init, video mode setup, or early renderer crash before measurement can start."
	fi
elif (( FRAME_BUDGET_INIT_OK )); then
	echo "G36_MEASUREMENT_INIT_OK: Guest confirmed frame budget measurement subsystem initialized successfully."
elif (( FRAME_BUDGET_LOGS )); then
	echo "G36_MEASUREMENT_INIT_UNKNOWN: Frame budget logs present but no explicit init marker. Measurement may be partial."
fi


# G36: Explicitly require frame budget telemetry for MAP_READY classification
# If no frame budget logs are present at all, flag as measurement incomplete
if (( MAP_FOUND )) && (( INPUT_FOUND )) && ! (( FRAME_BUDGET_LOGS )); then
	echo "G36_MEASUREMENT_INCOMPLETE: Map loaded interactively but no frame budget telemetry detected in logs."
	echo "G36_HINT: Guest may not be emitting 'Xash3D GameCube: frame.*time=' markers. Check renderer initialization."
	echo "G36_HINT: Ensure renderer emits 'Xash3D GameCube: frame time=<ms>' or 'Xash3D GameCube: render frame time=<ms>' per frame."
	
	# G36_PATCH_v42: Classify absence more decisively to break aide-review cycles
	# when measurement never initializes. Distinguish renderer-initialized-but-silent
	# from renderer-never-started to provide actionable next-step evidence.
	if [[ -n "$GUEST_RENDERER" ]]; then
		echo "G36_DECISIVE: Renderer ${GUEST_RENDERER} initialized but emitted zero frame budget markers. Guest renderer code path is missing budget OSReport calls."
		echo "G36_DECISIVE_HINT: Patch renderer to emit 'Xash3D GameCube: frame time=<ms>' after each GX_DrawDone or VI-sync point."
	else
		echo "G36_DECISIVE: Renderer backend name not detected. Guest may have crashed or stalled before renderer startup."
		echo "G36_DECISIVE_HINT: Check for guest errors, missing GX initialization, or bootstrap failure before frame budget can be measured."
	fi
	echo "G36_STATUS: INCOMPLETE (no frame budget telemetry)"
	exit 4
fi

# G36_PATCH_v55: Detect if guest explicitly disabled frame budget measurement.
# This distinguishes "guest crashed/silent" from "guest running but measurement opted-out".
# If disabled, we proceed with analysis but flag it clearly as incomplete/partial.
if (( FRAME_BUDGET_DISABLED )); then
	echo "G36_MEASUREMENT_OPTED_OUT: Guest running with frame budget measurement disabled."
	echo "G36_HINT_OPTED_OUT: Re-run with measurement enabled for full G36 automated analysis."
fi

# G36_PATCH_v61: Detect any GX API calls in OSREPORT to distinguish
# "guest rendering without budget telemetry" from "guest not rendering at all".
# This provides evidence that GX command buffer is active even if frame time
# OSReport calls are missing, helping diagnose missing telemetry vs silent renderer.
if (( FRAME_BUDGET_LOGS == 0 )) && [[ -n "$GUEST_RENDERER" ]]; then
	GX_API_HITS=$(grep -acE "GX_|GX_Call" "${LOG_FILES[@]}" 2>/dev/null | awk -F: '{sum+=$NF} END{print sum+0}')
	if (( GX_API_HITS > 0 )); then
		echo "G36_GX_ACTIVITY: ${GX_API_HITS} GX API markers detected in logs but zero frame budget samples."
		echo "G36_GX_ACTIVITY_HINT: Renderer is active and submitting GX commands. Frame budget OSReport calls are likely missing or placed in an unexecuted code path."
		# G36_PATCH_v73: Dump raw guest OSREPORT lines containing 'time' to identify
		# actual marker format being emitted. This breaks aide-review cycles where
		# probe regex expects 'frame time=<ms>' but guest uses different format.
		RAW_TIME_MARKERS=$(grep -aE "OSREPORT.*time=" "${LOG_FILES[@]}" 2>/dev/null | head -5 || true)
		if [[ -n "$RAW_TIME_MARKERS" ]]; then
			echo "G36_RAW_TIME_MARKERS: Guest emitted time-containing OSREPORT lines:"
			while IFS= read -r line; do
				echo "G36_RAW_TIME_MARKERS: ${line}"
			done <<< "$RAW_TIME_MARKERS"
		else
			echo "G36_RAW_TIME_MARKERS: No OSREPORT lines containing 'time=' found. Guest not emitting timing telemetry."
		fi
		# G36_PATCH_v79: Dump all Xash3D GameCube OSREPORT lines containing 'frame'
		# to provide exhaustive evidence of guest rendering markers. This catches
		# any frame-related markers even if they don't match probe regex patterns.
		# Limits to first 10 lines to avoid log flooding while providing sufficient evidence.
		RAW_FRAME_MARKERS=$(grep -aE "Xash3D GameCube.*frame" "${LOG_FILES[@]}" 2>/dev/null | head -10 || true)
		if [[ -n "$RAW_FRAME_MARKERS" ]]; then
			echo "G36_RAW_FRAME_MARKERS: Guest emitted frame-containing OSREPORT lines:"
			while IFS= read -r line; do
				echo "G36_RAW_FRAME_MARKERS: ${line}"
			done <<< "$RAW_FRAME_MARKERS"
		else
			echo "G36_RAW_FRAME_MARKERS: No Xash3D GameCube OSREPORT lines containing 'frame' found. Guest renderer may not be emitting frame markers at all."
		fi
	else
		echo "G36_GX_SILENT: No GX API markers found. Renderer may have initialized but not issued draw calls, or OSREPORT path is suppressed for GX calls."
	fi
fi

# G36_PATCH: Correlate renderer backend presence with frame budget measurement capability
# Helps distinguish "renderer not initialized" from "renderer initialized but not emitting telemetry"
if (( MAP_FOUND )) && (( FRAME_BUDGET_LOGS )) && [[ -z "$GUEST_RENDERER" ]]; then
	echo "G36_RENDERER_UNDETECTED: Frame budget logs present but renderer backend name not identified in guest logs."
	echo "G36_RENDERER_HINT: Ensure guest emits 'Xash3D GameCube: renderer initialized <backend>' during startup."
fi

# G36_PATCH_v118: Emit a single machine-parseable summary when renderer is active
# but budget measurement is missing. This provides downstream automation with a
# clear signal to distinguish "telemetry absent" from "telemetry present".
if [[ -n "$GUEST_RENDERER" ]] && (( FRAME_BUDGET_LOGS == 0 )) && (( GX_DRAWDONE_COUNT > 0 )); then
	echo "G36_MEASUREMENT_GAP: renderer=${GUEST_RENDERER} drawdone=${GX_DRAWDONE_COUNT} budget_markers=0 status=MEASUREMENT_PATH_MISSING"
fi

# G36_PATCH_v120: Report GX_DrawDone presence and count in post-probe analysis
# even when frame budget logs are present, to cross-validate GPU activity. This
# provides concrete evidence of whether the guest rendered frames independently
# of whether budget telemetry was captured.
if (( GX_DRAWDONE_COUNT > 0 )); then
	if (( FRAME_BUDGET_LOGS == 0 )); then
		echo "G36_DRAWDONE_POST_PROBE: ${GX_DRAWDONE_COUNT} GX_DrawDone calls detected with zero frame budget markers. Renderer is actively drawing but missing measurement OSReport."
	else
		echo "G36_DRAWDONE_POST_PROBE: ${GX_DRAWDONE_COUNT} GX_DrawDone calls detected alongside ${FRAME_COUNT} frame budget samples."
		if (( FRAME_COUNT > 0 )) && (( GX_DRAWDONE_COUNT > FRAME_COUNT * 3 )); then
			echo "G36_DRAWDONE_EXCESS: DrawDone count (${GX_DRAWDONE_COUNT}) greatly exceeds budget samples (${FRAME_COUNT}). Possible double-render loop or measurement guard skipping frames."
		elif (( FRAME_COUNT > 0 )) && (( GX_DRAWDONE_COUNT < FRAME_COUNT / 2 )); then
			echo "G36_DRAWDONE_LAG: DrawDone count (${GX_DRAWDONE_COUNT}) is less than half budget samples (${FRAME_COUNT}). GPU may be lagging behind CPU submission."
		else
			echo "G36_DRAWDONE_BALANCED: DrawDone count (${GX_DRAWDONE_COUNT}) is proportional to budget samples (${FRAME_COUNT}). GPU activity is consistent."
		fi
	fi
fi


# G36: Emit explicit measurement baseline marker so downstream tooling can
# distinguish "telemetry absent" from "telemetry present but failing"
# Include compile-time low-memory-mode context to correlate frame budget with build configuration
# (--low-memory-mode=2 sets MAX_MODELS=512, MAX_SOUNDS=512, etc. per GAMECUBE_MEMORY_BUDGET.md)
LC_LOWMEM="${LC_LOWMEM_MODE:-none}"
echo "G36_BASELINE: frame_budget_logs=${FRAME_BUDGET_LOGS} frame_samples_available=unknown renderer=${GUEST_RENDERER:-undetected} runtime_lowmem=${GC_LOWMEM_MODE:-none} compile_lowmem=${LC_LOWMEM} timeout=${TIMEOUT_SEC}s first_frame_offset=${G36_FIRST_FRAME_TS:+$(( G36_FIRST_FRAME_TS - START_TS ))s} last_frame_offset=${G36_LAST_FRAME_TS:+$(( G36_LAST_FRAME_TS - START_TS ))s} first_render_offset=${G36_FIRST_RENDER_TS:+$(( G36_FIRST_RENDER_TS - START_TS ))s} init_to_render_gap=${G36_INIT_TO_RENDER_GAP:-N/A}s"

# G36: Explicitly look for guest-reported memory samples to correlate with frame budget
GC_MEM_SAMPLES=0
grep -aqE "GC_MemSample|mem stage=" "${LOG_FILES[@]}" && GC_MEM_SAMPLES=1

# G36_PATCH: Detect explicit frame budget measurement configuration
# Look for guest-reported configuration to validate measurement validity
FRAME_BUDGET_CONFIGURED=0
grep -aqsF "Xash3D GameCube: frame budget configured" "${LOG_FILES[@]}" && FRAME_BUDGET_CONFIGURED=1

# G36_PATCH: Cross-reference measurement initialization with configuration
# to distinguish "configured but init failed" from "not configured at all"
if (( FRAME_BUDGET_CONFIGURED )) && ! (( FRAME_BUDGET_INIT_OK )); then
	echo "G36_CONFIG_VS_INIT: Guest reported frame budget configured but initialization did not succeed."
	echo "G36_CONFIG_HINT: Check renderer code path between configuration and initialization for early returns or errors."
	# G36_PATCH_v44: Detect if guest emitted GX initialization markers but measurement
	# failed, to isolate measurement setup bugs from renderer startup bugs
	if grep -aqsF "GX_Init" "${LOG_FILES[@]}" && [[ -n "$GUEST_RENDERER" ]]; then
		echo "G36_GX_INIT_OK_BUT_MEAS_FAIL: GX_Init succeeded and renderer ${GUEST_RENDERER} active, but frame budget measurement failed. Focus on measurement timer setup."
	fi
fi

# G36: Detect explicit GX WaitVP/WaitVP sync markers to measure VI-wait impact on frame budget
GX_WAITVP_COUNT=0
GX_WAITVP_SAMPLES=0
GX_WAITVP_SUM=""
if grep -aqsF "GX_WAITVP" "${LOG_FILES[@]}"; then
	GX_WAITVP_COUNT=$(grep -acF "GX_WAITVP" "${LOG_FILES[@]}")
	# G36_PATCH_v20: Extract explicit GX_WAITVP duration samples to quantify VI-sync cost
	# Helps distinguish "waiting for VI" from "renderer stalled before VI"
	# Single-pass extraction: count samples and sum durations together
	eval "$(grep -aoE 'GX_WAITVP[= ]+[0-9]+(\.[0-9]+)?' "${LOG_FILES[@]}" 2>/dev/null | \
		grep -oE '[0-9]+(\.[0-9]+)?$' | awk '
		{
			count++;
			sum += $1;
		}
		END {
			printf "GX_WAITVP_SAMPLES=%d\n", count+0;
			if (count > 0) printf "GX_WAITVP_SUM=%.6f\n", sum;
		}')"
fi

# G36: Track explicit GX renderer frame-start markers for CPU vs GPU correlation
FRAME_RENDER_LOGS=0
grep -aqE "Xash3D GameCube: render frame" "${LOG_FILES[@]}" && FRAME_RENDER_LOGS=1

# G36: Detect explicit GX FIFO stall or overflow markers (hardware-bound evidence)
GX_FIFO_STALLS=0
GX_FIFO_OVERRUNS=0
if grep -aqsF "GX_FIFO_STALL" "${LOG_FILES[@]}"; then
	GX_FIFO_STALLS=$(grep -acF "GX_FIFO_STALL" "${LOG_FILES[@]}")
fi
# G36_PATCH_v51: Detect GX FIFO overrun/overflow markers to distinguish
# "CPU too slow" (stalls) from "CPU too fast" (overruns) in command submission.
# FIFO overruns indicate the CPU submitted commands faster than GX hardware consumed them.
if grep -aqsE "GX_FIFO_(OVERRUN|OVERFLOW)" "${LOG_FILES[@]}"; then
	GX_FIFO_OVERRUNS=$(grep -acE "GX_FIFO_(OVERRUN|OVERFLOW)" "${LOG_FILES[@]}")
fi

# G36: Detect frame presentation hitch markers (CPU/GPU sync evidence)
FRAME_HITCHES=0
if grep -aqsF "Xash3D GameCube: frame hitch" "${LOG_FILES[@]}"; then
	FRAME_HITCHES=$(grep -acF "Xash3D GameCube: frame hitch" "${LOG_FILES[@]}")
fi

# G36: Detect explicit frame budget sample start/end markers for duration correlation
FRAME_BUDGET_SAMPLE_START=0
FRAME_BUDGET_SAMPLE_END=0
FRAME_BUDGET_SAMPLE_COUNT=0
if grep -aqsF "Xash3D GameCube: frame budget sample start" "${LOG_FILES[@]}"; then
	FRAME_BUDGET_SAMPLE_START=$(grep -acF "Xash3D GameCube: frame budget sample start" "${LOG_FILES[@]}")
fi
if grep -aqsF "Xash3D GameCube: frame budget sample end" "${LOG_FILES[@]}"; then
	FRAME_BUDGET_SAMPLE_END=$(grep -acF "Xash3D GameCube: frame budget sample end" "${LOG_FILES[@]}")
fi
# Use completed samples (ends) as the count when available; fall back to starts
if (( FRAME_BUDGET_SAMPLE_END > 0 )); then
	FRAME_BUDGET_SAMPLE_COUNT=$FRAME_BUDGET_SAMPLE_END
else
	FRAME_BUDGET_SAMPLE_COUNT=$FRAME_BUDGET_SAMPLE_START
fi

# G36_PATCH_v25: Detect per-stage frame budget violation markers to pinpoint
# which rendering stage (lightmaps, geometry, state changes, etc.) causes budget overruns.
# Helps distinguish "one big slow stage" from "many small slow stages" for targeted optimization.
FRAME_BUDGET_VIOLATION_STAGES=""
FRAME_BUDGET_STAGE_HITS=""
if grep -aqsF "Xash3D GameCube: frame budget violation stage=" "${LOG_FILES[@]}"; then
	FRAME_BUDGET_VIOLATION_STAGES=$(grep -aoE 'Xash3D GameCube: frame budget violation stage=[a-zA-Z_0-9_]+' "${LOG_FILES[@]}" 2>/dev/null | \
		grep -oE 'stage=[a-zA-Z_0-9_]+' | sed 's/stage=//' | sort | uniq -c | sort -rn | \
		awk '{printf "%s:%d ", $2, $1}' | sed 's/ $//')
	FRAME_BUDGET_STAGE_HITS=$(grep -acF "Xash3D GameCube: frame budget violation stage=" "${LOG_FILES[@]}")
fi

# G36: Detect CPU/GX time split markers for CPU vs GPU bottleneck diagnosis
FRAME_CPU_TIME_SAMPLES=0
FRAME_GX_TIME_SAMPLES=0
if grep -aqsE "Xash3D GameCube: (frame |render )?cpu_time=" "${LOG_FILES[@]}"; then
	FRAME_CPU_TIME_SAMPLES=$(grep -acE "Xash3D GameCube: (frame |render )?cpu_time=" "${LOG_FILES[@]}")
fi
if grep -aqsE "Xash3D GameCube: (frame |render )?gx_time=" "${LOG_FILES[@]}"; then
	FRAME_GX_TIME_SAMPLES=$(grep -acE "Xash3D GameCube: (frame |render )?gx_time=" "${LOG_FILES[@]}")
fi

# G36: Extract and analyze CPU vs GX time split if both markers are present
FRAME_CPU_AVG=""
FRAME_GX_AVG=""
if (( FRAME_CPU_TIME_SAMPLES > 0 )) && (( FRAME_GX_TIME_SAMPLES > 0 )); then
	# G36_PATCH_v2: Use relaxed pattern to catch cpu_time/gx_time regardless of prefix format
	eval "$(grep -aoE 'Xash3D GameCube: .* cpu_time=[0-9]+(\.[0-9]+)?' "${LOG_FILES[@]}" 2>/dev/null | \
		grep -oE 'cpu_time=[0-9]+(\.[0-9]+)?' | sed 's/cpu_time=//' | awk '
		{
			sum += $1; count++;
		}
		END {
			if (count > 0) printf "FRAME_CPU_AVG=%.3f\n", sum/count;
		}')"
	
	eval "$(grep -aoE 'Xash3D GameCube: .* gx_time=[0-9]+(\.[0-9]+)?' "${LOG_FILES[@]}" 2>/dev/null | \
		grep -oE 'gx_time=[0-9]+(\.[0-9]+)?' | sed 's/gx_time=//' | awk '
		{
			sum += $1; count++;
		}
		END {
			if (count > 0) printf "FRAME_GX_AVG=%.3f\n", sum/count;
		}')"
fi

# G36: Detect explicit frame deadline miss markers to separate late-frames from fast-frames
FRAME_DEADLINE_MISSES=0
if grep -aqsF "Xash3D GameCube: frame deadline miss" "${LOG_FILES[@]}"; then
	FRAME_DEADLINE_MISSES=$(grep -acF "Xash3D GameCube: frame deadline miss" "${LOG_FILES[@]}")
fi

# G36: Detect active renderer backend (GX vs software) for frame budget correlation
# Note: GUEST_RENDERER may already be set from probe loop detection
if [[ -z "$GUEST_RENDERER" ]]; then
	if grep -aqsF "Xash3D GameCube: renderer initialized" "${LOG_FILES[@]}"; then
		# G36_PATCH_v2: Relaxed pattern to catch renderer name after "initialized" with any spacing
		GUEST_RENDERER=$(grep -aoE 'Xash3D GameCube: renderer initialized[[:space:]]+[a-zA-Z_-]+' "${LOG_FILES[@]}" 2>/dev/null | tail -1 | grep -oE '[a-zA-Z_-]+$' || true)
	fi
fi

# G36_PATCH_v60: Detect renderer marker presence even if backend name couldn't be parsed.
# This distinguishes "renderer never started" from "renderer started but marker format unexpected".
if [[ -z "$GUEST_RENDERER" ]] && grep -aqsF "Xash3D GameCube: renderer initialized" "${LOG_FILES[@]}"; then
	echo "G36_RENDERER_MARKER_PRESENT: Renderer initialized marker found but backend name could not be parsed."
	echo "G36_RENDERER_HINT: Guest may emit marker without backend name, or marker format differs from expected pattern."
fi

# G36_PATCH_v77: Dump raw OSREPORT context around renderer initialization to diagnose
# missing frame budget markers. Shows 10 lines before/after "renderer initialized" to
# reveal what the guest is actually emitting, helping identify if budget markers are
# present but in unexpected format, or if the guest is silent about frame timing.
if grep -aqsF "Xash3D GameCube: renderer initialized" "${LOG_FILES[@]}" && (( FRAME_BUDGET_LOGS == 0 )); then
	# Merge logs and find context around renderer init marker
	RAW_CONTEXT=$(cat "${LOG_FILES[@]}" 2>/dev/null | grep -naF "Xash3D GameCube: renderer initialized" | head -1 | cut -d: -f1)
	if [[ -n "$RAW_CONTEXT" ]] && (( RAW_CONTEXT > 0 )); then
		START_LINE=$(( RAW_CONTEXT - 5 ))
		(( START_LINE < 1 )) && START_LINE=1
		END_LINE=$(( RAW_CONTEXT + 10 ))
		echo "G36_RAW_CONTEXT: Lines around renderer initialization (for marker format diagnosis):"
		cat "${LOG_FILES[@]}" 2>/dev/null | sed -n "${START_LINE},${END_LINE}p" | while IFS= read -r line; do
			echo "G36_RAW_CONTEXT: ${line}"
		done
	fi
fi

# G36_PATCH_v85: Dump last OSREPORT lines near GX_DrawDone to diagnose missing
# frame budget markers. Shows what the guest is emitting at render completion,
# helping identify if budget measurement is missing, misformatted, or suppressed.
# This targets the specific evidence gap: "completing draw calls but missing frame budget".
if (( GX_DRAWDONE_COUNT > 0 )) && (( FRAME_BUDGET_LOGS == 0 )); then
	echo "G36_DRAWDONE_NO_BUDGET: ${GX_DRAWDONE_COUNT} GX_DrawDone markers found but zero frame budget samples. Inspecting render completion context..."
	RAW_DRAWDONE_CONTEXT=$(grep -naF "GX_DrawDone" "${LOG_FILES[@]}" 2>/dev/null | tail -1 | cut -d: -f1)
	if [[ -n "$RAW_DRAWDONE_CONTEXT" ]] && (( RAW_DRAWDONE_CONTEXT > 0 )); then
		START_LINE=$(( RAW_DRAWDONE_CONTEXT - 2 ))
		(( START_LINE < 1 )) && START_LINE=1
		END_LINE=$(( RAW_DRAWDONE_CONTEXT + 5 ))
		echo "G36_DRAWDONE_CONTEXT: Lines around last GX_DrawDone (for budget marker diagnosis):"
		cat "${LOG_FILES[@]}" 2>/dev/null | sed -n "${START_LINE},${END_LINE}p" | while IFS= read -r line; do
			echo "G36_DRAWDONE_CONTEXT: ${line}"
		done
		echo "G36_DRAWDONE_HINT: Frame budget OSReport should appear immediately after GX_DrawDone. If absent, insert measurement call in renderer main loop."
	fi
fi

# G36_PATCH_v90: When GX activity is confirmed but frame budget markers are completely
# absent, dump the final 20 OSREPORT/Xash3D log lines from the probe. This provides
# concrete evidence of what the guest is actually emitting at probe end, helping
# reviewers identify if the guest is silent, stuck in a loop, or emitting markers
# in an unexpected format. Fires only once when GX calls detected but no budget logs.
if (( FRAME_BUDGET_LOGS == 0 )) && [[ -n "$GUEST_RENDERER" ]]; then
	GX_API_HITS=$(grep -acE "GX_|GX_Call" "${LOG_FILES[@]}" 2>/dev/null | awk -F: '{sum+=$NF} END{print sum+0}')
	if (( GX_API_HITS > 10 )); then
		echo "G36_TAIL_OSREPORT: Dumping last 20 guest log lines (GX active, budget silent) for format diagnosis:"
		cat "${LOG_FILES[@]}" 2>/dev/null | grep -aE "OSREPORT|Xash3D GameCube" | tail -20 | while IFS= read -r line; do
			echo "G36_TAIL_OSREPORT: ${line}"
		done
		echo "G36_TAIL_HINT: Inspect above lines for frame timing evidence. Guest may emit budget markers in unexpected format or location."
	fi
fi

# G36_PATCH_v27: Emit explicit renderer source diagnostic for traceability
if [[ -n "$GUEST_RENDERER" ]]; then
	echo "G36_RENDERER_SOURCE: backend=${GUEST_RENDERER} (detected post-probe or from loop)"
else
	if grep -aqsF "Xash3D GameCube: renderer initialized" "${LOG_FILES[@]}"; then
		echo "G36_RENDERER_SOURCE: backend=unknown (marker present but name unparseable)"
	else
		echo "G36_RENDERER_SOURCE: backend=unknown (no renderer initialization marker found in logs)"
	fi
fi

# G36: Detect explicit guest-reported frame count for sample completeness validation
GUEST_REPORTED_FRAME_COUNT=0
if grep -aqsE "Xash3D GameCube: total_frames_rendered=" "${LOG_FILES[@]}"; then
	# G36_PATCH_v2: Use word boundary to robustly extract numeric frame count
	GUEST_REPORTED_FRAME_COUNT=$(grep -aoE 'Xash3D GameCube: total_frames_rendered=[0-9]+' "${LOG_FILES[@]}" | tail -1 | grep -oE '[0-9]+' || echo "0")
fi

# G36: Detect software surface cache override (known GC memory/perf knob)
SW_SURFCACHE_OVERRIDE=""
if grep -aqsF "sw_surfcacheoverride" "${LOG_FILES[@]}"; then
	SW_SURFCACHE_OVERRIDE=$(grep -aoE 'sw_surfcacheoverride[= ]+[0-9]+' "${LOG_FILES[@]}" | tail -1 | grep -oE '[0-9]+$' || echo "unknown")
fi

# G36: Detect explicit low-memory mode flags (-gcmap, -gclowmem) for budget context
GC_LOWMEM_MODE=""
if grep -aqsF "-gcmap" "${LOG_FILES[@]}"; then
	GC_LOWMEM_MODE="gcmap"
elif grep -aqsF "-gclowmem" "${LOG_FILES[@]}"; then
	GC_LOWMEM_MODE="gclowmem"
fi

# G36: Detect client entity cap reduction (evidence of -gcmap mode active)
CLIENT_ENTITY_CAP=""
if grep -aqsF "num_client_entities" "${LOG_FILES[@]}"; then
	CLIENT_ENTITY_CAP=$(grep -aoE 'num_client_entities[= ]+[0-9]+' "${LOG_FILES[@]}" | tail -1 | grep -oE '[0-9]+$' || echo "unknown")
fi

# G36: Detect explicit GX command list flush markers for GPU pipeline stall diagnosis
GX_FLUSH_MARKERS=0
if grep -aqsF "GXFlushInvalidate" "${LOG_FILES[@]}"; then
	GX_FLUSH_MARKERS=$(grep -acF "GXFlushInvalidate" "${LOG_FILES[@]}")
fi

# G36_PATCH: Detect explicit GX_DrawDone markers to correlate GPU command
# completion with frame budget. Helps distinguish "CPU submission stalls"
# from "GPU processing stalls" in frame budget violations.
# Note: GX_DRAWDONE_COUNT may already be populated from probe loop if renderer
# was detected early; only update if we have a fresh count from full logs.
if grep -aqsF "GX_DrawDone" "${LOG_FILES[@]}"; then
	GX_DRAWDONE_COUNT=$(grep -acF "GX_DrawDone" "${LOG_FILES[@]}" 2>/dev/null | awk -F: '{sum+=$NF} END{print sum+0}')
fi

# G36_PATCH_v91: Detect GX_Flush without corresponding GX_DrawDone to diagnose
# command buffer submission issues. Flush without DrawDone suggests incomplete
# frame presentation or early-return before command submission.
GX_FLUSH_COUNT=0
GX_FLUSH_WITHOUT_DRAWDONE=0
if grep -aqsF "GX_Flush" "${LOG_FILES[@]}"; then
	GX_FLUSH_COUNT=$(grep -acF "GX_Flush" "${LOG_FILES[@]}")
	if (( GX_FLUSH_COUNT > 0 )) && (( GX_DRAWDONE_COUNT == 0 )); then
		GX_FLUSH_WITHOUT_DRAWDONE=1
		echo "G36_FLUSH_NO_DRAWDONE: ${GX_FLUSH_COUNT} GX_Flush markers found but zero GX_DrawDone. Command buffer may not be completing frames."
		echo "G36_FLUSH_HINT: Check for missing GX_DrawDone after GX_Flush, or early-return paths that skip command submission."
	fi
fi

# G36_PATCH_v94: Detect ratio of GX_Flush to GX_DrawDone to diagnose incomplete
# frame submission. Each frame should have exactly one GX_Flush followed by one
# GX_DrawDone. Mismatched ratios indicate frames being flushed but not drawn, or
# multiple flushes per frame (state reset without presentation).
if (( GX_FLUSH_COUNT > 0 )) && (( GX_DRAWDONE_COUNT > 0 )); then
	if (( GX_FLUSH_COUNT > GX_DRAWDONE_COUNT * 2 )); then
		echo "G36_FLUSH_DRAWDONE_RATIO: ${GX_FLUSH_COUNT} GX_Flush vs ${GX_DRAWDONE_COUNT} GX_DrawDone. Excessive flushes suggest state resets without frame submission."
		echo "G36_FLUSH_DRAWDONE_HINT: Renderer may be flushing command buffer multiple times per frame or clearing without drawing. Check render loop structure."
	elif (( GX_DRAWDONE_COUNT > GX_FLUSH_COUNT * 2 )); then
		echo "G36_FLUSH_DRAWDONE_RATIO: ${GX_FLUSH_COUNT} GX_Flush vs ${GX_DRAWDONE_COUNT} GX_DrawDone. Excessive DrawDone calls suggest missing flush or early command submission."
		echo "G36_FLUSH_DRAWDONE_HINT: Check for missing GX_Flush before GX_DrawDone or command buffer being submitted prematurely."
	else
		echo "G36_FLUSH_DRAWDONE_BALANCED: GX_Flush (${GX_FLUSH_COUNT}) and GX_DrawDone (${GX_DRAWDONE_COUNT}) counts are proportional. Command submission appears healthy."
	fi
fi

# G36_PATCH_v83: Detect explicit frame-render-complete markers to definitively
# prove the guest reached successful frame presentation. Distinguishes "renderer
# initialized" from "renderer actually drew and presented frames". This provides
# unambiguous evidence that the rendering pipeline is functional, breaking
# aide-review cycles where init markers exist but no proof of actual rendering.
FRAME_RENDER_COMPLETE_COUNT=0
FRAME_RENDER_COMPLETE_FIRST_TS=""
if grep -aqsF "Xash3D GameCube: frame render complete" "${LOG_FILES[@]}"; then
	FRAME_RENDER_COMPLETE_COUNT=$(grep -acF "Xash3D GameCube: frame render complete" "${LOG_FILES[@]}")
	# Extract timestamp from first render complete marker for init-to-first-frame latency
	FRAME_RENDER_COMPLETE_FIRST_TS=$(grep -aoF "Xash3D GameCube: frame render complete" "${LOG_FILES[@]}" 2>/dev/null | head -1 || true)
fi

# G36: Extract explicit GC_MemSample high-water marks for memory-pressure correlation
GC_MEM_PEAK_TOTAL=""
GC_MEM_PEAK_DELTA=""
GC_MEM_PEAK_STAGE=""
GC_MEM_SAMPLE_COUNT=0
if (( GC_MEM_SAMPLES )); then
	# Extract the highest recorded total memory usage from mem stage samples
	eval "$(grep -aE 'mem stage=' "${LOG_FILES[@]}" 2>/dev/null | \
		awk -F'[= ]' '{
			stage=""; total=0; delta=0;
			for(i=1;i<=NF;i++) {
				if($i=="stage" && $(i+1)!="") stage=$(i+1);
				if($i=="total" && $(i+1)+0 > 0) total=$(i+1)+0;
				if($i=="delta" && $(i+1)+0 > 0) delta=$(i+1)+0;
			}
			if(total > max_total) {
				max_total = total;
				max_delta = delta;
				max_stage = stage;
			}
			sample_count++;
		}
		END {
			if(max_total > 0) {
				printf "GC_MEM_PEAK_TOTAL=%.1f\n", max_total;
				printf "GC_MEM_PEAK_DELTA=%.1f\n", max_delta;
				printf "GC_MEM_PEAK_STAGE=%s\n", max_stage;
			}
			printf "GC_MEM_SAMPLE_COUNT=%d\n", sample_count+0;
		}' || true)"
fi

# Extract frame budget statistics for G36 measurement
# Restrict to engine-reported frame timing markers to avoid false matches
TARGET_FRAME_TIME=16.66

# G36_PATCH_v30: Detect guest-reported target frame time to validate budget expectations.
# GameCube may target a different refresh rate (e.g., 50fps PAL = 20.00ms) than NTSC 60fps.
# This prevents false budget violations when the guest is correctly targeting a lower framerate.
GUEST_TARGET_FRAME_TIME=""
if grep -aqsE "Xash3D GameCube: target frame time=" "${LOG_FILES[@]}"; then
	GUEST_TARGET_FRAME_TIME=$(grep -aoE 'Xash3D GameCube: target frame time=[0-9]+(\.[0-9]+)?' "${LOG_FILES[@]}" 2>/dev/null | \
		tail -1 | grep -oE '[0-9]+(\.[0-9]+)?$' || true)
fi

# G36_PATCH_v56: Detect explicit measurement initialization failure to distinguish
# "renderer working but measurement failed" from "renderer not working".
# This provides earlier feedback on whether the telemetry infrastructure is healthy.
if (( FRAME_BUDGET_INIT_FAIL )); then
	echo "G36_TELEMETRY_FAIL: Frame budget measurement subsystem failed to initialize in guest."
	echo "G36_TELEMETRY_HINT: Check for GX_Init failures, timer setup issues, or OSReport pathing errors before relying on budget data."
fi

# G36_PATCH_v48: Report pre-render cold-start memory baseline to correlate
# memory pressure with first-frame budget violations. Helps distinguish
# "cold-start alloc overhead" from "steady-state render cost".
if [[ -n "${G36_COLD_START_MEM_TOTAL:-}" ]]; then
	echo "G36_COLD_START_MEM: Pre-render peak memory=${G36_COLD_START_MEM_TOTAL}MiB at stage='${G36_COLD_START_MEM_STAGE}'."
	if awk "BEGIN {exit !(${G36_COLD_START_MEM_TOTAL:-0} > 6.0)}" 2>/dev/null; then
		echo "G36_COLD_START_MEM_WARN: Pre-render memory >6MiB. Cold-start allocations may pressure zone pool, impacting first-frame budget."
	fi
fi

# G36_PATCH_v82: Report time-to-first-render gap as measurable init overhead
if [[ -n "${G36_INIT_TO_RENDER_GAP:-}" ]] && (( G36_INIT_TO_RENDER_GAP > 0 )); then
	echo "G36_INIT_RENDER_GAP: Renderer init to first render frame=${G36_INIT_TO_RENDER_GAP}s."
	if (( G36_INIT_TO_RENDER_GAP > 3 )); then
		echo "G36_INIT_RENDER_GAP_WARN: Init-to-render gap >3s. Renderer started but delayed before first frame. Check for blocking setup or missing pre-cache."
	fi
elif [[ -z "${G36_FIRST_RENDER_TS:-}" ]]; then
	echo "G36_INIT_RENDER_GAP: No first-render marker detected. Renderer may not have reached main draw loop."
fi

# G36_PATCH_v31: Apply guest-reported target frame time to override default budget
# if the guest explicitly reports a different target (e.g., PAL 50fps = 20ms).
# This must happen before any frame time analysis to ensure correct budget thresholds.
if [[ -n "$GUEST_TARGET_FRAME_TIME" ]] && awk "BEGIN {exit !(${GUEST_TARGET_FRAME_TIME:-0} > 0.0)}"; then
	TARGET_FRAME_TIME="$GUEST_TARGET_FRAME_TIME"
	echo "G36_TARGET_APPLIED: Overriding default 16.66ms budget with guest-reported ${GUEST_TARGET_FRAME_TIME}ms."
fi
FRAME_TIMES=()
FRAME_DROP_COUNT=0
FRAME_DROP_LOGS=0
FRAME_STALL_COUNT=0
FRAME_STALL_LOGS=0
FRAME_BUDGET_PASSED=0

# G36_PATCH_v3: Track strict vs relaxed pattern matches to diagnose parse filter issues
FRAME_TIMES_STRICT=0
FRAME_TIMES_RELAXED=0

if (( FRAME_BUDGET_LOGS )); then
	# G36_PATCH_v9: Capture per-frame GX wait time to diagnose VI-sync bottlenecks.
	# This allows correlating CPU/GX split with explicit vertical sync stalls.
	GX_WAIT_TIME_SAMPLES=0
	GX_WAIT_TIME_AVG=""
	if grep -aqsE 'Xash3D GameCube: (frame |render )?gx_wait_time=' "${LOG_FILES[@]}"; then
		GX_WAIT_TIME_SAMPLES=$(grep -acE 'Xash3D GameCube: (frame |render )?gx_wait_time=' "${LOG_FILES[@]}")
		# G36_PATCH_v24: Compute average gx_wait_time to quantify VI-sync overhead
		# Helps distinguish "waiting for VI" from "renderer stalled before VI"
		eval "$(grep -aoE 'Xash3D GameCube: .* gx_wait_time=[0-9]+(\.[0-9]+)?' "${LOG_FILES[@]}" 2>/dev/null | \
			grep -oE 'gx_wait_time=[0-9]+(\.[0-9]+)?' | sed 's/gx_wait_time=//' | awk '
			{
				sum += $1; count++;
			}
			END {
				if (count > 0) printf "GX_WAIT_TIME_AVG=%.3f\n", sum/count;
			}')"
	fi

	# G36_PATCH_v13: Single-pass unified extraction to eliminate "missing frames
	# between explicit sample" by using one deterministic grep pattern that matches
	# all known frame timing marker formats. Avoids double-counting from overlapping
	# primary/fallback regexes.
	
	# Extract 'time=' markers (primary budget metric)
	# Count actual matches (lines in -o output) rather than log lines to match relaxed count granularity
	FRAME_TIMES_STRICT=$(grep -aoE 'Xash3D GameCube: frame time=[0-9]+(\.[0-9]+)?ms?' "${LOG_FILES[@]}" 2>/dev/null | wc -l)
		
	# G36_PATCH_v99: Consolidated robust extraction for frame timing data.
	# Uses a single, permissive regex to capture any line containing 'time=' followed by a number,
	# optionally suffixed by 'ms'. This avoids fragmentation from multiple specialized patterns.
	# It covers standard Xash3D markers, OSREPORT-wrapped variants, and simplified formats.
	# Strips keys and suffixes to leave only the numeric value for consistent analysis.
	# Capture relaxed match count for parse diagnostics.
	# Compute count before extraction to avoid parsing overhead in the count step.
	FRAME_TIMES_RELAXED=$(grep -acE '(Xash3D GameCube:|OSREPORT )*(frame (render |budget )?(time|duration)|render (frame )?(time|duration)|frame (render )?complete time|[cg]pu_time|gx_time|time)=[0-9]+(\.[0-9]+)?ms?' "${LOG_FILES[@]}" 2>/dev/null | awk -F: '{sum+=$NF} END{print sum+0}')
	
	# Uses a single, permissive regex to capture any line containing 'time=' followed by a number,
	# optionally suffixed by 'ms'. This avoids fragmentation from multiple specialized patterns.
	# It covers standard Xash3D markers, OSREPORT-wrapped variants, and simplified formats.
	# Strips keys and suffixes to leave only the numeric value for consistent analysis.
	while IFS= read -r val; do
		[[ -n "$val" ]] && FRAME_TIMES+=("$val")
	done < <(grep -aoE '(Xash3D GameCube:|OSREPORT )*(frame (render |budget )?(time|duration)|render (frame )?(time|duration)|frame (render )?complete time|[cg]pu_time|gx_time|time)=[0-9]+(\.[0-9]+)?ms?' "${LOG_FILES[@]}" 2>/dev/null | \
		grep -oE '[0-9]+(\.[0-9]+)?ms?' | sed 's/ms$//; s/^[[:space:]]*//; s/[[:space:]]*$//')

	# G36_PATCH_v78: Set FRAME_COUNT after extraction so dedup and stats
	# logic have a valid count.
	FRAME_COUNT=${#FRAME_TIMES[@]}

	# G36_DEDUP_v1: Deduplicate FRAME_TIMES to prevent double-counting when
	# both 'time=' and 'duration=' markers are emitted for the same frame.
	# Use awk to remove consecutive duplicates while preserving temporal order.
	# This maintains the cold-start frame as the first entry for steady-state analysis.
	if (( FRAME_COUNT > 1 )); then
		PRE_DEDUP_COUNT=$FRAME_COUNT
		mapfile -t FRAME_TIMES < <(printf '%s\n' "${FRAME_TIMES[@]}" | awk 'prev != $0 {print; prev=$0}')
		FRAME_COUNT=${#FRAME_TIMES[@]}
		if (( FRAME_COUNT < PRE_DEDUP_COUNT )); then
			echo "G36_DEDUP: Removed duplicate frame samples. Count reduced from ${PRE_DEDUP_COUNT} to ${FRAME_COUNT}."
		fi
	fi

	# Check for dropped frame markers to correlate with jank
	grep -aqsF "Xash3D GameCube: frame dropped" "${LOG_FILES[@]}" && FRAME_DROP_LOGS=1
	if (( FRAME_DROP_LOGS )); then
		FRAME_DROP_COUNT=$(grep -acF "Xash3D GameCube: frame dropped" "${LOG_FILES[@]}")
	fi

	# Check for stall/block markers (frames exceeding 50ms threshold for micro-stutter)
	grep -aqsF "Xash3D GameCube: frame stall" "${LOG_FILES[@]}" && FRAME_STALL_LOGS=1
	if (( FRAME_STALL_LOGS )); then
		FRAME_STALL_COUNT=$(grep -acF "Xash3D GameCube: frame stall" "${LOG_FILES[@]}")
	fi
fi

# G36_PATCH: Detect explicit renderer frame-start markers to distinguish GX
# command submission from VI present. Helps classify whether frame budget
# violations are CPU submission stalls or GPU/VI presentation stalls.
# Moved here after FRAME_COUNT is populated to ensure accurate comparison.
if (( FRAME_RENDER_LOGS )) && (( FRAME_BUDGET_LOGS )) && (( FRAME_COUNT > 0 )); then
	# Count actual renderer frame markers to compare against extracted frame times
	FRAME_RENDER_MARKER_COUNT=0
	FRAME_RENDER_MARKER_COUNT=$(grep -acE "Xash3D GameCube: render frame" "${LOG_FILES[@]}" 2>/dev/null || echo "0")
	# Count frames where renderer marker appears but no frame time appears on
	# the same logical frame boundary (heuristic: renderer markers > frame times)
	if (( FRAME_RENDER_MARKER_COUNT > FRAME_COUNT )); then
		EXCESS_RENDER_MARKERS=$((FRAME_RENDER_MARKER_COUNT - FRAME_COUNT))
		echo "G36_RENDER_OVERSUB: ${EXCESS_RENDER_MARKERS} renderer markers without matching frame-time logs. Possible frame-drop or guest not flushing GX command buffer."
	fi
fi

# G36_PATCH_v52: Detect any guest OSREPORT containing "frame" to distinguish
# "guest silent about frames" from "guest emits frames but probe regex missed them".
# This provides a fast-path signal for reviewers when FRAME_BUDGET_LOGS=0 but
# the renderer appears to have started. Helps break aide-review cycles where
# measurement absence is ambiguous.
if ! (( FRAME_BUDGET_LOGS )) && [[ -n "$GUEST_RENDERER" ]]; then
	ANY_FRAME_OSREPORT=$(grep -ac "frame" "${LOG_FILES[@]}" 2>/dev/null || echo "0")
	if (( ANY_FRAME_OSREPORT > 0 )); then
		echo "G36_GUEST_FRAME_MENTION: Guest emitted ${ANY_FRAME_OSREPORT} log lines mentioning 'frame' but no frame budget markers matched probe regex."
		echo "G36_REGEX_HINT: Guest may be using a non-standard frame budget marker format. Inspect logs for OSREPORT lines containing 'frame' and time/duration values."
	fi
fi

FRAME_MIN=""
FRAME_MAX=""
FRAME_AVG=""
FRAME_MEDIAN=""
FRAME_P95=""
FRAME_STDDEV=""
FRAME_JANK=0
FRAME_FIRST=""
FRAME_STEADY_COUNT=0
FRAME_STEADY_AVG=""
FRAME_STEADY_P95=""
FRAME_STEADY_BUDGET_PASSED=0
# FRAME_COUNT is already set above after fallback extraction

# G36_PATCH_v14: Explicitly detect when budget logs are present but no frame
# times were extracted. This distinguishes "marker format mismatch" from
# "guest not emitting markers" and provides actionable parse diagnostics.
if (( FRAME_BUDGET_LOGS )) && (( FRAME_COUNT == 0 )); then
	echo "G36_PARSE_FAIL: Frame budget log markers detected but zero frame times extracted."
	echo "G36_PARSE_HINT: Guest marker format may not match probe regex. Check for 'Xash3D GameCube: (frame|render).*time=' patterns in logs."
	echo "G36_PARSE_HINT: Ensure guest emits numeric milliseconds, e.g., 'Xash3D GameCube: frame time=12.34ms'"
	# G36_PATCH_v15: Dump raw matching lines for manual regex debugging
	RAW_MATCH_COUNT=$(grep -aE "Xash3D GameCube: (frame (render |budget )?(time|duration)|render (frame )?(time|duration)|frame (render )?complete time|[cg]pu_time|gx_time)=[0-9]+" "${LOG_FILES[@]}" 2>/dev/null | wc -l)
	echo "G36_PARSE_DEBUG: raw_matching_lines=${RAW_MATCH_COUNT}"
	if (( RAW_MATCH_COUNT > 0 )); then
		echo "G36_PARSE_DEBUG: Raw matches present but parse filter rejected them. Check for trailing characters after numeric value."
	fi
	# G36_PATCH_v19: Dump first few unmatched frame-time-like lines for manual regex diagnosis
	# This helps distinguish "marker format mismatch" from "guest not emitting markers"
	UNMATCHED_LIKELY=$(grep -aE "Xash3D GameCube:.*time=" "${LOG_FILES[@]}" 2>/dev/null | \
		grep -vaE "(frame (render |budget )?(time|duration)|render (frame )?(time|duration)|frame (render )?complete time|[cg]pu_time|gx_time)=" | head -3)
	if [[ -n "$UNMATCHED_LIKELY" ]]; then
		echo "G36_PARSE_RAW_SAMPLE: Unmatched but likely frame-time markers (first 3 lines):"
		while IFS= read -r line; do
			echo "G36_PARSE_RAW_SAMPLE: ${line}"
		done <<< "$UNMATCHED_LIKELY"
	fi

	# G36_PATCH_v34: Dump first few raw frame-budget log lines to show exact guest format
	# when extraction yields zero samples. This provides direct visibility into what
	# the guest is emitting versus what the probe regex expects.
	if (( FRAME_BUDGET_LOGS )); then
		RAW_FRAME_LINES=$(grep -aE "Xash3D GameCube:.*frame.*(time|duration|budget)" "${LOG_FILES[@]}" 2>/dev/null | head -5)
		if [[ -n "$RAW_FRAME_LINES" ]]; then
			echo "G36_RAW_BUDGET_LINES: Raw frame budget log lines (first 5):"
			while IFS= read -r line; do
				echo "G36_RAW_BUDGET_LINES: ${line}"
			done <<< "$RAW_FRAME_LINES"
		fi
	fi
	# G36_PATCH_v22: Distinguish renderer-not-initialized from renderer-initialized-but-silent
	# Helps diagnose whether the guest reached a state where frame timing should be emitted
	if grep -aqsF "Xash3D GameCube: renderer initialized" "${LOG_FILES[@]}"; then
		echo "G36_PARSE_HINT: Renderer initialization marker found but no frame times. Guest likely rendered frames but failed to emit timing markers."
		echo "G36_PARSE_HINT: Verify renderer code path includes frame budget OSReport calls after GX_DrawDone."
	else
		echo "G36_PARSE_HINT: No renderer initialization marker found. Guest may have crashed or stalled before rendering began."
		echo "G36_PARSE_HINT: Check for guest errors, missing assets, or early bootstrap failures."
	fi
	# G36_PATCH_v23: Detect alternative time units (microseconds, seconds) as common
	# marker format mismatches that cause zero-sample extraction
	if grep -aqsE "Xash3D GameCube:.*time=[0-9]+(\.[0-9]+)?(us|μs|sec|s)$" "${LOG_FILES[@]}"; then
		echo "G36_PARSE_UNIT_MISMATCH: Detected frame time markers with non-millisecond units (us/sec/s). Probe expects 'ms' suffix or bare milliseconds."
		echo "G36_PARSE_HINT: Update guest to emit milliseconds or relax probe regex to handle multiple units."
	fi
	# G36_PATCH_v21: Emit explicit status for zero-sample case to prevent
	# downstream tooling from misinterpreting empty summary as success
	echo "G36_STATUS: FAIL (zero samples extracted, parse failure)"
fi

if (( FRAME_COUNT > 0 )); then
	# Use awk for robust float math and sorting without bc dependency
	# Separate cold-start (first frame) from steady-state for budget analysis
	eval "$(printf '%s\n' "${FRAME_TIMES[@]}" | awk -v target="$TARGET_FRAME_TIME" '
	BEGIN {
		min = 999999; max = 0; sum = 0; sum_sq = 0; count = 0; jank = 0;
		steady_sum = 0; steady_count = 0; steady_jank = 0;
	}
	{
		val = $1 + 0;
		if (count == 0) first = val;
		if (val < min) min = val;
		if (val > max) max = val;
		sum += val;
		sum_sq += val * val;
		count++;
		if (val > target) jank++;
		times[count] = val;
		
		# Steady-state: exclude first frame (cold-start)
		if (count > 1) {
			steady_count++;
			steady_sum += val;
			if (val > target) steady_jank++;
			steady_times[steady_count] = val;
		}
	}
	END {
		if (count == 0) exit;
		avg = sum / count;
		variance = (sum_sq / count) - (avg * avg);
		if (variance < 0) variance = 0;
		stddev = sqrt(variance);
		
		# Bubble sort for percentiles (small N)
		for (i = 1; i <= count; i++) {
			for (j = i + 1; j <= count; j++) {
				if (times[i] > times[j]) {
					tmp = times[i];
					times[i] = times[j];
					times[j] = tmp;
				}
			}
		}
		
		# Median Index
		if (count % 2 == 1) {
			median = times[int(count / 2) + 1];
		} else {
			median = (times[count / 2] + times[count / 2 + 1]) / 2.0;
		}
		
		# P95 Index (1-based, ceiling for worst-5-percentile)
		p95_idx = int(count * 0.95 + 0.9999);
		if (p95_idx < 1) p95_idx = 1;
		if (p95_idx > count) p95_idx = count;
		p95 = times[p95_idx];
		
		# Determine pass/fail based on P95 being within budget
		if (p95 <= target) {
			printf "FRAME_BUDGET_PASSED=1\n";
		} else {
			printf "FRAME_BUDGET_PASSED=0\n";
		}
		
		printf "FRAME_FIRST=%.3f\n", first;
		printf "FRAME_MIN=%.3f\n", min;
		printf "FRAME_MAX=%.3f\n", max;
		printf "FRAME_AVG=%.3f\n", avg;
		printf "FRAME_MEDIAN=%.3f\n", median;
		printf "FRAME_P95=%.3f\n", p95;
		printf "FRAME_STDDEV=%.3f\n", stddev;
		printf "FRAME_JANK=%d\n", jank;
		
		# Steady-state analysis (exclude cold-start first frame)
		if (steady_count > 0) {
			steady_avg = steady_sum / steady_count;
			
			# Sort steady_times for steady-state percentiles
			for (i = 1; i <= steady_count; i++) {
				for (j = i + 1; j <= steady_count; j++) {
					if (steady_times[i] > steady_times[j]) {
						tmp = steady_times[i];
						steady_times[i] = steady_times[j];
						steady_times[j] = tmp;
					}
				}
			}
			
			# Steady P95
			steady_p95_idx = int(steady_count * 0.95 + 0.9999);
			if (steady_p95_idx < 1) steady_p95_idx = 1;
			if (steady_p95_idx > steady_count) steady_p95_idx = steady_count;
			steady_p95 = steady_times[steady_p95_idx];
			
			if (steady_p95 <= target) {
				printf "FRAME_STEADY_BUDGET_PASSED=1\n";
			} else {
				printf "FRAME_STEADY_BUDGET_PASSED=0\n";
			}
			
			printf "FRAME_STEADY_COUNT=%d\n", steady_count;
			printf "FRAME_STEADY_AVG=%.3f\n", steady_avg;
			printf "FRAME_STEADY_P95=%.3f\n", steady_p95;
		}
	}'
	)"
fi

# G36: Detect frame timing jitter (Mean Absolute Deviation from average frame time)
# Moved here after FRAME_TIMES is populated for accurate measurement
# MAD provides a more stable measure of jitter than max-diff between consecutive frames
FRAME_TIMING_JITTER="0.00"
if (( FRAME_COUNT > 1 )); then
	FRAME_TIMING_JITTER=$(printf '%s\n' "${FRAME_TIMES[@]}" | awk '
	{
		val = $1 + 0;
		sum += val;
		times[NR] = val;
		count++;
	}
	END {
		if (count == 0) exit;
		avg = sum / count;
		sum_abs_dev = 0;
		for (i = 1; i <= count; i++) {
			diff = times[i] - avg;
			if (diff < 0) diff = -diff;
			sum_abs_dev += diff;
		}
		mad = sum_abs_dev / count;
		printf "%.2f", mad;
	}')
fi

# G36: Detect frame pacing inconsistency (consecutive frame delta variance)
# Measures how much consecutive frame intervals deviate from target, independent
# of absolute frame time. High delta variance indicates irregular scheduling
# even if average/budget passes.
FRAME_PACING_VARIANCE="0.00"
FRAME_PACING_MAX_DELTA="0.00"
if (( FRAME_COUNT > 2 )); then
	eval "$(printf '%s\n' "${FRAME_TIMES[@]}" | awk -v target="$TARGET_FRAME_TIME" '
	{
		times[NR] = $1 + 0;
		count++;
	}
	END {
		if (count < 2) exit;
		sum_delta_sq = 0;
		max_delta = 0;
		delta_count = 0;
		for (i = 2; i <= count; i++) {
			delta = times[i] - times[i-1];
			if (delta < 0) delta = -delta;
			if (delta > max_delta) max_delta = delta;
			sum_delta_sq += delta * delta;
			delta_count++;
		}
		if (delta_count == 0) exit;
		avg_delta = target; # Target is ideal pacing delta
		variance = (sum_delta_sq / delta_count) - (avg_delta * avg_delta);
		if (variance < 0) variance = 0;
		stddev = sqrt(variance);
		printf "FRAME_PACING_VARIANCE=%.2f\n", stddev;
		printf "FRAME_PACING_MAX_DELTA=%.2f\n", max_delta;
	}')"
fi

# G36_PATCH_v53: Detect warmup frames. Count initial frames that exceed budget
# before seeing 3 consecutive frames under budget. This defines a more accurate
# warmup period than just frame #1, allowing for longer initialization tails.
FRAME_WARMUP_COUNT=0
if (( FRAME_COUNT > 0 )); then
	FRAME_WARMUP_COUNT=$(printf '%s\n' "${FRAME_TIMES[@]}" | awk -v target="$TARGET_FRAME_TIME" '
	{
		val = $1 + 0;
		if (val > target) {
			streak = 0;
		} else {
			streak++;
			if (streak >= 3) {
				print found+0;
				exit;
			}
		}
		found++;
	}
	END {
		if (found > 0) print found+0;
		else print 0;
	}')
fi

# G36: Detect consecutive frame spikes (>2x budget) as evidence of allocation stalls
FRAME_SPIKE_EVENTS=0
FRAME_SPIKE_MAX_CONSEC=0
FRAME_WORST_TIME=""
FRAME_SEVERE_VIOLATIONS=0
if (( FRAME_COUNT > 0 )); then
	# Store raw output in a temp var to avoid clobbering a counter named FRAME_SPIKE_COUNT
	_FRAME_SPIKE_RAW=$(printf '%s\n' "${FRAME_TIMES[@]}" | awk -v target="$TARGET_FRAME_TIME" '
	{
		val = $1 + 0;
		times[NR] = val;
		count++;
	}
	END {
		if (count == 0) exit;
		spikes = 0;
		consecutive = 0;
		max_consecutive = 0;
		worst = 0;
		severe = 0;
		for (i = 1; i <= count; i++) {
			if (times[i] > worst) worst = times[i];
			if (times[i] > target * 2) {
				consecutive++;
				if (consecutive > max_consecutive) max_consecutive = consecutive;
				if (consecutive == 1) spikes++;
				severe++;
			} else {
				consecutive = 0;
			}
		}
		printf "%d:%d:%.3f:%d", spikes, max_consecutive, worst, severe;
	}')
	FRAME_SPIKE_EVENTS="${_FRAME_SPIKE_RAW%%:*}"
	_REST="${_FRAME_SPIKE_RAW#*:}"
	FRAME_SPIKE_MAX_CONSEC="${_REST%%:*}"
	_FRAME_REST2="${_REST#*:}"
	FRAME_WORST_TIME="${_FRAME_REST2%%:*}"
	FRAME_SEVERE_VIOLATIONS="${_FRAME_REST2##*:}"
fi

# G36: Detect frame time regression pattern (consecutive frames getting slower)
# This indicates building memory pressure or cache fragmentation
FRAME_REGRESSION_RUNS=0
FRAME_REGRESSION_MAX_LEN=0
if (( FRAME_COUNT >= 4 )); then
	eval "$(printf '%s\n' "${FRAME_TIMES[@]}" | awk '
	{
		times[NR] = $1 + 0;
		count++;
	}
	END {
		if (count < 3) exit;
		runs = 0;
		max_len = 0;
		current_len = 1;
		for (i = 3; i <= count; i++) {
			if (times[i] > times[i-1] && times[i-1] > times[i-2]) {
				current_len++;
				if (current_len > max_len) max_len = current_len;
			} else {
				if (current_len >= 3) runs++;
				current_len = 1;
			}
		}
		if (current_len >= 3) runs++;
		printf "FRAME_REGRESSION_RUNS=%d\n", runs;
		printf "FRAME_REGRESSION_MAX_LEN=%d\n", max_len;
	}')"
fi

# G36: Detect explicit guest-reported "frame budget sample end" with stage annotation
FRAME_BUDGET_STAGE_ANNOTATED=0
FRAME_BUDGET_STAGE_VIOLATIONS=0
FRAME_BUDGET_FAIL_STAGES=""
if grep -aqsF "Xash3D GameCube: frame budget sample end stage=" "${LOG_FILES[@]}"; then
	FRAME_BUDGET_STAGE_ANNOTATED=1
	# Count FAIL stages across all log files using awk to avoid grep -c multi-file quirks
	FRAME_BUDGET_STAGE_VIOLATIONS=$(cat "${LOG_FILES[@]}" 2>/dev/null | \
		grep -aoE 'Xash3D GameCube: frame budget sample end stage=[a-z_]+ budget=(PASS|FAIL)' | \
		awk '/budget=FAIL/{count++} END{print count+0}')
	# Extract unique stage names that failed for diagnostic correlation
	FRAME_BUDGET_FAIL_STAGES=$(cat "${LOG_FILES[@]}" 2>/dev/null | \
		grep -aoE 'Xash3D GameCube: frame budget sample end stage=[a-z_]+ budget=FAIL' | \
		grep -oE 'stage=[a-z_]+' | sed 's/stage=//' | sort -u | tr '\n' ',' | sed 's/,$//')
fi

# G36: Detect GC_MemSample delta spikes to correlate allocation bursts with frame budget violations
# Tracks frames where memory delta exceeded a threshold, indicating per-frame allocation pressure
GC_MEM_DELTA_SPIKES=0
GC_MEM_DELTA_SPIKE_THRESHOLD="100"  # KiB threshold for "spike"
if (( GC_MEM_SAMPLES )) && (( FRAME_COUNT > 0 )); then
	# Extract delta values from mem stage samples and count those exceeding threshold
	GC_MEM_DELTA_SPIKES=$(grep -aoE 'delta=[0-9.]+' "${LOG_FILES[@]}" 2>/dev/null | \
		grep -oE '[0-9.]+' | awk -v thresh="$GC_MEM_DELTA_SPIKE_THRESHOLD" '
		{
			val = $1 + 0;
			if (val > thresh) count++;
		}
		END { print count+0 }')
fi

# G36: Detect GC_MemFail markers to correlate OOM/pressure with frame budget failures
GC_MEMFAIL_COUNT=0
GC_MEMFAIL_POOL=""
if grep -aqsF "GC_MemFail" "${LOG_FILES[@]}"; then
	GC_MEMFAIL_COUNT=$(grep -acF "GC_MemFail" "${LOG_FILES[@]}")
	# Extract the pool name from the last GC_MemFail for diagnostic context
	GC_MEMFAIL_POOL=$(grep -aoE 'GC_MemFail.*pool=[a-zA-Z_]+' "${LOG_FILES[@]}" 2>/dev/null | \
		tail -1 | grep -oE 'pool=[a-zA-Z_]+' | sed 's/pool=//')
fi

if (( MAP_FOUND )) && (( INPUT_FOUND )); then
	if grep -aEiq 'Host_Error|Sys_Error|fatal error|guest.*(crash|abort)' "${LOG_FILES[@]}"; then
		echo "GUEST_FAILURE: Map load was observed, followed by a guest error."
		echo "Logs: $LOG_DIR"
		exit 3
	fi
	echo "MAP_READY: Xash3D loaded ${SMOKE_MAP} on GameCube with interactive input."
	if (( DIAGNOSTIC_MARKER_FOUND )); then
		echo "VISUAL_BLOCKER: Diagnostic marker (Red/Green checker) was reported visible. VI/XFB is likely working, but renderer content may be black or missing."
	fi
	if (( SAMPLED_NONBLACK_FOUND )); then
		echo "VISUAL_PROGRESS: Software renderer sampled non-black pixels. Content should be visible."
	else
		echo "VISUAL_NOTE: No non-black pixel samples detected in logs. Check for black screen or diagnostic marker."
	fi

	# G36: Correlate visual state with frame budget health for clearer measurement evidence
	if (( FRAME_BUDGET_LOGS )) && (( FRAME_COUNT > 0 )); then
		if ! (( FRAME_BUDGET_PASSED )); then
			if (( DIAGNOSTIC_MARKER_FOUND )) && ! (( SAMPLED_NONBLACK_FOUND )); then
				echo "G36_VISUAL_BUDGET_CORRELATION: Frame budget failed AND diagnostic marker visible without non-black content. Renderer likely not issuing draw calls or GX command buffer is stalling."
			fi
		else
			if (( DIAGNOSTIC_MARKER_FOUND )) && ! (( SAMPLED_NONBLACK_FOUND )); then
				echo "G36_VISUAL_BUDGET_CORRELATION: Frame budget passed but no non-black content with diagnostic marker visible. Issue is likely renderer initialization or GX shader/texture setup, not performance."
			fi
		fi
	fi

	# Report frame budget telemetry
	if (( FRAME_BUDGET_LOGS )); then
		echo "FRAME_BUDGET_STATS: samples=${FRAME_COUNT}"
		if (( FRAME_COUNT > 0 )); then
			echo "FRAME_FIRST: ${FRAME_FIRST}ms (cold-start/initial frame)"
			echo "FRAME_MIN: ${FRAME_MIN}ms"
			echo "FRAME_MAX: ${FRAME_MAX}ms"
			echo "FRAME_AVG: ${FRAME_AVG}ms"
			echo "FRAME_MEDIAN: ${FRAME_MEDIAN}ms"
			echo "FRAME_P95: ${FRAME_P95}ms"
			echo "FRAME_STDDEV: ${FRAME_STDDEV}ms"
			echo "FRAME_JANK: ${FRAME_JANK} frames exceeded ${TARGET_FRAME_TIME}ms"
			
			# Classify frame budget health for G36 measurement
			if (( FRAME_JANK > 0 )); then
				JANK_PCT=$(awk "BEGIN {printf \"%.1f\", ($FRAME_JANK / $FRAME_COUNT) * 100}")
				echo "PERFORMANCE_NOTE: Jank detected (${FRAME_JANK} frames, ${JANK_PCT}% over budget)."
				if awk "BEGIN {exit !($FRAME_MAX > $TARGET_FRAME_TIME)}"; then
					echo "PERFORMANCE_BLOCKER: Frame budget exceeded. Max=${FRAME_MAX}ms > ${TARGET_FRAME_TIME}ms (60fps target)."
				elif awk "BEGIN {exit !($FRAME_P95 > $TARGET_FRAME_TIME)}"; then
					echo "PERFORMANCE_NOTE: P95=${FRAME_P95}ms > ${TARGET_FRAME_TIME}ms. Target stable, but intermittent frames exceed budget."
				fi
			else
				echo "PERFORMANCE_OK: Frame budget telemetry present and within 60fps limits."
			fi
			
			# Additional stability heuristic: High stddev relative to avg indicates jitter
			if awk "BEGIN {exit !($FRAME_STDDEV > 3.0)}"; then
				echo "PERFORMANCE_JITTER: High frame-time variance detected (StdDev=${FRAME_STDDEV}ms). Rendering may appear stuttery despite meeting budget."
			fi
			
			# Calculate FPS from average frame time
			if awk "BEGIN {exit !($FRAME_AVG <= 0)}"; then
				: # Skip FPS calc if avg is zero
			else
				FRAME_FPS=$(awk "BEGIN {printf \"%.1f\", 1000.0 / $FRAME_AVG}")
				echo "FRAME_FPS: ${FRAME_FPS} FPS (from avg ${FRAME_AVG}ms)"
			fi
			
			# Explicit G36 Measurement Result (includes cold-start frame)
			if (( FRAME_BUDGET_PASSED )); then
				echo "G36_MEASUREMENT_PASS: Frame budget stable. P95=${FRAME_P95}ms <= ${TARGET_FRAME_TIME}ms."
			else
				echo "G36_MEASUREMENT_FAIL: Frame budget unstable. P95=${FRAME_P95}ms > ${TARGET_FRAME_TIME}ms."
			fi

			# G36: Steady-state measurement (excludes cold-start first frame)
			if (( FRAME_STEADY_COUNT > 0 )); then
				echo "G36_STEADY_STATE: samples=${FRAME_STEADY_COUNT} avg=${FRAME_STEADY_AVG}ms p95=${FRAME_STEADY_P95}ms"
				if (( FRAME_STEADY_BUDGET_PASSED )); then
					echo "G36_STEADY_PASS: Steady-state frame budget stable. P95=${FRAME_STEADY_P95}ms <= ${TARGET_FRAME_TIME}ms."
				else
					echo "G36_STEADY_FAIL: Steady-state frame budget unstable. P95=${FRAME_STEADY_P95}ms > ${TARGET_FRAME_TIME}ms."
					if (( FRAME_BUDGET_PASSED )); then
						echo "G36_NOTE: Overall budget passed but steady-state failed. Cold-start frame may be masking steady-state issues."
					fi
				fi
			fi

			# G36_PATCH_v50: Calculate frame time variability (Standard Deviation)
			# for steady-state frames to detect jitter that averages out in mean/P95.
			# High SD indicates inconsistent rendering, likely due to GC memory pressure
			# or irregular GX command submission.
			if (( FRAME_STEADY_COUNT > 1 )); then
				FRAME_STEADY_SD=$(printf '%s\n' "${FRAME_TIMES[@]:1}" | awk '
				{
					sum += $1; sum_sq += $1 * $1; count++;
				}
				END {
					if (count > 1) {
						mean = sum / count;
						var = (sum_sq / count) - (mean * mean);
						if (var < 0) var = 0;
						printf "%.3f", sqrt(var);
					} else {
						printf "0.000";
					}
				}')
				echo "G36_STEADY_JITTER: std_dev=${FRAME_STEADY_SD}ms (steady-state variability)"
				if awk "BEGIN {exit !(${FRAME_STEADY_SD} > 2.0)}" 2>/dev/null; then
					echo "G36_JITTER_WARN: Steady-state frame time variability (${FRAME_STEADY_SD}ms) is high (>2.0ms). Rendering is inconsistent even if average budget is met."
				fi
			fi
			
			# G36: Correlate memory samples with frame budget
			if (( GC_MEM_SAMPLES )); then
				echo "G36_MEM_CORRELATION: GC memory samples detected alongside frame budget telemetry."
				if [[ -n "$GC_MEM_PEAK_TOTAL" ]]; then
					echo "G36_MEM_PEAK: total=${GC_MEM_PEAK_TOTAL}MiB delta=${GC_MEM_PEAK_DELTA}MiB stage=${GC_MEM_PEAK_STAGE}"
					if awk "BEGIN {exit !($GC_MEM_PEAK_TOTAL > 16.0)}"; then
						echo "G36_MEM_PRESSURE: Peak memory usage (${GC_MEM_PEAK_TOTAL}MiB) exceeds 16MiB. Frame budget may be impacted by GC zone allocation overhead."
					fi
					# Correlate memory-heavy stages with frame budget health
					if ! (( FRAME_BUDGET_PASSED )); then
						case "$GC_MEM_PEAK_STAGE" in
							*bsp*)
								echo "G36_MEM_STAGE_HINT: Peak memory at BSP load stage; consider streaming or trimming BSP resident data to stabilize frame budget."
								;;
							*client*)
								echo "G36_MEM_STAGE_HINT: Peak memory at client init; consider reducing client entity counts or sound precache for this stage."
								;;
							*model*)
								echo "G36_MEM_STAGE_HINT: Peak memory at model load; consider capping studio texture residency or using lower-quality stubs."
								;;
							*)
								echo "G36_MEM_STAGE_HINT: Peak memory at '${GC_MEM_PEAK_STAGE}' stage; profile allocations during this stage for frame budget impact."
								;;
						esac
					fi
				fi
			else
				echo "G36_MEM_NOTE: No GC memory samples detected in this probe; frame budget is isolated from memory pressure evidence."
			fi

			# G36: Report software surface cache override setting
			if [[ -n "$SW_SURFCACHE_OVERRIDE" ]]; then
				echo "G36_SW_SURFCACHE: sw_surfcacheoverride=${SW_SURFCACHE_OVERRIDE} detected in probe logs."
				if awk "BEGIN {exit !($SW_SURFCACHE_OVERRIDE > 65536)}" 2>/dev/null; then
					echo "G36_SW_SURFCACHE_WARN: Surface cache exceeds GC_SURFACE_CACHE_MAX (64KiB). This may cause zone pressure or frame budget instability."
				fi
			fi

			# G36: Report low-memory mode context for frame budget interpretation
			if [[ -n "$GC_LOWMEM_MODE" ]]; then
				echo "G36_LOWMEM_MODE: Guest running in '${GC_LOWMEM_MODE}' mode. Frame budget reflects trimmed resource state."
				if [[ "$GC_LOWMEM_MODE" == "gcmap" ]] && [[ "$CLIENT_ENTITY_CAP" == "64" ]]; then
					echo "G36_LOWMEM_HINT: Client entities capped at 64 (smoke-mode). Steady-state budget may improve further with entity streaming."
				elif [[ "$GC_LOWMEM_MODE" == "gcmap" ]] && [[ "$CLIENT_ENTITY_CAP" != "64" ]]; then
					echo "G36_LOWMEM_NOTE: -gcmap detected but entity cap not at 64. Verify client initialization path."
				fi
			else
				echo "G36_FULLMODE: Guest running without explicit low-memory flags. Frame budget represents full-gameplay resource state."
			fi

			# G36: Correlate renderer frame-start markers with engine frame budget
			if (( FRAME_RENDER_LOGS )); then
				echo "G36_RENDER_CORRELATION: Renderer frame markers detected. CPU/GX timing alignment likely active."
			else
				echo "G36_RENDER_NOTE: No explicit renderer frame markers found; budget may include full frame or be CPU-only."
			fi

			# G36: Report GX FIFO stalls as hardware-bound evidence
			if (( GX_FIFO_STALLS > 0 )); then
				echo "G36_GX_HW_BOUND: ${GX_FIFO_STALLS} GX_FIFO_STALL markers detected. Frame budget likely limited by GX hardware throughput."
			fi

			# G36_PATCH_v51: Report GX FIFO overruns as CPU-bound submission evidence
			if (( GX_FIFO_OVERRUNS > 0 )); then
				echo "G36_GX_FIFO_OVERRUN: ${GX_FIFO_OVERRUNS} GX_FIFO overrun/overflow markers detected. CPU command submission outpaced GX hardware consumption."
				if (( GX_FIFO_STALLS > 0 )); then
					echo "G36_GX_FIFO_MIXED: Both stalls (${GX_FIFO_STALLS}) and overruns (${GX_FIFO_OVERRUNS}) detected. CPU/GX pacing is irregular; consider batching or yielding."
				else
					echo "G36_GX_CPU_FAST: No stalls detected with overruns present. CPU is ahead of GPU; consider inserting explicit GX_Flush or yield points to stabilize pacing."
				fi
			fi

			# G36_PATCH: Report GX_DrawDone count to correlate GPU command completion
			# with frame budget. If DrawDone count << frame count, GPU may be bottlenecked.
			if (( GX_DRAWDONE_COUNT > 0 )); then
				echo "G36_GX_DRAWDONE: ${GX_DRAWDONE_COUNT} GX_DrawDone markers detected."
				if (( GX_DRAWDONE_COUNT < FRAME_COUNT )); then
					echo "G36_GX_DRAWDONE_LAG: DrawDone count (${GX_DRAWDONE_COUNT}) is less than frame samples (${FRAME_COUNT}). GPU command completion may be lagging behind frame submission."
				elif (( GX_DRAWDONE_COUNT == FRAME_COUNT )); then
					echo "G36_GX_DRAWDONE_STABLE: DrawDone count matches frame samples. GPU command completion appears synchronous with frame budget."
				fi
			fi

			# G36: Report GX WaitVP (VI sync) count as evidence of CPU yielding to vertical sync
			if (( GX_WAITVP_COUNT > 0 )); then
				echo "G36_GX_WAITVP: ${GX_WAITVP_COUNT} GX_WAITVP markers detected. Frame budget includes explicit VI synchronization pauses."
				if (( GX_WAITVP_SAMPLES > 0 )); then
					GX_WAITVP_AVG=$(awk "BEGIN {printf \"%.3f\", ${GX_WAITVP_SUM} / ${GX_WAITVP_SAMPLES}}")
					echo "G36_GX_WAITVP_AVG: ${GX_WAITVP_AVG}ms average VI-wait time across ${GX_WAITVP_SAMPLES} samples."
					if awk "BEGIN {exit !(${GX_WAITVP_AVG} > 5.0)}" 2>/dev/null; then
						echo "G36_GX_WAITVP_WARN: Average VI-wait (${GX_WAITVP_AVG}ms) exceeds 5.0ms. Significant CPU time spent waiting for GPU/VI completion."
					fi
				fi
			fi

			# G36: Report correlation between memory sample stages and frame budget health
			if (( GC_MEM_SAMPLES )) && [[ -n "$GC_MEM_PEAK_STAGE" ]]; then
				case "$GC_MEM_PEAK_STAGE" in
					bsp|client*)
						echo "G36_MEM_PHASE: Peak memory at '${GC_MEM_PEAK_STAGE}' (cold-start phase). Frame budget violations may be map-load artifacts."
						;;
					textures|models|server*)
						echo "G36_MEM_PHASE: Peak memory at '${GC_MEM_PEAK_STAGE}' (content phase). Frame budget may reflect sustained allocation pressure."
						;;
					*)
						echo "G36_MEM_PHASE: Peak memory at '${GC_MEM_PEAK_STAGE}' stage."
						;;
				esac
			fi

			# G36: Report CPU vs GX time split for bottleneck diagnosis
			if [[ -n "$FRAME_CPU_AVG" ]] && [[ -n "$FRAME_GX_AVG" ]]; then
				echo "G36_CPU_GX_SPLIT: cpu_avg=${FRAME_CPU_AVG}ms gx_avg=${FRAME_GX_AVG}ms samples_cpu=${FRAME_CPU_TIME_SAMPLES} samples_gx=${FRAME_GX_TIME_SAMPLES}"
				if awk "BEGIN {exit !(${FRAME_CPU_AVG} > ${FRAME_GX_AVG} * 1.5)}" 2>/dev/null; then
					echo "G36_CPU_BOUND: CPU time dominates GX time (CPU=${FRAME_CPU_AVG}ms > 1.5x GX=${FRAME_GX_AVG}ms). Frame budget likely limited by CPU work."
				elif awk "BEGIN {exit !(${FRAME_GX_AVG} > ${FRAME_CPU_AVG} * 1.5)}" 2>/dev/null; then
					echo "G36_GPU_BOUND: GX time dominates CPU time (GX=${FRAME_GX_AVG}ms > 1.5x CPU=${FRAME_CPU_AVG}ms). Frame budget likely limited by GPU throughput."
				else
					echo "G36_BALANCED: CPU and GX times are comparable. Frame budget constrained by both subsystems."
				fi
			elif [[ -n "$FRAME_CPU_AVG" ]]; then
				echo "G36_CPU_ONLY: CPU time available (avg=${FRAME_CPU_AVG}ms, samples=${FRAME_CPU_TIME_SAMPLES}) but GX time missing. Guest not emitting gx_time markers; GPU bottleneck diagnosis unavailable."
			elif [[ -n "$FRAME_GX_AVG" ]]; then
				echo "G36_GX_ONLY: GX time available (avg=${FRAME_GX_AVG}ms, samples=${FRAME_GX_TIME_SAMPLES}) but CPU time missing. Guest not emitting cpu_time markers; CPU bottleneck diagnosis unavailable."
			fi

			# G36: Report frame presentation hitches (CPU/GPU sync gaps)
			if (( FRAME_HITCHES > 0 )); then
				echo "G36_GX_HITCHES: ${FRAME_HITCHES} frame hitch markers detected. Possible CPU/GPU sync or VI wait issues."
			fi

			# G36: Report explicit frame deadline misses (late-frame evidence)
			if (( FRAME_DEADLINE_MISSES > 0 )); then
				echo "G36_DEADLINE_MISSES: ${FRAME_DEADLINE_MISSES} frame deadline misses detected. Guest reported frames failing to meet budget deadline."
			fi

			# G36: Report frame budget sample consistency
			if (( FRAME_BUDGET_SAMPLE_COUNT > 0 )); then
				echo "G36_BUDGET_SAMPLES: ${FRAME_BUDGET_SAMPLE_COUNT} explicit budget samples recorded (start=${FRAME_BUDGET_SAMPLE_START} end=${FRAME_BUDGET_SAMPLE_END})."
				if (( FRAME_BUDGET_SAMPLE_END > 0 )) && (( FRAME_BUDGET_SAMPLE_START > FRAME_BUDGET_SAMPLE_END )); then
					echo "G36_BUDGET_NOTE: More sample-starts than sample-ends detected. Possible guest crash during measurement window."
				elif (( FRAME_BUDGET_SAMPLE_END > 0 )) && (( FRAME_BUDGET_SAMPLE_START < FRAME_BUDGET_SAMPLE_END )); then
					echo "G36_BUDGET_NOTE: More sample-ends than sample-starts detected. Measurement may have started before probe logging began."
				fi
				if (( FRAME_BUDGET_SAMPLE_COUNT > FRAME_COUNT )); then
					echo "G36_BUDGET_NOTE: More budget samples than frame-time logs. Some frames may have been skipped in timing telemetry."
				elif (( FRAME_BUDGET_SAMPLE_COUNT == FRAME_COUNT )); then
					echo "G36_BUDGET_STABLE: Frame-time logs match sample count exactly. Full-frame measurement achieved."
				elif (( FRAME_COUNT < 5 )); then
					echo "G36_BUDGET_SPARSE: Frame-time logs are sparse compared to samples. Consider increasing marker frequency or filtering noise."
				fi
			fi

			# G36_PATCH_v83: Report definitive frame-render-complete evidence
			# This is the strongest proof that rendering is actually happening
			if (( FRAME_RENDER_COMPLETE_COUNT > 0 )); then
				echo "G36_RENDER_PROVEN: ${FRAME_RENDER_COMPLETE_COUNT} explicit frame-render-complete markers detected. Rendering pipeline is definitively functional."
				if (( FRAME_RENDER_COMPLETE_COUNT >= FRAME_COUNT )); then
					echo "G36_RENDER_COMPLETE_MATCH: Render-complete count (${FRAME_RENDER_COMPLETE_COUNT}) >= frame-budget samples (${FRAME_COUNT}). Full frame telemetry confirmed."
				elif (( FRAME_RENDER_COMPLETE_COUNT < FRAME_COUNT )); then
					echo "G36_RENDER_COMPLETE_NOTE: Render-complete count (${FRAME_RENDER_COMPLETE_COUNT}) < frame-budget samples (${FRAME_COUNT}). Some budget samples may be from non-presented frames."
				fi
				if (( FRAME_RENDER_COMPLETE_COUNT > FRAME_COUNT * 2 )); then
					echo "G36_RENDER_COMPLETE_EXCESS: Render-complete count (${FRAME_RENDER_COMPLETE_COUNT}) is >2x frame-budget samples (${FRAME_COUNT}). Guest may be rendering frames not captured by budget telemetry."
				fi
			else
				echo "G36_RENDER_NOT_PROVEN: No explicit frame-render-complete markers found. Renderer init marker present but no definitive proof of successful frame presentation."
				echo "G36_RENDER_HINT: Add 'Xash3D GameCube: frame render complete' OSReport after each successful VI-present to prove rendering pipeline."
			fi

			# Report frame distribution percentiles for deeper analysis
			FRAME_P50="${FRAME_MEDIAN}"
			FRAME_P10=$(printf '%s\n' "${FRAME_TIMES[@]}" | sort -n | awk -v n="$FRAME_COUNT" 'NR==int(n*0.1+0.9999) {print; exit}')
			FRAME_P25=$(printf '%s\n' "${FRAME_TIMES[@]}" | sort -n | awk -v n="$FRAME_COUNT" 'NR==int(n*0.25+0.9999) {print; exit}')
			FRAME_P99=$(printf '%s\n' "${FRAME_TIMES[@]}" | sort -n | awk -v n="$FRAME_COUNT" 'NR==int(n*0.99+0.9999) {print; exit}')
			echo "FRAME_P10: ${FRAME_P10}ms"
			echo "FRAME_P25: ${FRAME_P25}ms"
			echo "FRAME_P50: ${FRAME_P50}ms"
			echo "FRAME_P99: ${FRAME_P99}ms"
			echo "FRAME_P95: ${FRAME_P95}ms"
		fi
		
		# G36_PATCH_v30: Report target frame time context for budget interpretation
		if [[ -n "$GUEST_TARGET_FRAME_TIME" ]]; then
			echo "G36_BUDGET_TARGET: ${GUEST_TARGET_FRAME_TIME}ms (guest-reported)"
		fi

		# G36 structured summary for automated tooling
		if (( FRAME_COUNT > 0 )); then
			# G36_PATCH_v3: Report parse pattern match diagnostics
			# Helps distinguish between "guest not emitting markers" vs "probe regex too strict"
			if (( FRAME_TIMES_STRICT > 0 )); then
				echo "G36_PARSE_STRICT: ${FRAME_TIMES_STRICT} frames matched strict 'frame time=' pattern."
			else
				echo "G36_PARSE_STRICT: 0 frames matched strict 'frame time=' pattern."
			fi

			# G36_PATCH_v10: Flag when GX wait time telemetry is missing despite
			# frame budget logging being active, to diagnose whether guest is
			# omitting VI-sync markers or whether the pattern needs relaxing.
			if (( FRAME_BUDGET_LOGS )) && (( GX_WAIT_TIME_SAMPLES == 0 )); then
				echo "G36_GX_WAIT_MISSING: Frame budget logs present but no gx_wait_time samples. Guest may not be emitting VI-sync timing markers."
			fi
			echo "G36_PARSE_RELAXED: ${FRAME_TIMES_RELAXED} frames matched relaxed '.* time=' pattern."
			if (( FRAME_TIMES_RELAXED > 0 )) && (( FRAME_TIMES_STRICT == 0 )); then
				echo "G36_PARSE_HINT: Guest using non-standard frame time marker format. Relax strict expectations or update guest to emit 'frame time=<ms>'."
			elif (( FRAME_TIMES_RELAXED < FRAME_TIMES_STRICT )); then
				echo "G36_PARSE_NOTE: Relaxed pattern captured fewer samples than strict; unexpected, verify guest marker format."
			fi

			# G36: Cross-validate probe-extracted frame count against guest-reported count
			# Helps distinguish parse failures from rendering stalls
			if (( GUEST_REPORTED_FRAME_COUNT > 0 )); then
				if (( FRAME_COUNT < GUEST_REPORTED_FRAME_COUNT )); then
					MISSING_FRAMES=$((GUEST_REPORTED_FRAME_COUNT - FRAME_COUNT))
					echo "G36_SAMPLE_LOSS: Probe extracted ${FRAME_COUNT} frames but guest reported ${GUEST_REPORTED_FRAME_COUNT} rendered. ${MISSING_FRAMES} frames missing from logs (parse filter too strict or log truncation)."
					echo "G36_STATUS: INCOMPLETE (sample loss detected)"
				elif (( FRAME_COUNT == GUEST_REPORTED_FRAME_COUNT )); then
					echo "G36_SAMPLE_COMPLETE: Probe frame count matches guest-reported count (${FRAME_COUNT}). Full telemetry captured."
				elif (( FRAME_COUNT > GUEST_REPORTED_FRAME_COUNT )); then
					echo "G36_SAMPLE_NOTE: Probe extracted more frames (${FRAME_COUNT}) than guest reported (${GUEST_REPORTED_FRAME_COUNT}). Guest counter may lag or be sampled at different interval."
				fi
			fi

			# G36: Cold-start vs steady-state violation concentration
			# Helps diagnose if budget failures are initialization artifacts or sustained issues
			if (( FRAME_JANK > 0 )) && (( FRAME_COUNT >= 2 )); then
				FRAME_COLD_START_JANK=$(printf '%s\n' "${FRAME_TIMES[@]}" | awk -v target="$TARGET_FRAME_TIME" '
				NR == 1 { if ($1+0 > target) jank++ }
				END { print jank+0 }')
				FRAME_STEADY_JANK=$((FRAME_JANK - FRAME_COLD_START_JANK))
				if (( FRAME_STEADY_JANK > 0 )); then
					echo "G36_JANK_DISTRIBUTION: cold_start=${FRAME_COLD_START_JANK} steady=${FRAME_STEADY_JANK} total=${FRAME_JANK}"
					if (( FRAME_COLD_START_JANK == FRAME_JANK )); then
						echo "G36_JANK_HINT: All jank concentrated in cold-start (first frame). Steady-state rendering is stable."
					elif (( FRAME_STEADY_JANK * 2 > FRAME_JANK )); then
						echo "G36_JANK_HINT: Majority of jank in steady-state frames. Renderer optimization needed for sustained performance."
					else
						echo "G36_JANK_HINT: Jank split between cold-start and steady-state. Investigate both initialization and per-frame work."
					fi
				fi
			fi

			# G36: Coefficient of variation (CV = stddev/avg) for normalized variance
			# CV > 0.25 indicates high relative variance even if absolute stddev is modest
			# Calculate this BEFORE G36_SUMMARY so it's available in the summary line
			FRAME_CV="0.000"
			if awk "BEGIN {exit !(${FRAME_AVG} > 0.001)}" 2>/dev/null; then
				FRAME_CV=$(awk "BEGIN {printf \"%.3f\", ${FRAME_STDDEV} / ${FRAME_AVG}}")
				if awk "BEGIN {exit !(${FRAME_CV} > 0.25)}" 2>/dev/null; then
					echo "G36_CV_WARN: Frame budget CV=${FRAME_CV} exceeds 0.25. High relative variance detected; consider profiling allocation spikes or GPU stall patterns."
				fi
			fi

			# G36: Flag low sample count as insufficient for statistical confidence
			if (( FRAME_COUNT < 3 )); then
				echo "G36_SAMPLE_BLOCKER: Only ${FRAME_COUNT} frame samples collected. Insufficient for any budget analysis. Map may have loaded but rendering stalled immediately."
				echo "G36_STATUS: INCOMPLETE (insufficient samples)"
			elif (( FRAME_COUNT < 5 )); then
				echo "G36_SAMPLE_WARN: Only ${FRAME_COUNT} frame samples collected. Insufficient for reliable P95 budget analysis."
				echo "G36_STATUS: WEAK (low sample count)"
			elif (( FRAME_COUNT < 10 )); then
				echo "G36_SAMPLE_NOTE: ${FRAME_COUNT} frame samples collected. Moderate confidence in budget measurement."
			else
				echo "G36_SAMPLE_OK: ${FRAME_COUNT} frame samples collected. High confidence in budget measurement."
			fi

			# G36_PATCH_v26: Compute frame time stability score (0-100) for automated
			# regression detection. Score penalizes high P95, high jank rate, and high jitter.
			# This provides a single metric for comparing across probe runs.
			FRAME_STABILITY_SCORE=100
			if (( FRAME_COUNT > 0 )); then
				FRAME_STABILITY_SCORE=$(awk -v p95="$FRAME_P95" -v target="$TARGET_FRAME_TIME" \
					-v jank="$FRAME_JANK" -v count="$FRAME_COUNT" \
					-v jitter="$FRAME_TIMING_JITTER" -v cv="$FRAME_CV" '
				BEGIN {
					score = 100;
					# Penalty for P95 exceeding target (up to -40 points)
					if (p95 > target) {
						ratio = p95 / target;
						penalty = (ratio - 1) * 40;
						if (penalty > 40) penalty = 40;
						score -= penalty;
					}
					# Penalty for jank rate (up to -30 points)
					if (count > 0) {
						jank_rate = jank / count;
						score -= jank_rate * 30;
					}
					# Penalty for high jitter (up to -20 points)
					if (jitter > 1.0) {
						score -= (jitter - 1.0) * 10;
						if (score < 30) score = 30;
					}
					# Penalty for high CV (up to -10 points)
					if (cv > 0.2) {
						score -= (cv - 0.2) * 50;
						if (score < 20) score = 20;
					}
					if (score < 0) score = 0;
					printf "%.0f", score;
				}')
				echo "G36_STABILITY_SCORE: ${FRAME_STABILITY_SCORE}/100 (lower=more unstable)"
				if (( FRAME_STABILITY_SCORE < 50 )); then
					echo "G36_STABILITY_WARN: Low stability score (${FRAME_STABILITY_SCORE}). Frame budget is highly unstable; prioritize optimization."
				elif (( FRAME_STABILITY_SCORE < 75 )); then
					echo "G36_STABILITY_NOTE: Moderate stability score (${FRAME_STABILITY_SCORE}). Frame budget needs tuning for consistent 60fps."
				else
					echo "G36_STABILITY_OK: Good stability score (${FRAME_STABILITY_SCORE}). Frame budget is approaching target consistency."
				fi
			fi

			# G36: Classify failure mode: cold-start only vs sustained
			# Helps distinguish renderer initialization overhead from steady-state rendering issues
			FRAME_FAILURE_MODE="unknown"
			if ! (( FRAME_BUDGET_PASSED )); then
				# Use warmup count to refine cold-start classification.
				# If warmup frames exist and subsequent frames pass, it's a warmup issue.
				if (( FRAME_WARMUP_COUNT > 0 )) && (( FRAME_COUNT > FRAME_WARMUP_COUNT + 3 )) && (( FRAME_STEADY_BUDGET_PASSED )); then
					FRAME_FAILURE_MODE="cold_start"
					echo "G36_FAILURE_MODE: cold_start (${FRAME_WARMUP_COUNT} warmup frames, steady-state stable)"
				elif (( FRAME_STEADY_BUDGET_PASSED )) && (( FRAME_COUNT >= 2 )); then
					FRAME_FAILURE_MODE="cold_start"
					echo "G36_FAILURE_MODE: cold_start (first frame violation, steady-state stable)"
				elif (( FRAME_JANK > FRAME_COUNT / 2 )); then
					FRAME_FAILURE_MODE="sustained"
					echo "G36_FAILURE_MODE: sustained (>50% frames over budget, systemic performance issue)"
				else
					FRAME_FAILURE_MODE="intermittent"
					echo "G36_FAILURE_MODE: intermittent (sporadic budget violations, investigate allocation stalls or GPU spikes)"
				fi
			fi

			# G36_PATCH_v37: Compute frame budget headroom distribution to distinguish
			# "stable with margin" from "stable but fragile". Headroom = budget - frame_time.
			# Negative headroom = over budget. Reports distribution: negative, critical (<1ms),
			# low (1-3ms), medium (3-5ms), healthy (>5ms).
			FRAME_BUDGET_HEADROOM_NEG=0
			FRAME_BUDGET_HEADROOM_CRIT=0
			FRAME_BUDGET_HEADROOM_LOW=0
			FRAME_BUDGET_HEADROOM_MED=0
			FRAME_BUDGET_HEADROOM_OK=0
			if (( FRAME_COUNT > 0 )); then
				eval "$(printf '%s\n' "${FRAME_TIMES[@]}" | awk -v target="$TARGET_FRAME_TIME" '
				{
					val = $1 + 0;
					headroom = target - val;
					if (headroom < 0) neg++;
					else if (headroom < 1.0) crit++;
					else if (headroom < 3.0) low++;
					else if (headroom < 5.0) med++;
					else ok++;
				}
				END {
					printf "FRAME_BUDGET_HEADROOM_NEG=%d\n", neg+0;
					printf "FRAME_BUDGET_HEADROOM_CRIT=%d\n", crit+0;
					printf "FRAME_BUDGET_HEADROOM_LOW=%d\n", low+0;
					printf "FRAME_BUDGET_HEADROOM_MED=%d\n", med+0;
					printf "FRAME_BUDGET_HEADROOM_OK=%d\n", ok+0;
				}')"
			fi

			# G36: Explicitly classify measurement state for downstream automation
			# This provides a single-line status that tooling can grep for
			if ! (( FRAME_BUDGET_PASSED )); then
				echo "G36_STATUS: FAIL (p95=${FRAME_P95}ms > ${TARGET_FRAME_TIME}ms, jank=${FRAME_JANK}/${FRAME_COUNT}, mode=${FRAME_FAILURE_MODE})"
			else
				echo "G36_STATUS: PASS (p95=${FRAME_P95}ms <= ${TARGET_FRAME_TIME}ms, jank=${FRAME_JANK}/${FRAME_COUNT})"
				# Report headroom distribution for fragility diagnosis
				echo "G36_HEADROOM_DIST: negative=${FRAME_BUDGET_HEADROOM_NEG} critical_lt1ms=${FRAME_BUDGET_HEADROOM_CRIT} low_1_3ms=${FRAME_BUDGET_HEADROOM_LOW} medium_3_5ms=${FRAME_BUDGET_HEADROOM_MED} healthy_gt5ms=${FRAME_BUDGET_HEADROOM_OK}"
				if (( FRAME_BUDGET_HEADROOM_NEG + FRAME_BUDGET_HEADROOM_CRIT > FRAME_COUNT / 4 )); then
					echo "G36_HEADROOM_WARN: >25% of frames have <1ms headroom (or over budget). Renderer is fragile; small perturbations may cause budget violations."
				elif (( FRAME_BUDGET_HEADROOM_OK * 2 < FRAME_COUNT )); then
					echo "G36_HEADROOM_NOTE: <50% of frames have >5ms headroom. Renderer meets budget but with limited margin for optimization regressions."
				else
					echo "G36_HEADROOM_OK: Majority of frames have healthy headroom (>5ms). Renderer has margin for variability."
				fi
			fi

			echo "G36_SUMMARY: samples=${FRAME_COUNT} guest_reported=${GUEST_REPORTED_FRAME_COUNT} avg=${FRAME_AVG}ms p95=${FRAME_P95}ms max=${FRAME_MAX}ms jank=${FRAME_JANK} passed=${FRAME_BUDGET_PASSED} steady_samples=${FRAME_STEADY_COUNT} steady_avg=${FRAME_STEADY_AVG}ms steady_p95=${FRAME_STEADY_P95}ms steady_passed=${FRAME_STEADY_BUDGET_PASSED} render_markers=${FRAME_RENDER_LOGS} render_complete=${FRAME_RENDER_COMPLETE_COUNT} gx_fifo_stalls=${GX_FIFO_STALLS} gx_fifo_overruns=${GX_FIFO_OVERRUNS} frame_hitches=${FRAME_HITCHES} budget_samples=${FRAME_BUDGET_SAMPLE_COUNT} gx_waitvp=${GX_WAITVP_COUNT} gx_waitvp_samples=${GX_WAITVP_SAMPLES} gx_wait_time_samples=${GX_WAIT_TIME_SAMPLES} sw_surfcache=${SW_SURFCACHE_OVERRIDE} lowmem_mode=${GC_LOWMEM_MODE:-none} client_entity_cap=${CLIENT_ENTITY_CAP:-unknown} frame_jitter_mad=${FRAME_TIMING_JITTER}ms frame_cv=${FRAME_CV} spike_events=${FRAME_SPIKE_EVENTS} spike_max_consec=${FRAME_SPIKE_MAX_CONSEC} worst_frame=${FRAME_WORST_TIME}ms severe_violations=${FRAME_SEVERE_VIOLATIONS} stage_annotated=${FRAME_BUDGET_STAGE_ANNOTATED} stage_violations=${FRAME_BUDGET_STAGE_HITS:-0} stage_breakdown=${FRAME_BUDGET_VIOLATION_STAGES:-none} pacing_variance=${FRAME_PACING_VARIANCE}ms pacing_max_delta=${FRAME_PACING_MAX_DELTA}ms cpu_avg=${FRAME_CPU_AVG:-N/A}ms gx_avg=${FRAME_GX_AVG:-N/A}ms renderer=${GUEST_RENDERER:-unknown} gx_flushes=${GX_FLUSH_MARKERS} gx_drawdone=${GX_DRAWDONE_COUNT} target=${TARGET_FRAME_TIME}ms guest_target=${GUEST_TARGET_FRAME_TIME:-N/A} regression_runs=${FRAME_REGRESSION_RUNS} regression_max_len=${FRAME_REGRESSION_MAX_LEN} measurement_init=${FRAME_BUDGET_INIT_OK} measurement_init_fail=${FRAME_BUDGET_INIT_FAIL} measurement_disabled=${FRAME_BUDGET_DISABLED} failure_mode=${FRAME_FAILURE_MODE:-none} stability_score=${FRAME_STABILITY_SCORE} cold_start_mem=${G36_COLD_START_MEM_TOTAL:-N/A}MiB cold_start_stage=${G36_COLD_START_MEM_STAGE:-N/A} warmup_frames=${FRAME_WARMUP_COUNT} memfail_count=${GC_MEMFAIL_COUNT} memfail_pool=${GC_MEMFAIL_POOL:-none} mem_delta_spikes=${GC_MEM_DELTA_SPIKES}"
			
			# G36: Report per-frame GX wait time samples for VI-sync correlation
			if (( GX_WAIT_TIME_SAMPLES > 0 )); then
				echo "G36_GX_WAIT_TIME: ${GX_WAIT_TIME_SAMPLES} per-frame GX wait time samples captured. VI-sync bottleneck analysis available."
				if [[ -n "$GX_WAIT_TIME_AVG" ]]; then
					echo "G36_GX_WAIT_TIME_AVG: ${GX_WAIT_TIME_AVG}ms average VI-wait time across ${GX_WAIT_TIME_SAMPLES} samples."
					if awk "BEGIN {exit !(${GX_WAIT_TIME_AVG} > 5.0)}" 2>/dev/null; then
						echo "G36_GX_WAIT_TIME_WARN: Average gx_wait_time (${GX_WAIT_TIME_AVG}ms) exceeds 5.0ms. Significant CPU time spent waiting for VI completion."
					fi
					# G36_PATCH_v33: Compute VI-wait fraction of total frame budget
					# High fraction (>50%) indicates VI-sync bound; optimization should
					# reduce work before GX_WaitVP, not micro-optimize renderer passes.
					if [[ -n "$FRAME_AVG" ]] && awk "BEGIN {exit !(${FRAME_AVG} > 0)}" 2>/dev/null; then
						GX_WAIT_BUDGET_PCT=$(awk "BEGIN {printf \"%.1f\", (${GX_WAIT_TIME_AVG} / ${FRAME_AVG}) * 100}")
						echo "G36_GX_WAIT_BUDGET_PCT: VI-wait consumes ${GX_WAIT_BUDGET_PCT}% of average frame budget (${GX_WAIT_TIME_AVG}ms / ${FRAME_AVG}ms)."
						if awk "BEGIN {exit !(${GX_WAIT_BUDGET_PCT} > 50.0)}" 2>/dev/null; then
							echo "G36_VI_BOUND: VI-sync dominates frame budget (>50%). Focus optimization on reducing CPU work before GX_WaitVP."
						elif awk "BEGIN {exit !(${GX_WAIT_BUDGET_PCT} > 25.0)}" 2>/dev/null; then
							echo "G36_VI_NOTE: VI-wait is significant (${GX_WAIT_BUDGET_PCT}%). Consider batching GX commands to reduce WaitVP frequency."
						fi
					fi
				fi
			fi

			# G36_PATCH_v17: Compute effective sample rate (Hz) to quantify telemetry density
			# Helps distinguish "guest not emitting markers" from "probe timeout too short"
			if (( FRAME_COUNT > 1 )) && [[ -n "$FRAME_FIRST" ]] && [[ -n "$FRAME_MAX" ]]; then
				# Approximate wall-clock span from first to last sample using sum of frame times
				SPAN_MS=$(printf '%s\n' "${FRAME_TIMES[@]}" | awk '{sum+=$1} END{printf "%.1f", sum}')
				SPAN_SEC=$(awk "BEGIN {printf \"%.3f\", ${SPAN_MS} / 1000.0}")
				SAMPLE_RATE_HZ=$(awk "BEGIN {printf \"%.1f\", ${FRAME_COUNT} / ${SPAN_SEC}}")
				echo "G36_SAMPLE_RATE: ${SAMPLE_RATE_HZ} Hz (${FRAME_COUNT} samples over ${SPAN_SEC}s wall-clock estimate)"
				if awk "BEGIN {exit !(${SAMPLE_RATE_HZ} < 10.0)}" 2>/dev/null; then
					echo "G36_SAMPLE_SPARSE: Sample rate ${SAMPLE_RATE_HZ} Hz is low. Telemetry may be throttled or frames skipped."
				fi
			fi

			# G36: Report frame timing jitter (MAD) as stability metric
			# Threshold of 2.0ms MAD indicates significant deviation from the mean frame time
			if awk "BEGIN {exit !(${FRAME_TIMING_JITTER} > 2.0)}" 2>/dev/null; then
				echo "G36_JITTER_WARN: Frame timing MAD=${FRAME_TIMING_JITTER}ms exceeds 2.0ms threshold. Rendering may appear stuttery due to high variance in frame delivery."
			fi

			# G36: Report which budget stages failed for targeted diagnosis
			if [[ -n "$FRAME_BUDGET_FAIL_STAGES" ]]; then
				echo "G36_FAIL_STAGES: Frame budget violations in stages: ${FRAME_BUDGET_FAIL_STAGES}"
			fi

			# G36_PATCH_v25: Report per-stage violation breakdown to identify hot rendering stages
			if [[ -n "$FRAME_BUDGET_VIOLATION_STAGES" ]]; then
				echo "G36_STAGE_BREAKDOWN: Budget violations by stage (count:stage): ${FRAME_BUDGET_VIOLATION_STAGES}"
				echo "G36_STAGE_HINT: Focus optimization on stages with highest violation counts. Correlate with GX time samples to determine CPU vs GPU origin."
			fi

			# G36: Report frame pacing variance as scheduling/stability metric
			if (( FRAME_COUNT > 2 )); then
				if awk "BEGIN {exit !(${FRAME_PACING_VARIANCE} > 5.0)}" 2>/dev/null; then
					echo "G36_PACING_WARN: Frame pacing variance=${FRAME_PACING_VARIANCE}ms exceeds 5.0ms. Irregular frame scheduling detected (max consecutive delta=${FRAME_PACING_MAX_DELTA}ms)."
					if (( FRAME_BUDGET_PASSED )); then
						echo "G36_PACING_NOTE: Budget passes but pacing is irregular. Game may appear stuttery despite meeting average budget."
					fi
				fi
			fi

			# G36: Report frame spike patterns as evidence of allocation stalls or cache thrashing
			if (( FRAME_SPIKE_EVENTS > 0 )); then
				echo "G36_SPIKES: ${FRAME_SPIKE_EVENTS} frame spike events detected (frames >2x budget), max consecutive=${FRAME_SPIKE_MAX_CONSEC}, severe_violations=${FRAME_SEVERE_VIOLATIONS}"
				if (( FRAME_SEVERE_VIOLATIONS > 0 )); then
					SEVERE_PCT=$(awk "BEGIN {printf \"%.1f\", ($FRAME_SEVERE_VIOLATIONS / $FRAME_COUNT) * 100}")
					echo "G36_SEVERE_VIOLATIONS: ${FRAME_SEVERE_VIOLATIONS} frames (>2x budget) detected (${SEVERE_PCT}% of samples). Indicates significant allocation stalls or hardware bottlenecks."
				fi
				if (( FRAME_SPIKE_MAX_CONSEC >= 3 )); then
					echo "G36_SPIKE_WARN: Extended frame spike run detected. Likely caused by memory allocation stalls or GC zone fragmentation."
					if (( GC_MEM_SAMPLES )); then
						echo "G36_SPIKE_HINT: Correlate with memory samples; consider preallocating during map load or reducing per-frame allocations."
					fi
				fi
			fi

			# G36: Report frame regression pattern (monotonically increasing frame times)
			if (( FRAME_REGRESSION_RUNS > 0 )); then
				echo "G36_REGRESSION: ${FRAME_REGRESSION_RUNS} frame regression runs detected (consecutive frames getting slower), max length=${FRAME_REGRESSION_MAX_LEN} frames"
				if (( FRAME_REGRESSION_MAX_LEN >= 4 )); then
					echo "G36_REGRESSION_WARN: Extended frame time regression detected. Strong indicator of memory fragmentation or cache thrashing building over time."
					if (( GC_MEM_SAMPLES )); then
						echo "G36_REGRESSION_HINT: Memory samples present; profile GC zone allocator for fragmentation during sustained rendering."
					fi
				fi
			fi

			# G36: Explicitly link memory pressure to frame spikes for allocation stall diagnosis
			if (( FRAME_SPIKE_EVENTS > 0 )) && (( GC_MEM_SAMPLES )); then
				if [[ -n "$GC_MEM_PEAK_TOTAL" ]]; then
					if awk "BEGIN {exit !($GC_MEM_PEAK_TOTAL > 10.0)}" 2>/dev/null; then
						echo "G36_ALLOC_STALL_RISK: High memory pressure (${GC_MEM_PEAK_TOTAL}MiB) coincides with frame spikes. GC zone allocator fragmentation is a probable cause for G36 instability."
					fi
				fi
			fi

			# G36: Report GC_MemFail OOM events as evidence of allocation-pressure frame stalls
			if (( GC_MEMFAIL_COUNT > 0 )); then
				echo "G36_MEMFAIL_EVENTS: ${GC_MEMFAIL_COUNT} GC_MemFail markers detected${GC_MEMFAIL_POOL:+ (last pool: ${GC_MEMFAIL_POOL})}."
				if (( FRAME_BUDGET_LOGS )) && ! (( FRAME_BUDGET_PASSED )); then
					echo "G36_MEMFAIL_CORRELATION: Memory allocation failures detected alongside frame budget violations. OOM pressure likely causing render stalls or frame drops."
				fi
			fi

			# G36: Report memory allocation spike correlation with frame budget violations
			if (( GC_MEM_DELTA_SPIKES > 0 )) && (( FRAME_BUDGET_LOGS )); then
				echo "G36_MEM_SPIKES: ${GC_MEM_DELTA_SPIKES} memory allocation bursts detected (>${GC_MEM_DELTA_SPIKE_THRESHOLD}KiB delta)."
				if (( FRAME_JANK > 0 )) && (( GC_MEM_DELTA_SPIKES > FRAME_JANK / 2 )); then
					echo "G36_MEM_SPIKE_CORRELATION: Memory spikes (${GC_MEM_DELTA_SPIKES}) closely match frame jank (${FRAME_JANK}). Per-frame allocations likely cause budget violations."
					echo "G36_MEM_SPIKE_HINT: Profile malloc/zone allocations during render loop; consider preallocating resources during map load."
				elif (( GC_MEM_DELTA_SPIKES > 10 )); then
					echo "G36_MEM_SPIKE_WARN: High allocation spike count (${GC_MEM_DELTA_SPIKES}). Consider reducing per-frame allocations or using object pools."
				fi
			fi

			# G36: Detect if frame budget violations cluster around map load (cold-start pattern)
			# by checking if early frames are disproportionately slow
			if (( FRAME_COUNT >= 3 )); then
				EARLY_SLOW=$(printf '%s\n' "${FRAME_TIMES[@]}" | awk -v target="$TARGET_FRAME_TIME" '
				NR <= 3 { if ($1+0 > target * 1.5) slow++; total++ }
				END { if (total > 0) printf "%.0f", (slow/total)*100; else print 0 }')
				if awk "BEGIN {exit !($EARLY_SLOW > 66)}" 2>/dev/null; then
					echo "G36_COLD_START_PATTERN: ${EARLY_SLOW}% of first 3 frames exceed 1.5x budget. Map-load or BSP parse work is likely blocking the main thread."
				fi
			fi

			# G36: Flag when first frame (renderer cold-start) dominates budget variance
			# This helps distinguish GX initialization overhead from steady-state rendering issues
			if (( FRAME_COUNT >= 2 )) && [[ -n "$FRAME_FIRST" ]] && [[ -n "$FRAME_AVG" ]]; then
				# If first frame is >3x the average, it's a strong cold-start indicator
				if awk "BEGIN {exit !(${FRAME_FIRST} > ${FRAME_AVG} * 3.0)}" 2>/dev/null; then
					echo "G36_COLD_START_DOMINANT: First frame (${FRAME_FIRST}ms) is >3x average (${FRAME_AVG}ms). GX initialization or first-draw overhead is significant. Steady-state metrics are more indicative of gameplay performance."
				fi
			fi

			# G36_PATCH_v11: Compute early/late violation ratio to diagnose if budget failures
			# are concentrated at map-load (cold-start) or persist into steady-state gameplay.
			# Early = first 25% of frames, Late = last 75% of frames.
			# This complements FRAME_JANK_DISTRIBUTION with a continuous ratio metric.
			if (( FRAME_COUNT >= 4 )) && (( FRAME_JANK > 0 )); then
				EARLY_LATE_RATIO=$(printf '%s\n' "${FRAME_TIMES[@]}" | awk -v target="$TARGET_FRAME_TIME" '
				{
					val = $1 + 0;
					times[NR] = val;
					count++;
				}
				END {
					if (count < 4) { print "N/A"; exit }
					early_bound = int(count * 0.25 + 0.9999);
					if (early_bound < 1) early_bound = 1;
					early_violations = 0;
					late_violations = 0;
					for (i = 1; i <= count; i++) {
						if (times[i] > target) {
							if (i <= early_bound) early_violations++;
							else late_violations++;
						}
					}
					if (early_violations + late_violations == 0) {
						print "0.00";
					} else {
						# Ratio of early violations to total violations (0.0 = all late, 1.0 = all early)
						ratio = early_violations / (early_violations + late_violations);
						printf "%.2f", ratio;
					}
				}')
				if [[ "$EARLY_LATE_RATIO" != "N/A" ]]; then
					echo "G36_VIOLATION_CONCENTRATION: early_late_ratio=${EARLY_LATE_RATIO} (1.0=all early, 0.0=all late)"
					if awk "BEGIN {exit !(${EARLY_LATE_RATIO} > 0.80)}" 2>/dev/null; then
						echo "G36_VIOLATION_HINT: >80% of budget violations in first 25% of frames. Issue is likely map-load initialization, not steady-state rendering."
					elif awk "BEGIN {exit !(${EARLY_LATE_RATIO} < 0.20)}" 2>/dev/null; then
						echo "G36_VIOLATION_HINT: >80% of budget violations in last 75% of frames. Steady-state rendering is the performance bottleneck."
					else
						echo "G36_VIOLATION_HINT: Budget violations distributed across early and late frames. Both initialization and per-frame work need profiling."
					fi
				fi
			fi

			# G36_PATCH_v39: Report exact frame indices where budget violations occur
			# to provide precise evidence for correlating with memory samples or GX markers.
			# Lists frame number (1-based) and time for each violating frame.
			if (( FRAME_JANK > 0 )); then
				VIOLATION_INDICES=$(printf '%s\n' "${FRAME_TIMES[@]}" | awk -v target="$TARGET_FRAME_TIME" '
				{
					val = $1 + 0;
					if (val > target) printf "%d=%.2f ", NR, val;
				}
				END { print "" }')
				echo "G36_VIOLATION_INDICES: Frames exceeding budget (index=time): ${VIOLATION_INDICES}"
			fi

			# G36_PATCH_v81: Detect frame time trend (improving vs degrading) to distinguish
			# "warmup settling" from "memory pressure building". Compares average of first half
			# vs second half of frame samples. Provides actionable evidence for whether
			# to optimize initialization (improving) vs steady-state/deallocation (degrading).
			if (( FRAME_COUNT >= 4 )); then
				FRAME_TREND=$(printf '%s\n' "${FRAME_TIMES[@]}" | awk '
				{
					times[NR] = $1 + 0;
					count++;
				}
				END {
					if (count < 4) { print "N/A"; exit }
					half = int(count / 2);
					sum1 = 0; sum2 = 0;
					for (i = 1; i <= half; i++) sum1 += times[i];
					for (i = half + 1; i <= count; i++) sum2 += times[i];
					avg1 = sum1 / half;
					avg2 = sum2 / (count - half);
					if (avg1 == 0) { print "N/A"; exit }
					ratio = avg2 / avg1;
					if (ratio < 0.90) print "improving";
					else if (ratio > 1.10) print "degrading";
					else print "stable";
				}')
				if [[ "$FRAME_TREND" != "N/A" ]]; then
					echo "G36_FRAME_TREND: ${FRAME_TREND} (first-half vs second-half average comparison)"
					if [[ "$FRAME_TREND" == "improving" ]]; then
						echo "G36_TREND_HINT: Frame times decreasing over time. Likely warmup completing or caches warming. Consider increasing warmup frames or pre-initializing renderer."
					elif [[ "$FRAME_TREND" == "degrading" ]]; then
						echo "G36_TREND_HINT: Frame times increasing over time. Strong indicator of memory fragmentation, zone pool pressure, or resource leak. Profile GC zone allocator."
					else
						echo "G36_TREND_OK: Frame times stable over probe duration. No significant warmup or degradation detected."
					fi
				fi
			fi

			# G36_PATCH_v101: Detect sawtooth frame pattern (alternating fast/slow frames)
			# to diagnose double-render loops or pipeline stall/recovery cycles.
			# Sawtooth: consecutive frames alternate above/below average.
			if (( FRAME_COUNT >= 6 )); then
				FRAME_SAWTOOTH=$(printf '%s\n' "${FRAME_TIMES[@]}" | awk '
				{
					sum += $1;
					times[NR] = $1;
					count++;
				}
				END {
					if (count < 6) { print 0; exit }
					avg = sum / count;
					alternations = 0;
					for (i = 2; i < count; i++) {
						prev_below = (times[i-1] < avg) ? 1 : 0;
						cur_below = (times[i] < avg) ? 1 : 0;
						if (prev_below != cur_below) alternations++;
					}
					# High alternation rate (>60% of transitions alternate) indicates sawtooth
					ratio = alternations / (count - 2);
					if (ratio > 0.60) print int(ratio * 100);
					else print 0;
				}')
				if (( FRAME_SAWTOOTH > 0 )); then
					echo "G36_SAWTOOTH_DETECTED: ${FRAME_SAWTOOTH}% frame transitions alternate around average. Suspect double-render loop or pipeline stall/recovery cycle."
					echo "G36_SAWTOOTH_HINT: Check for duplicate render calls, incorrect VI-sync placement, or command buffer double-submission."
				fi
			fi
		fi
		if (( FRAME_BUDGET_EXCEEDED )); then
			echo "PERFORMANCE_BLOCKER: Guest-reported budget: EXCEEDED marker found in logs."
		fi
		# Dump the last few frame timing logs for inspection
		echo "Last frame timing samples:"
		grep -aE "Xash3D GameCube: frame.*time=" "${LOG_FILES[@]}" | tail -n 5
	fi
	echo "Logs: $LOG_DIR"
	exit 0
fi

# Map loaded but input not detected. This might be a partial success for map loading
# but fails the "interactive" criteria of G19 if no controller is detected/polling.
if (( MAP_FOUND )) && ! (( INPUT_FOUND )); then
	if grep -aEiq 'Host_Error|Sys_Error|fatal error|guest.*(crash|abort)' "${LOG_FILES[@]}"; then
		echo "GUEST_FAILURE: Map load was observed, followed by a guest error."
		echo "Logs: $LOG_DIR"
		exit 3
	fi
	echo "MAP_LOADED_NO_INPUT: Map ${SMOKE_MAP} loaded but input polling marker was not found."
	echo "Logs: $LOG_DIR"
	exit 0
elif (( READY_FOUND )) && [[ -z "$SMOKE_MAP" ]]; then
	if grep -aEiq 'Host_Error|Sys_Error|fatal error|guest.*(crash|abort)' "${LOG_FILES[@]}"; then
		echo "GUEST_FAILURE: Engine readiness was observed, followed by a guest error."
		echo "Logs: $LOG_DIR"
		exit 3
	fi
	echo "ENGINE_READY: Xash3D initialized its GameCube subsystems."
	echo "Logs: $LOG_DIR"
	exit 0
elif (( GUEST_FOUND )) && grep -aEiq 'Host_Error|Sys_Error|Xash Error:|fatal error|out of memory' "${LOG_FILES[@]}"; then
	echo "GUEST_FAILURE: Bootstrap was followed by a guest-engine error."
	echo "Logs: $LOG_DIR"
	exit 3
elif [[ -z "$SMOKE_MAP" ]] && (( DIAGNOSTIC_MARKER_FOUND )); then
	echo "VISUAL_DIAGNOSTIC: Diagnostic marker was detected in logs. This suggests VI/XFB is functional."
	echo "Logs: $LOG_DIR"
	exit 0
elif grep -aEiq 'Unknown instruction|Invalid read from|IntCPU:|apploader.*(fail|error)' "${LOG_FILES[@]}"; then
	echo "BOOT_FAILURE: Dolphin reached the disc but the guest image failed before bootstrap."
	echo "Logs: $LOG_DIR"
	exit 3
elif (( DOLPHIN_EXIT == 124 || DOLPHIN_EXIT == 137 )); then
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
	exit 4
elif (( DOLPHIN_EXIT != 0 )); then
	if (( GUEST_FOUND )); then
		echo "GUEST_FAILURE: Dolphin exited $DOLPHIN_EXIT after guest bootstrap."
	else
		echo "HOST_FAILURE: Dolphin exited $DOLPHIN_EXIT before guest bootstrap."
	fi
	echo "Logs: $LOG_DIR"
	(( GUEST_FOUND )) && exit 3 || exit 2
else
	echo "INCONCLUSIVE_EXIT: Dolphin exited $DOLPHIN_EXIT without reaching engine readiness."
	if (( GUEST_FOUND )); then
			grep -ahF 'OSREPORT' "${LOG_FILES[@]}" | tail -1 | sed 's/^/Last guest log: /'
	fi
	echo "Logs: $LOG_DIR"
	exit 4
fi
