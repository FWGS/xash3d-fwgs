#!/usr/bin/env bash
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

RUN_ALL=0
if [[ "${1:-}" == "--all" ]]; then
	RUN_ALL=1
	shift
fi

MAP_ROOT="${HL1_MAP_DIR:-$ROOT/Half-Life/valve/maps}"
TIMEOUT_SEC="${DOLPHIN_TIMEOUT:-180}"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUT_DIR=".ai/logs/map-compat-$STAMP"
REPORT="$OUT_DIR/report.tsv"
SUMMARY="$OUT_DIR/summary.md"
mkdir -p "$OUT_DIR"

# Initialize maps list
MAPS=()

if [[ $# -gt 0 ]]; then
	MAPS=("$@")
elif [[ -n "${MAP_COMPAT_MAPS:-}" ]]; then
	read -r -a MAPS <<<"$MAP_COMPAT_MAPS"
elif (( RUN_ALL )) && [[ -d "$MAP_ROOT" ]]; then
	# Use a temporary file to safely read map names to avoid subshell issues with mapfile if needed,
	# though mapfile is generally safe in bash 4+.
	mapfile -t MAPS < <(find "$MAP_ROOT" -maxdepth 1 -type f -name '*.bsp' \
		-printf '%f\n' | sed 's/\.bsp$//' | sort)
else
	echo "map-compat: supply map names, MAP_COMPAT_MAPS, or --all for $MAP_ROOT" >&2
	exit 2
fi

if [[ ${#MAPS[@]} -eq 0 ]]; then
	echo "map-compat: no maps found to probe."
	exit 0
fi

printf 'map\tstatus\tlog\n' >"$REPORT"
{
	echo "# GameCube Map Compatibility Probe"
	echo
	echo "- Started: $STAMP"
	echo "- Timeout: ${TIMEOUT_SEC}s"
	echo "- Maps: ${#MAPS[@]}"
	echo
	echo "| Map | Status | Log |"
	echo "| --- | --- | --- |"
} >"$SUMMARY"

# Track overall status
overall_status=0
for map in "${MAPS[@]}"; do
	echo "==> probing $map"
	
	# Capture output and status
	set +e
	output="$(DOLPHIN_TIMEOUT="$TIMEOUT_SEC" DOLPHIN_SMOKE_MAP="$map" \
		scripts/dolphin-boot-probe.sh 2>&1)" || true
	probe_status=$?
	set -e
	
	# Extract log path from output
	log_path="$(printf '%s\n' "$output" | awk '/^Logs: / { print $2 }' | tail -1)"
	if [[ -z "$log_path" ]]; then
		log_path="unknown"
	fi

	# Determine label based on probe_status and output content
	label="unknown"
	case "$probe_status" in
		0) label="ready" ;;
		1) 
			# Exit 1 often means build failed in dolphin-boot-probe.sh
			if printf '%s\n' "$output" | grep -q "FAIL: Disc build failed"; then
				label="build-failure"
				overall_status=2
			else
				label="probe-error-1"
				overall_status=2
			fi
			;;
		2) label="host-failure"; (( overall_status < 2 )) && overall_status=2 ;;
		3) label="guest-failure"; (( overall_status < 3 )) && overall_status=3 ;;
		4) label="timeout"; (( overall_status < 4 )) && overall_status=4 ;;
		*) label="probe-error-$probe_status"; (( overall_status < probe_status )) && overall_status=$probe_status ;;
	esac
	
	# Refine status if we have specific markers in output for status 0
	if [[ "$label" == "ready" ]]; then
		if printf '%s\n' "$output" | grep -q "MAP_READY"; then
			label="playable"
		elif printf '%s\n' "$output" | grep -q "MAP_LOADED_NO_INPUT"; then
			label="loaded-no-input"
		else
			label="engine-ready"
		fi
	fi

	printf '%s\t%s\t%s\n' "$map" "$label" "${log_path}" >>"$REPORT"
	printf '| `%s` | `%s` | `%s` |\n' "$map" "$label" "${log_path}" >>"$SUMMARY"
	printf '%s\n' "$output" >"$OUT_DIR/$map.output.log"
done

echo "map-compat: wrote $REPORT"
echo "map-compat: wrote $SUMMARY"
exit "$overall_status"
