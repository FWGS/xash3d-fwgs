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

if [[ $# -gt 0 ]]; then
	MAPS=("$@")
elif [[ -n "${MAP_COMPAT_MAPS:-}" ]]; then
	read -r -a MAPS <<<"$MAP_COMPAT_MAPS"
elif (( RUN_ALL )) && [[ -d "$MAP_ROOT" ]]; then
	mapfile -t MAPS < <(find "$MAP_ROOT" -maxdepth 1 -type f -name '*.bsp' \
		-printf '%f\n' | sed 's/\.bsp$//' | sort)
else
	echo "map-compat: supply map names, MAP_COMPAT_MAPS, or --all for $MAP_ROOT" >&2
	exit 2
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

status=0
for map in "${MAPS[@]}"; do
	echo "==> probing $map"
	set +e
	output="$(DOLPHIN_TIMEOUT="$TIMEOUT_SEC" DOLPHIN_SMOKE_MAP="$map" \
		scripts/dolphin-boot-probe.sh 2>&1)"
	probe_status=$?
	set -e
	log_path="$(printf '%s\n' "$output" | awk '/^Logs: / { print $2 }' | tail -1)"
	case "$probe_status" in
		0) label="ready" ;;
		2) label="host-failure"; status=2 ;;
		3) label="guest-failure"; (( status < 3 )) && status=3 ;;
		4) label="timeout"; (( status < 4 )) && status=4 ;;
		*) label="probe-error-$probe_status"; status="$probe_status" ;;
	esac
	printf '%s\t%s\t%s\n' "$map" "$label" "${log_path:-none}" >>"$REPORT"
	printf '| `%s` | `%s` | `%s` |\n' "$map" "$label" "${log_path:-none}" >>"$SUMMARY"
	printf '%s\n' "$output" >"$OUT_DIR/$map.output.log"
done

echo "map-compat: wrote $REPORT"
echo "map-compat: wrote $SUMMARY"
exit "$status"
