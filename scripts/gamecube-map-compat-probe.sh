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
PROBE_TIMEOUT="${MAP_COMPAT_TIMEOUT:-120}"

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

# Find maps
shopt -s nullglob
map_files=("$MAP_SOURCE_DIR"/*.bsp)
shopt -u nullglob

if [[ ${#map_files[@]} -eq 0 ]]; then
	echo "No .bsp files found in $MAP_SOURCE_DIR"
	exit 0
fi

for bsp in "${map_files[@]}"; do
	map_name="$(basename "$bsp" .bsp)"
	echo "==> Probing map: $map_name"
	
	# Run probe with timeout
	PROBE_OUTPUT=$(DOLPHIN_SMOKE_MAP="$map_name" timeout "$PROBE_TIMEOUT" bash "$PROBE_SCRIPT" 2>&1) || true
	PROBE_EXIT=$?
	
	# Determine log directory (most recent dolphin-probe log)
	PROBE_LOG_DIR=$(ls -td .ai/logs/dolphin-probe-* 2>/dev/null | head -n 1)
	
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
			
			# Extract memory high-water mark
			if [[ -f "$STDERR_LOG" ]]; then
				MEM_LINE=$(grep "Xash3D GameCube: mem stage=" "$STDERR_LOG" 2>/dev/null | tail -n 1)
				if [[ -n "$MEM_LINE" ]]; then
					MEM_PEAK=$(echo "$MEM_LINE" | grep -oP 'hwm=\K[0-9.]+ [KmG]b' || echo "N/A")
				fi
			fi
		elif echo "$PROBE_OUTPUT" | grep -qsF "MAP_READY"; then
			STATUS="MAP_READY"
		elif echo "$PROBE_OUTPUT" | grep -qsF "GUEST_FAILURE"; then
			STATUS="GUEST_FAILURE"
			BLOCKER=$(grep -m1 -iE "error|fail|panic|assert" "$STDERR_LOG" 2>/dev/null | head -n 1 || echo "Guest failure")
		elif echo "$PROBE_OUTPUT" | grep -qsF "HOST_FAILURE"; then
			STATUS="HOST_FAILURE"
			BLOCKER="Dolphin/Host initialization failed"
		elif [[ $PROBE_EXIT -eq 124 ]]; then
			STATUS="TIMEOUT"
			BLOCKER="Probe timed out after ${PROBE_TIMEOUT}s"
		else
			STATUS="INCONCLUSIVE"
			BLOCKER="No clear success/failure markers"
		fi
	fi
	
	# Sanitize blocker for TSV/MD
	BLOCKER=$(echo "$BLOCKER" | tr '\n' ' ' | tr '\t' ' ' | sed 's/|/ /g' | head -c 100)
	
	# Write results
	printf "%s\t%s\t%s\t%s\t%s\n" "$map_name" "$STATUS" "$MEM_PEAK" "$BLOCKER" "$LOG_PATH" >> "$TSV_FILE"
	printf "| %s | %s | %s | %s | [%s](%s) |\n" \
		"$map_name" "$STATUS" "$MEM_PEAK" "$BLOCKER" "Logs" "$LOG_PATH" >> "$MD_FILE"
		
	echo "  Result: $STATUS"
done

echo "==> Probe complete."
echo "   Summary: $MD_FILE"
echo "   TSV: $TSV_FILE"
