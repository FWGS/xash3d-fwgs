#!/usr/bin/env bash
set -uo pipefail


ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

# Source environment for Dolphin configuration
if [[ -f scripts/gamecube-env.sh ]]; then
	source scripts/gamecube-env.sh
fi

# Defaults
MAP_SOURCE_DIR="${MAP_SOURCE_DIR:-Half-Life/valve/maps}"
PROBE_SCRIPT="scripts/dolphin-boot-probe.sh"
LOG_BASE=".ai/logs/map-compat-$(date +%Y%m%d-%H%M%S)"
TSV_FILE="$LOG_BASE/results.tsv"
MD_FILE="$LOG_BASE/summary.md"
PROBE_TIMEOUT="${MAP_PROBE_TIMEOUT:-${MAP_COMPAT_TIMEOUT:-180}}"

mkdir -p "$LOG_BASE"

# Headers
printf "map\tstatus\tmemory_peak\tblocker\tlog_path\n" > "$TSV_FILE"
cat > "$MD_FILE" <<EOF
# GameCube Map Compatibility Report

Generated: $(date -u +"%Y-%m-%dT%H:%M:%SZ")
Maps Directory: ${MAP_SOURCE_DIR}
Probe Script: ${PROBE_SCRIPT}
Probe Timeout: ${PROBE_TIMEOUT}s

## Results

| Map | Status | Memory Peak | Blocker | Log Path |
|---|---|---|---|---|
EOF

if [[ ! -d "$MAP_SOURCE_DIR" ]]; then
	echo "WARN: Maps directory not found: $MAP_SOURCE_DIR"
	echo "No maps to probe."
	exit 0
fi

# Find maps. Positional arguments limit the run to a bounded map list.
map_files=()
if [[ $# -gt 0 ]]; then
	for map_name in "$@"; do
		map_name="${map_name%.bsp}"
		map_files+=("$MAP_SOURCE_DIR/${map_name}.bsp")
	done
else
	shopt -s nullglob
	map_files=("$MAP_SOURCE_DIR"/*.bsp)
	shopt -u nullglob
fi

if [[ ${#map_files[@]} -eq 0 ]]; then
	echo "No .bsp files found in $MAP_SOURCE_DIR"
	exit 0
fi

map_loaded_count=0
map_ready_count=0
inconclusive_count=0
missing_count=0
hard_failure_count=0

for bsp in "${map_files[@]}"; do
	if [[ ! -f "$bsp" ]]; then
		map_name="$(basename "$bsp" .bsp)"
		printf "%s\t%s\t%s\t%s\t%s\n" "$map_name" "MISSING" "N/A" "Map file not found" "N/A" >> "$TSV_FILE"
		printf "| %s | %s | %s | %s | %s |\n" "$map_name" "MISSING" "N/A" "Map file not found" "N/A" >> "$MD_FILE"
		echo "==> Probing map: $map_name"
		echo "  Result: MISSING"
		missing_count=$((missing_count + 1))
		continue
	fi
	map_name="$(basename "$bsp" .bsp)"
	echo "==> Probing map: $map_name"
	
	# dolphin-boot-probe.sh enforces DOLPHIN_TIMEOUT internally; do not wrap it in
	# an outer timeout shorter than build + probe + frame-sample budget.
	set +e
	PROBE_OUTPUT=$(DOLPHIN_SMOKE_MAP="$map_name" DOLPHIN_TIMEOUT="$PROBE_TIMEOUT" GC_BOOT_PROBE_TIMEOUT="$PROBE_TIMEOUT" bash "$PROBE_SCRIPT" 2>&1)
	PROBE_EXIT=$?
	set -e
	
	# Use the log directory reported by this probe. Falling back to "latest"
	# is unsafe when the GUI/Aider/Codex can run Dolphin probes concurrently.
	PROBE_LOG_DIR=$(printf "%s\n" "$PROBE_OUTPUT" | awk '/^Logs: / {print $2; found=1} END {exit found ? 0 : 1}' || true)
	if [[ -z "$PROBE_LOG_DIR" ]]; then
		PROBE_LOG_DIR=$(ls -td "$ROOT"/.ai/logs/dolphin-probe-* 2>/dev/null | head -n 1 || true)
	fi
	
	STATUS="UNKNOWN"
	BLOCKER=""
	MEM_PEAK="N/A"
	LOG_PATH="N/A"
	
	if [[ -n "$PROBE_LOG_DIR" ]]; then
		LOG_PATH="$PROBE_LOG_DIR"
		STDERR_LOG="$PROBE_LOG_DIR/stderr.log"
		STDOUT_LOG="$PROBE_LOG_DIR/stdout.log"
		
		# Check for success markers
		if grep -qsF "Xash3D GameCube: map loaded ${map_name}" "$STDERR_LOG" 2>/dev/null || \
		   grep -qsF "Xash3D GameCube: map loaded ${map_name}" "$STDOUT_LOG" 2>/dev/null; then
			STATUS="MAP_LOADED"
		elif echo "$PROBE_OUTPUT" | grep -qsF "MAP_READY"; then
			STATUS="MAP_READY"
		elif echo "$PROBE_OUTPUT" | grep -qsF "GUEST_FAILURE"; then
			STATUS="GUEST_FAILURE"
			BLOCKER=$(grep -ahm1 "Xash3D GameCube: fatal message=" "$STDERR_LOG" "$STDOUT_LOG" 2>/dev/null || true)
			if [[ -z "$BLOCKER" ]]; then
				BLOCKER=$(grep -ahm1 "Xash3D GameCube: mem FAIL" "$STDERR_LOG" "$STDOUT_LOG" 2>/dev/null || true)
			fi
			if [[ -z "$BLOCKER" ]]; then
				BLOCKER=$(grep -ahm1 -iE "Host_Error|Sys_Error|_Mem_Alloc|out of memory|fatal|panic|assert" "$STDERR_LOG" "$STDOUT_LOG" 2>/dev/null || true)
			fi
			if [[ -z "$BLOCKER" ]]; then
				BLOCKER=$(grep -ahm1 -iE "error|fail" "$STDERR_LOG" "$STDOUT_LOG" 2>/dev/null || echo "Guest failure")
			fi
		elif grep -aqF "Xash3D GameCube: fatal message=" "$STDERR_LOG" 2>/dev/null || \
		     grep -aqF "Xash3D GameCube: mem FAIL" "$STDERR_LOG" 2>/dev/null; then
			STATUS="GUEST_FAILURE"
			BLOCKER=$(grep -ahm1 "Xash3D GameCube: fatal message=" "$STDERR_LOG" "$STDOUT_LOG" 2>/dev/null || true)
			if [[ -z "$BLOCKER" ]]; then
				BLOCKER=$(grep -ahm1 "Xash3D GameCube: mem FAIL" "$STDERR_LOG" "$STDOUT_LOG" 2>/dev/null || true)
			fi
		elif echo "$PROBE_OUTPUT" | grep -qsF "HOST_FAILURE"; then
			STATUS="HOST_FAILURE"
			BLOCKER="Dolphin/Host initialization failed"
		elif [[ $PROBE_EXIT -ne 0 ]] && echo "$PROBE_OUTPUT" | grep -qsF "MAP_TIMEOUT"; then
			STATUS="TIMEOUT"
			BLOCKER="Probe timed out after ${PROBE_TIMEOUT}s"
		else
			STATUS="INCONCLUSIVE"
			BLOCKER="No clear success/failure markers"
		fi

		# Extract the latest memory high-water mark for both MAP_LOADED and
		# MAP_READY paths. Some probes report MAP_READY through analyzer output
		# even when the raw map-loaded marker is not copied to stdout.
		MEM_LINE=$(grep -ah "Xash3D GameCube: mem stage=" "$STDERR_LOG" "$STDOUT_LOG" 2>/dev/null | tail -n 1 || true)
		if [[ -n "$MEM_LINE" ]]; then
			MEM_PEAK=$(echo "$MEM_LINE" | grep -oP 'hwm=\K[0-9.]+ [KMG]b' || echo "N/A")
		fi
	fi
	
	# Sanitize blocker for TSV/MD
	BLOCKER=$(echo "$BLOCKER" | tr '\n' ' ' | tr '\t' ' ' | sed 's/|/ /g' | head -c 100)
	
	# Write results
	printf "%s\t%s\t%s\t%s\t%s\n" "$map_name" "$STATUS" "$MEM_PEAK" "$BLOCKER" "$LOG_PATH" >> "$TSV_FILE"
	printf "| %s | %s | %s | %s | [%s](%s) |\n" \
		"$map_name" "$STATUS" "$MEM_PEAK" "$BLOCKER" "Logs" "$LOG_PATH" >> "$MD_FILE"

	case "$STATUS" in
		MAP_LOADED) map_loaded_count=$((map_loaded_count + 1)) ;;
		MAP_READY) map_ready_count=$((map_ready_count + 1)) ;;
		INCONCLUSIVE) inconclusive_count=$((inconclusive_count + 1)) ;;
		MISSING) missing_count=$((missing_count + 1)) ;;
		*) hard_failure_count=$((hard_failure_count + 1)) ;;
	esac
		
	echo "  Result: $STATUS"
done

echo "==> Probe complete."
echo "   Summary: $MD_FILE"
echo "   TSV: $TSV_FILE"
echo "   Counts: loaded=$map_loaded_count ready=$map_ready_count inconclusive=$inconclusive_count missing=$missing_count hard_failures=$hard_failure_count"

if (( hard_failure_count == 0 )) && (( map_loaded_count + map_ready_count > 0 )); then
	if (( inconclusive_count == 0 )) && (( missing_count == 0 )); then
		echo "MAP_COMPAT_PROBE: PASS"
	else
		echo "MAP_COMPAT_PROBE: PARTIAL"
	fi
	exit 0
fi

echo "MAP_COMPAT_PROBE: FAIL"
exit 1
