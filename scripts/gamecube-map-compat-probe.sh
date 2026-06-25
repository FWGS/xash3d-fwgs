#!/usr/bin/env bash
set -uo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

MAPS_DIR="${1:-Half-Life/valve/maps}"
LOG_BASE=".ai/logs/map-compat-$(date +%Y%m%d-%H%M%S)"
TSV_FILE="$LOG_BASE/results.tsv"
MD_FILE="$LOG_BASE/summary.md"
PROBE_SCRIPT="scripts/dolphin-boot-probe.sh"

mkdir -p "$LOG_BASE"

# Header for TSV
printf "map\tstatus\tmemory_peak\tblocker\tlog_path\n" > "$TSV_FILE"

# Header for Markdown
cat > "$MD_FILE" <<EOF
# GameCube Map Compatibility Report

Generated: $(date -u +"%Y-%m-%dT%H:%M:%SZ")
Maps Directory: ${MAPS_DIR}
Probe Script: ${PROBE_SCRIPT}

## Results

| Map | Status | Memory Peak | Blocker | Log Path |
|---|---|---|---|---|
EOF

if [ ! -d "$MAPS_DIR" ]; then
	echo "WARN: Maps directory not found: $MAPS_DIR"
	echo "No maps to probe."
	exit 0
fi

# Iterate over all .bsp files
for bsp in "$MAPS_DIR"/*.bsp; do
	[ -f "$bsp" ] || continue
	
	map_name="$(basename "$bsp" .bsp)"
	echo "==> Probing map: $map_name"
	
	# Run the boot probe for this specific map
	# DOLPHIN_SMOKE_MAP tells the probe which map to load
	# We capture stdout/stderr to determine status
	PROBE_OUTPUT=$(DOLPHIN_SMOKE_MAP="$map_name" timeout 180 bash "$PROBE_SCRIPT" 2>&1)
	PROBE_EXIT=$?
	
	# Find the specific log directory created by the probe
	# The probe creates .ai/logs/dolphin-probe-YYYYMMDD-HHMMSS
	# We look for the most recently modified one
	PROBE_LOG_DIR=$(ls -td .ai/logs/dolphin-probe-* 2>/dev/null | head -n 1)
	
	if [ -z "$PROBE_LOG_DIR" ]; then
		STATUS="PROBE_FAIL"
		BLOCKER="Probe script did not create log directory"
		LOG_PATH=""
		MEM_PEAK="N/A"
	else
		LOG_PATH="$PROBE_LOG_DIR"
		
		# Analyze logs
		STDERR_LOG="$PROBE_LOG_DIR/stderr.log"
		STDOUT_LOG="$PROBE_LOG_DIR/stdout.log"
		
		BLOCKER=""
		STATUS="UNKNOWN"
		MEM_PEAK="N/A"
		
		# Check for Map Loaded marker
		if grep -q "Xash3D GameCube: map loaded ${map_name}" "$STDERR_LOG" 2>/dev/null || \
		   grep -q "Xash3D GameCube: map loaded ${map_name}" "$STDOUT_LOG" 2>/dev/null; then
			STATUS="MAP_LOADED"
			
			# Extract memory peak if available
			MEM_LINE=$(grep "mem stage=.*hwm=" "$STDERR_LOG" 2>/dev/null | tail -n 1)
			if [ -n "$MEM_LINE" ]; then
				MEM_PEAK=$(echo "$MEM_LINE" | grep -oP 'hwm=\K[0-9.]+ [KmG]b')
			fi
		elif grep -q "MAP_READY" <<< "$PROBE_OUTPUT"; then
			STATUS="MAP_READY"
		elif grep -q "GUEST_FAILURE" <<< "$PROBE_OUTPUT" || grep -q "Host_Error" "$STDERR_LOG" 2>/dev/null; then
			STATUS="GUEST_FAILURE"
			BLOCKER=$(grep -m1 -i "error\|fail\|panic" "$STDERR_LOG" 2>/dev/null | head -n 1)
		elif grep -q "HOST_FAILURE" <<< "$PROBE_OUTPUT"; then
			STATUS="HOST_FAILURE"
			BLOCKER="Dolphin/Host failed"
		elif [ $PROBE_EXIT -eq 124 ] || [ $PROBE_EXIT -eq 137 ]; then
			STATUS="TIMEOUT"
			BLOCKER="Probe timed out or killed"
		else
			STATUS="INCONCLUSIVE"
			BLOCKER="No clear success/failure markers found"
		fi
		
		# Clean up blocker string for TSV/Markdown (replace newlines/tabs)
		BLOCKER=$(echo "$BLOCKER" | tr '\n' ' ' | tr '\t' ' ' | sed 's/|/ /g')
	fi
	
	# Record to TSV
	printf "%s\t%s\t%s\t%s\t%s\n" "$map_name" "$STATUS" "$MEM_PEAK" "$BLOCKER" "$LOG_PATH" >> "$TSV_FILE"
	
	# Record to Markdown
	printf "| %s | %s | %s | %s | [%s](%s) |\n" \
		"$map_name" "$STATUS" "$MEM_PEAK" "$BLOCKER" "Logs" "$LOG_PATH" >> "$MD_FILE"
		
done

echo "==> Probe complete. Results in $LOG_BASE/"
echo "   TSV: $TSV_FILE"
echo "   Markdown: $MD_FILE"
