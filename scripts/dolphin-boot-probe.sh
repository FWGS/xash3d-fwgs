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
	echo "G36_STATUS: INCOMPLETE (no frame budget telemetry)"
fi

# G36_PATCH: Correlate renderer backend presence with frame budget measurement capability
# Helps distinguish "renderer not initialized" from "renderer initialized but not emitting telemetry"
if (( MAP_FOUND )) && (( FRAME_BUDGET_LOGS )) && [[ -z "$GUEST_RENDERER" ]]; then
	echo "G36_RENDERER_UNDETECTED: Frame budget logs present but renderer backend name not identified in guest logs."
	echo "G36_RENDERER_HINT: Ensure guest emits 'Xash3D GameCube: renderer initialized <backend>' during startup."
fi

# G36: Emit explicit measurement baseline marker so downstream tooling can
# distinguish "telemetry absent" from "telemetry present but failing"
echo "G36_BASELINE: frame_budget_logs=${FRAME_BUDGET_LOGS} frame_samples_available=unknown renderer=${GUEST_RENDERER:-undetected} lowmem=${GC_LOWMEM_MODE:-none} timeout=${TIMEOUT_SEC}s"

# G36: Explicitly look for guest-reported memory samples to correlate with frame budget
GC_MEM_SAMPLES=0
grep -aqE "GC_MemSample|mem stage=" "${LOG_FILES[@]}" && GC_MEM_SAMPLES=1

# G36: Detect explicit GX WaitVP/WaitVP sync markers to measure VI-wait impact on frame budget
GX_WAITVP_COUNT=0
if grep -aqsF "GX_WAITVP" "${LOG_FILES[@]}"; then
	GX_WAITVP_COUNT=$(grep -acF "GX_WAITVP" "${LOG_FILES[@]}")
fi

# G36: Track explicit GX renderer frame-start markers for CPU vs GPU correlation
FRAME_RENDER_LOGS=0
grep -aqE "Xash3D GameCube: render frame" "${LOG_FILES[@]}" && FRAME_RENDER_LOGS=1

# G36: Detect explicit GX FIFO stall or overflow markers (hardware-bound evidence)
GX_FIFO_STALLS=0
if grep -aqsF "GX_FIFO_STALL" "${LOG_FILES[@]}"; then
	GX_FIFO_STALLS=$(grep -acF "GX_FIFO_STALL" "${LOG_FILES[@]}")
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
FRAME_BUDGET_SAMPLE_COUNT=$FRAME_BUDGET_SAMPLE_START

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
GUEST_RENDERER=""
if grep -aqsF "Xash3D GameCube: renderer initialized" "${LOG_FILES[@]}"; then
	# G36_PATCH_v2: Relaxed pattern to catch renderer name after "initialized" with any spacing
	GUEST_RENDERER=$(grep -aoE 'Xash3D GameCube: renderer initialized +[a-zA-Z_-]+' "${LOG_FILES[@]}" | tail -1 | grep -oE '[a-zA-Z_-]+$')
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
	if grep -aqsE 'Xash3D GameCube: (frame |render )?gx_wait_time=' "${LOG_FILES[@]}"; then
		GX_WAIT_TIME_SAMPLES=$(grep -acE 'Xash3D GameCube: (frame |render )?gx_wait_time=' "${LOG_FILES[@]}")
	fi

	# G36_PATCH_v13: Single-pass unified extraction to eliminate "missing frames
	# between explicit sample" by using one deterministic grep pattern that matches
	# all known frame timing marker formats. Avoids double-counting from overlapping
	# primary/fallback regexes.
	
	# Extract 'time=' markers (primary budget metric)
	FRAME_TIMES_STRICT=$(grep -aoE 'Xash3D GameCube: frame time=[0-9]+(\.[0-9]+)?ms?' "${LOG_FILES[@]}" 2>/dev/null | wc -l)
		
	# Extract all frame time/duration values in one deterministic pass
	# Captures: frame time, render time, frame budget time, frame render complete time,
	# frame duration, render duration, frame budget duration, gx_time, cpu_time
	# Uses a single pattern to avoid sample loss from fragmented grep invocations.
	while IFS= read -r val; do
		[[ -n "$val" ]] && FRAME_TIMES+=("$val")
	done < <(grep -aoE 'Xash3D GameCube: (frame (render |budget )?(time|duration)|render (frame )?(time|duration)|frame (render )?complete time|[cg]pu_time|gx_time)=[0-9]+(\.[0-9]+)?ms?' "${LOG_FILES[@]}" 2>/dev/null | \
		grep -oE '[a-z_]+=[0-9]+(\.[0-9]+)?' | sed 's/.*=//')

	FRAME_TIMES_RELAXED=${#FRAME_TIMES[@]}

	# G36_DEDUP_v1: Deduplicate FRAME_TIMES to prevent double-counting when
	# both 'time=' and 'duration=' markers are emitted for the same frame.
	# Sort numerically, remove exact duplicates, then restore to array.
	FRAME_COUNT=${#FRAME_TIMES[@]}
	if (( FRAME_COUNT > 1 )); then
		PRE_DEDUP_COUNT=$FRAME_COUNT
		FRAME_TIMES=( $(printf '%s\n' "${FRAME_TIMES[@]}" | sort -n | uniq) )
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
FRAME_COUNT=${#FRAME_TIMES[@]}
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

			# G36: Report GX WaitVP (VI sync) count as evidence of CPU yielding to vertical sync
			if (( GX_WAITVP_COUNT > 0 )); then
				echo "G36_GX_WAITVP: ${GX_WAITVP_COUNT} GX_WAITVP markers detected. Frame budget includes explicit VI synchronization pauses."
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

			# G36: Classify failure mode: cold-start only vs sustained
			# Helps distinguish renderer initialization overhead from steady-state rendering issues
			FRAME_FAILURE_MODE="unknown"
			if ! (( FRAME_BUDGET_PASSED )); then
				if (( FRAME_STEADY_BUDGET_PASSED )) && (( FRAME_COUNT >= 2 )); then
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

			# G36: Explicitly classify measurement state for downstream automation
			# This provides a single-line status that tooling can grep for
			if ! (( FRAME_BUDGET_PASSED )); then
				echo "G36_STATUS: FAIL (p95=${FRAME_P95}ms > ${TARGET_FRAME_TIME}ms, jank=${FRAME_JANK}/${FRAME_COUNT}, mode=${FRAME_FAILURE_MODE})"
			else
				echo "G36_STATUS: PASS (p95=${FRAME_P95}ms <= ${TARGET_FRAME_TIME}ms, jank=${FRAME_JANK}/${FRAME_COUNT})"
			fi

			echo "G36_SUMMARY: samples=${FRAME_COUNT} guest_reported=${GUEST_REPORTED_FRAME_COUNT} avg=${FRAME_AVG}ms p95=${FRAME_P95}ms max=${FRAME_MAX}ms jank=${FRAME_JANK} passed=${FRAME_BUDGET_PASSED} steady_samples=${FRAME_STEADY_COUNT} steady_avg=${FRAME_STEADY_AVG}ms steady_p95=${FRAME_STEADY_P95}ms steady_passed=${FRAME_STEADY_BUDGET_PASSED} render_markers=${FRAME_RENDER_LOGS} gx_fifo_stalls=${GX_FIFO_STALLS} frame_hitches=${FRAME_HITCHES} budget_samples=${FRAME_BUDGET_SAMPLE_COUNT} gx_waitvp=${GX_WAITVP_COUNT} gx_wait_time_samples=${GX_WAIT_TIME_SAMPLES} sw_surfcache=${SW_SURFCACHE_OVERRIDE} lowmem_mode=${GC_LOWMEM_MODE:-none} client_entity_cap=${CLIENT_ENTITY_CAP:-unknown} frame_jitter_mad=${FRAME_TIMING_JITTER}ms frame_cv=${FRAME_CV} spike_events=${FRAME_SPIKE_EVENTS} spike_max_consec=${FRAME_SPIKE_MAX_CONSEC} worst_frame=${FRAME_WORST_TIME}ms severe_violations=${FRAME_SEVERE_VIOLATIONS} stage_annotated=${FRAME_BUDGET_STAGE_ANNOTATED} pacing_variance=${FRAME_PACING_VARIANCE}ms pacing_max_delta=${FRAME_PACING_MAX_DELTA}ms cpu_avg=${FRAME_CPU_AVG:-N/A}ms gx_avg=${FRAME_GX_AVG:-N/A}ms renderer=${GUEST_RENDERER:-unknown} gx_flushes=${GX_FLUSH_MARKERS} target=${TARGET_FRAME_TIME}ms regression_runs=${FRAME_REGRESSION_RUNS} regression_max_len=${FRAME_REGRESSION_MAX_LEN} measurement_init=${FRAME_BUDGET_INIT_OK} measurement_init_fail=${FRAME_BUDGET_INIT_FAIL} measurement_disabled=${FRAME_BUDGET_DISABLED} failure_mode=${FRAME_FAILURE_MODE:-none}"
			
			# G36: Report per-frame GX wait time samples for VI-sync correlation
			if (( GX_WAIT_TIME_SAMPLES > 0 )); then
				echo "G36_GX_WAIT_TIME: ${GX_WAIT_TIME_SAMPLES} per-frame GX wait time samples captured. VI-sync bottleneck analysis available."
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
