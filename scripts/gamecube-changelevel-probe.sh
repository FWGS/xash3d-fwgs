#!/usr/bin/env bash
# G68: one representative changelevel sample per chapter group.
set -uo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT" || exit 1

LOG_BASE="${G68_CHANGELEVEL_LOG_DIR:-.ai/logs/changelevel-g68-$(date +%Y%m%d-%H%M%S)}"
TSV="$LOG_BASE/results.tsv"
MD="$LOG_BASE/summary.md"
PROBE_TIMEOUT="${MAP_PROBE_TIMEOUT:-150}"
mkdir -p "$LOG_BASE"

# chapter|from|to  (skip single-map chapters)
TRANSITIONS=(
	"Black Mesa Inbound|c0a0|c0a0a"
	"Anomalous Materials|c1a0|c1a0a"
	"Unforeseen Consequences|c1a1|c1a1a"
	"Office Complex|c1a2|c1a2a"
	"We've Got Hostiles|c1a3|c1a3a"
	"Blast Pit|c1a4|c1a4b"
	"Power Up|c2a1|c2a1a"
	"On A Rail|c2a2|c2a2a"
	"Apprehension|c2a3|c2a3a"
	"Residue Processing|c2a4|c2a4a"
	"Questionable Ethics|c2a4e|c2a4f"
	"Surface Tension|c2a5|c2a5a"
	"Forget About Freeman|c3a1|c3a1a"
	"Lambda Core|c3a2|c3a2a"
	"Xen|c4a1|c4a1a"
	"Gonarch's Lair|c4a2|c4a2a"
)

printf "chapter\tfrom\tto\tstatus\tlog_path\n" > "$TSV"
{
	echo "# G68 Changelevel Transition Samples"
	echo
	echo "- Generated: $(date -u +"%Y-%m-%dT%H:%M:%SZ")"
	echo "- Probe timeout: ${PROBE_TIMEOUT}s"
	echo
	echo "| Chapter | From | To | Status | Logs |"
	echo "|---|---|---|---|---|"
} > "$MD"

pass=0
fail=0
ISO_SENTINEL=""

for row in "${TRANSITIONS[@]}"; do
	IFS='|' read -r chapter from to <<<"$row"
	echo "==> Changelevel sample: $chapter ($from → $to)"
	env_args=(
		DOLPHIN_SMOKE_MAP="$from"
		DOLPHIN_CHANGELEVEL="$to"
		DOLPHIN_TIMEOUT="$PROBE_TIMEOUT"
		DOLPHIN_FRAME_SAMPLE_SEC=4
	)
	if [[ -n "$ISO_SENTINEL" && -f "$ISO_SENTINEL" ]]; then
		env_args+=(DOLPHIN_SKIP_BUILD=1 DOLPHIN_PREBUILT_ISO="$ISO_SENTINEL")
	fi

	set +e
	out=$(env "${env_args[@]}" bash scripts/dolphin-boot-probe.sh 2>&1)
	rc=$?
	set -e

	log_dir=$(printf "%s\n" "$out" | awk '/^Logs: / {print $2; found=1} END {exit found ? 0 : 1}' || true)
	if [[ -z "$log_dir" ]]; then
		log_dir="N/A"
	elif [[ -z "$ISO_SENTINEL" && -f "$log_dir/xash3d-gc.iso" ]]; then
		ISO_SENTINEL="$log_dir/xash3d-gc.iso"
	fi

	marker="Xash3D GameCube: G68 changelevel ready from=${from} to=${to}"
	status="FAIL"
	if printf "%s\n" "$out" | grep -qsF "G68 changelevel ready from=${from} to=${to}"; then
		status="PASS"
	elif [[ -n "$log_dir" && -f "$log_dir/stderr.log" ]] \
		&& grep -aqsF "$marker" "$log_dir/stderr.log"; then
		status="PASS"
	elif printf "%s\n" "$out" | grep -qsF "GUEST_FAILURE"; then
		status="GUEST_FAILURE"
	elif [[ $rc -ne 0 ]]; then
		status="TIMEOUT"
	fi

	printf "%s\t%s\t%s\t%s\t%s\n" "$chapter" "$from" "$to" "$status" "$log_dir" >> "$TSV"
	printf "| %s | %s | %s | %s | %s |\n" "$chapter" "$from" "$to" "$status" "$log_dir" >> "$MD"
	echo "  Result: $status ($log_dir)"
	if [[ "$status" == "PASS" ]]; then
		pass=$((pass + 1))
	else
		fail=$((fail + 1))
	fi
done

{
	echo
	echo "## Counts"
	echo
	echo "- PASS: $pass"
	echo "- FAIL: $fail"
} >> "$MD"

echo "==> G68 changelevel samples complete: PASS=$pass FAIL=$fail"
echo "    Summary: $MD"
if (( fail > 0 )); then
	echo "G68_CHANGELEVEL: PARTIAL"
	exit 1
fi
echo "G68_CHANGELEVEL: PASS"
exit 0
