#!/usr/bin/env bash
# Chapter-level Half-Life campaign audit for the GameCube port.
set -uo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT" || exit 1

MAP_SOURCE_DIR="${MAP_SOURCE_DIR:-Half-Life/valve/maps}"
MAP_COMPAT_SCRIPT="${MAP_COMPAT_SCRIPT:-scripts/gamecube-map-compat-probe.sh}"
PROBE_TIMEOUT="${MAP_COMPAT_TIMEOUT:-120}"
STAMP="$(date +%Y%m%d-%H%M%S)"
LOG_BASE="${CAMPAIGN_AUDIT_LOG_DIR:-.ai/logs/campaign-audit-$STAMP}"
MAP_TSV="$LOG_BASE/map-results.tsv"
CHAPTER_TSV="$LOG_BASE/chapter-results.tsv"
SUMMARY="$LOG_BASE/summary.md"
MODE="representative"
DRY_RUN=0
declare -a SELECT_CHAPTERS

usage() {
	cat <<'EOF'
Usage: scripts/gamecube-campaign-audit.sh [--representative|--full] [--dry-run] [--chapter NAME]

Runs the stock Half-Life campaign route through the GameCube map compatibility
probe and summarizes results by chapter. The default representative mode probes
one entry map per chapter; --full probes every listed chapter map.

Environment:
  MAP_SOURCE_DIR           map directory, default Half-Life/valve/maps
  MAP_COMPAT_TIMEOUT       per-map timeout passed to gamecube-map-compat-probe.sh
  CAMPAIGN_AUDIT_LOG_DIR   output directory override
EOF
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		--representative) MODE="representative" ;;
		--full) MODE="full" ;;
		--dry-run) DRY_RUN=1 ;;
		--chapter)
			shift
			if [[ $# -eq 0 ]]; then
				echo "--chapter requires a chapter name" >&2
				exit 2
			fi
			SELECT_CHAPTERS+=("$1")
			;;
		--help|-h) usage; exit 0 ;;
		*)
			echo "unknown argument: $1" >&2
			usage >&2
			exit 2
			;;
	esac
	shift
done

mkdir -p "$LOG_BASE"

CHAPTERS=(
	"Black Mesa Inbound|c0a0e|c0a0 c0a0a c0a0b c0a0c c0a0d c0a0e"
	"Anomalous Materials|c1a0|c1a0 c1a0a c1a0b c1a0c c1a0d c1a0e"
	"Unforeseen Consequences|c1a1|c1a1 c1a1a c1a1b c1a1c c1a1d c1a1f"
	"Office Complex|c1a2|c1a2 c1a2a c1a2b c1a2c c1a2d"
	"We've Got Hostiles|c1a3|c1a3 c1a3a c1a3b c1a3c c1a3d"
	"Blast Pit|c1a4|c1a4 c1a4b c1a4d c1a4e c1a4f c1a4g c1a4i c1a4j c1a4k"
	"Power Up|c2a1|c2a1 c2a1a c2a1b"
	"On A Rail|c2a2|c2a2 c2a2a c2a2b1 c2a2b2 c2a2c c2a2d c2a2e c2a2f c2a2g c2a2h"
	"Apprehension|c2a3|c2a3 c2a3a c2a3b c2a3c c2a3d c2a3e"
	"Residue Processing|c2a4|c2a4 c2a4a c2a4b c2a4c c2a4d"
	"Questionable Ethics|c2a4e|c2a4e c2a4f c2a4g"
	"Surface Tension|c2a5|c2a5 c2a5a c2a5b c2a5c c2a5d c2a5e c2a5f c2a5g c2a5w c2a5x"
	"Forget About Freeman|c3a1|c3a1 c3a1a c3a1b"
	"Lambda Core|c3a2|c3a2 c3a2a c3a2b c3a2c c3a2d c3a2e c3a2f"
	"Xen|c4a1|c4a1 c4a1a c4a1b c4a1c c4a1d c4a1e c4a1f"
	"Gonarch's Lair|c4a2|c4a2 c4a2a c4a2b"
	"Interloper|c4a3|c4a3"
	"Nihilanth|c5a1|c5a1"
	"Hazard Course|t0a0|t0a0 t0a0a t0a0b t0a0b1"
)

declare -a MAPS
printf "chapter\tmap\tstatus\tmemory_peak\tblocker\tlog_path\n" > "$MAP_TSV"
printf "chapter\tclassification\tmaps_loaded\tmaps_total\tfirst_blocker\tevidence\n" > "$CHAPTER_TSV"

map_exists() {
	[[ -f "$MAP_SOURCE_DIR/${1%.bsp}.bsp" ]]
}

chapter_selected() {
	local chapter="$1"
	local selected
	if [[ ${#SELECT_CHAPTERS[@]} -eq 0 ]]; then
		return 0
	fi
	for selected in "${SELECT_CHAPTERS[@]}"; do
		[[ "$chapter" == "$selected" ]] && return 0
	done
	return 1
}

append_unique_map() {
	local candidate="$1"
	local existing
	for existing in "${MAPS[@]:-}"; do
		[[ "$existing" == "$candidate" ]] && return 0
	done
	MAPS+=("$candidate")
}

for row in "${CHAPTERS[@]}"; do
	IFS='|' read -r chapter representative full_maps <<<"$row"
	chapter_selected "$chapter" || continue
	if [[ "$MODE" == "full" ]]; then
		for map_name in $full_maps; do
			append_unique_map "$map_name"
		done
	else
		append_unique_map "$representative"
	fi
done

if [[ "$DRY_RUN" == "1" ]]; then
	for row in "${CHAPTERS[@]}"; do
		IFS='|' read -r chapter representative full_maps <<<"$row"
		chapter_selected "$chapter" || continue
		maps="$representative"
		[[ "$MODE" == "full" ]] && maps="$full_maps"
		for map_name in $maps; do
			if map_exists "$map_name"; then
				printf "%s\t%s\t%s\t%s\t%s\t%s\n" "$chapter" "$map_name" "NOT_TESTED" "N/A" "dry run" "N/A" >> "$MAP_TSV"
			else
				printf "%s\t%s\t%s\t%s\t%s\t%s\n" "$chapter" "$map_name" "MISSING" "N/A" "Map file not found" "N/A" >> "$MAP_TSV"
			fi
		done
	done
else
	if [[ ! -x "$MAP_COMPAT_SCRIPT" ]]; then
		echo "missing executable map compatibility script: $MAP_COMPAT_SCRIPT" >&2
		exit 1
	fi
	MAP_COMPAT_TIMEOUT="$PROBE_TIMEOUT" "$MAP_COMPAT_SCRIPT" "${MAPS[@]}" | tee "$LOG_BASE/map-compat.log"
	LATEST_MAP_LOG="$(ls -td .ai/logs/map-compat-* 2>/dev/null | head -n 1 || true)"
	if [[ -z "$LATEST_MAP_LOG" || ! -s "$LATEST_MAP_LOG/results.tsv" ]]; then
		echo "map compatibility probe did not produce results.tsv" >&2
		exit 1
	fi
	cp "$LATEST_MAP_LOG/results.tsv" "$LOG_BASE/raw-map-results.tsv"
	cp "$LATEST_MAP_LOG/summary.md" "$LOG_BASE/raw-map-summary.md" 2>/dev/null || true

	for row in "${CHAPTERS[@]}"; do
		IFS='|' read -r chapter representative full_maps <<<"$row"
		chapter_selected "$chapter" || continue
		maps="$representative"
		[[ "$MODE" == "full" ]] && maps="$full_maps"
		for map_name in $maps; do
			awk -F '\t' -v chapter="$chapter" -v map="$map_name" '
				NR > 1 && $1 == map {
					printf "%s\t%s\t%s\t%s\t%s\t%s\n", chapter, $1, $2, $3, $4, $5
				}
			' "$LATEST_MAP_LOG/results.tsv" >> "$MAP_TSV"
		done
	done
fi

for row in "${CHAPTERS[@]}"; do
	IFS='|' read -r chapter representative full_maps <<<"$row"
	chapter_selected "$chapter" || continue
	maps="$representative"
	[[ "$MODE" == "full" ]] && maps="$full_maps"
	total=0
	loaded=0
	blocker=""
	for map_name in $maps; do
		total=$((total + 1))
		line="$(awk -F '\t' -v chapter="$chapter" -v map="$map_name" 'NR > 1 && $1 == chapter && $2 == map {print; exit}' "$MAP_TSV")"
		status="$(printf "%s" "$line" | awk -F '\t' '{print $3}')"
		case "$status" in
			MAP_LOADED|MAP_READY) loaded=$((loaded + 1)) ;;
			"")
				[[ -z "$blocker" ]] && blocker="$map_name: no audit row"
				;;
			NOT_TESTED)
				[[ -z "$blocker" ]] && blocker="$map_name: not tested"
				;;
			*)
				[[ -z "$blocker" ]] && blocker="$map_name: $status $(printf "%s" "$line" | awk -F '\t' '{print $5}')"
				;;
		esac
	done
	if (( loaded == total && total > 0 )); then
		classification="playable"
	elif (( loaded > 0 )); then
		classification="partially_playable"
	elif [[ "$blocker" == *"not tested"* ]]; then
		classification="not_tested"
	else
		classification="blocked"
	fi
	printf "%s\t%s\t%s\t%s\t%s\t%s\n" "$chapter" "$classification" "$loaded" "$total" "${blocker:-none}" "$MAP_TSV" >> "$CHAPTER_TSV"
done

{
	echo "# GameCube Half-Life Campaign Audit"
	echo
	echo "- Generated: $(date -u +"%Y-%m-%dT%H:%M:%SZ")"
	echo "- Mode: $MODE"
	echo "- Dry run: $DRY_RUN"
	echo "- Map source: \`$MAP_SOURCE_DIR\`"
	echo "- Per-map timeout: ${PROBE_TIMEOUT}s"
	echo "- Map results: \`$MAP_TSV\`"
	echo "- Chapter results: \`$CHAPTER_TSV\`"
	echo
	echo "## Chapter Classification"
	echo
	echo "| Chapter | Classification | Loaded | Total | First Blocker |"
	echo "|---|---|---:|---:|---|"
	awk -F '\t' 'NR > 1 {printf "| %s | %s | %s | %s | %s |\n", $1, $2, $3, $4, $5}' "$CHAPTER_TSV"
	echo
	echo "## Map Evidence"
	echo
	echo "| Chapter | Map | Status | Memory Peak | Blocker | Log |"
	echo "|---|---|---|---|---|---|"
	awk -F '\t' 'NR > 1 {printf "| %s | %s | %s | %s | %s | %s |\n", $1, $2, $3, $4, $5, $6}' "$MAP_TSV"
} > "$SUMMARY"

echo "Campaign audit summary: $SUMMARY"
echo "Chapter results: $CHAPTER_TSV"
echo "Map results: $MAP_TSV"
