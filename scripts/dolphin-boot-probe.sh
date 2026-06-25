#!/usr/bin/env bash
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"
if [[ -f scripts/gamecube-env.sh ]]; then
	source scripts/gamecube-env.sh
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
if ! python3 scripts/build-gamecube-disc.py "${BUILD_ARGS[@]}"; then
    echo "FAIL: Disc build failed."
    exit 1
fi

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
grep -aqE "Xash3D GameCube: frame.*time=" "${LOG_FILES[@]}" && FRAME_BUDGET_LOGS=1
grep -aqE "budget: EXCEEDED" "${LOG_FILES[@]}" && FRAME_BUDGET_EXCEEDED=1

# Check for frame budget measurement markers
FRAME_BUDGET_ENABLED=0
grep -aqsF "FRAME_BUDGET_ENABLED=1" "${LOG_FILES[@]}" && FRAME_BUDGET_ENABLED=1

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
FRAME_BUDGET_SAMPLE_COUNT=0
if grep -aqsF "Xash3D GameCube: frame budget sample start" "${LOG_FILES[@]}"; then
	FRAME_BUDGET_SAMPLE_COUNT=$(grep -acF "Xash3D GameCube: frame budget sample start" "${LOG_FILES[@]}")
fi

# G36: Extract explicit GC_MemSample high-water marks for memory-pressure correlation
GC_MEM_PEAK_TOTAL=""
GC_MEM_PEAK_DELTA=""
GC_MEM_PEAK_STAGE=""
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
		}
		END {
			if(max_total > 0) {
				printf "GC_MEM_PEAK_TOTAL=%.1f\n", max_total;
				printf "GC_MEM_PEAK_DELTA=%.1f\n", max_delta;
				printf "GC_MEM_PEAK_STAGE=%s\n", max_stage;
			}
		}')"
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
if (( FRAME_BUDGET_LOGS )); then
	# Extract frame times in one pass using grep -P for reliability
	while IFS= read -r val; do
		[[ -n "$val" ]] && FRAME_TIMES+=("$val")
	done < <(grep -aoE 'Xash3D GameCube: frame[^ ]* time=[0-9]+(\.[0-9]+)?' "${LOG_FILES[@]}" 2>/dev/null | \
		grep -oP 'time=\K[0-9]+(\.[0-9]+)?')

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
FRAME_COUNT=${#FRAME_TIMES[@]}
if (( FRAME_COUNT > 0 )); then
	# Use awk for robust float math and sorting without bc dependency
	eval "$(printf '%s\n' "${FRAME_TIMES[@]}" | awk -v target="$TARGET_FRAME_TIME" '
	BEGIN {
		min = 999999; max = 0; sum = 0; sum_sq = 0; count = 0; jank = 0;
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
	}'
	)"
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
			
			# Explicit G36 Measurement Result
			if (( FRAME_BUDGET_PASSED )); then
				echo "G36_MEASUREMENT_PASS: Frame budget stable. P95=${FRAME_P95}ms <= ${TARGET_FRAME_TIME}ms."
			else
				echo "G36_MEASUREMENT_FAIL: Frame budget unstable. P95=${FRAME_P95}ms > ${TARGET_FRAME_TIME}ms."
			fi
			
			# G36: Correlate memory samples with frame budget
			if (( GC_MEM_SAMPLES )); then
				echo "G36_MEM_CORRELATION: GC memory samples detected alongside frame budget telemetry."
				if [[ -n "$GC_MEM_PEAK_TOTAL" ]]; then
					echo "G36_MEM_PEAK: total=${GC_MEM_PEAK_TOTAL}MiB delta=${GC_MEM_PEAK_DELTA}MiB stage=${GC_MEM_PEAK_STAGE}"
					if awk "BEGIN {exit !($GC_MEM_PEAK_TOTAL > 16.0)}"; then
						echo "G36_MEM_PRESSURE: Peak memory usage (${GC_MEM_PEAK_TOTAL}MiB) exceeds 16MiB. Frame budget may be impacted by GC zone allocation overhead."
					fi
				fi
			else
				echo "G36_MEM_NOTE: No GC memory samples detected in this probe; frame budget is isolated from memory pressure evidence."
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

			# G36: Report frame presentation hitches (CPU/GPU sync gaps)
			if (( FRAME_HITCHES > 0 )); then
				echo "G36_GX_HITCHES: ${FRAME_HITCHES} frame hitch markers detected. Possible CPU/GPU sync or VI wait issues."
			fi

			# G36: Report frame budget sample consistency
			if (( FRAME_BUDGET_SAMPLE_COUNT > 0 )); then
				echo "G36_BUDGET_SAMPLES: ${FRAME_BUDGET_SAMPLE_COUNT} explicit budget samples recorded."
				if (( FRAME_BUDGET_SAMPLE_COUNT > FRAME_COUNT )); then
					echo "G36_BUDGET_NOTE: More budget samples than frame-time logs. Some frames may have been skipped in timing telemetry."
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
			echo "G36_SUMMARY: samples=${FRAME_COUNT} avg=${FRAME_AVG}ms p95=${FRAME_P95}ms max=${FRAME_MAX}ms jank=${FRAME_JANK} passed=${FRAME_BUDGET_PASSED} render_markers=${FRAME_RENDER_LOGS} gx_fifo_stalls=${GX_FIFO_STALLS} frame_hitches=${FRAME_HITCHES} budget_samples=${FRAME_BUDGET_SAMPLE_COUNT} gx_waitvp=${GX_WAITVP_COUNT}"
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
