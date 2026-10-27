#!/usr/bin/env bash
# TODO-CAM-018: deeper campaign visual proof (post-lab changelevel hops).
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT" || exit 1

STAMP="$(date +%Y%m%d-%H%M%S)"
LOG_DIR="${CAM018_LOG_DIR:-.ai/logs/campaign-visual-$STAMP}"
# Do not bake swissquit into Dolphin campaign/ISO probes (Swiss SD path only).
unset GC_SMOKE_SWISSQUIT
TIMEOUT="${CAM018_TIMEOUT:-300}"
mkdir -p "$LOG_DIR"

# chapter|from|to — tip-safe campaign spine (landmarks in dolphin-landmarks.sh)
HOPS=(
	"Black Mesa Inbound|c0a0e|c1a0"
	"Anomalous Materials|c1a0a|c1a0d"
	"Unforeseen Consequences|c1a1|c1a1a"
	"Office Complex|c1a2|c1a2a"
	"We've Got Hostiles|c1a3|c1a3d"
	"Blast Pit|c1a4|c1a4k"
	"Power Up|c2a1|c2a1b"
	"On A Rail|c2a2|c2a2a"
	"Apprehension|c2a3|c2a3a"
	"Residue Processing|c2a4|c2a4a"
	"Surface Tension|c2a5|c2a5w"
	"Forget About Freeman|c3a1|c3a1a"
	"Lambda Core|c3a2|c3a2a"
	"Xen|c4a1|c4a2"
	"Gonarch's Lair|c4a2|c4a2a"
	"Nihilanth|c4a3|c5a1"
)

SUMMARY="$LOG_DIR/summary.md"
{
	echo "# CAM-018 Campaign Visual Proof"
	echo
	echo "- Generated: $(date -u +"%Y-%m-%dT%H:%M:%SZ")"
	echo "- Timeout: ${TIMEOUT}s"
	echo
	echo "| Chapter | From | To | Status | CapFaces | Visual | Probe |"
	echo "|---|---|---|---|---|---|---|"
} >"$SUMMARY"

pass=0
fail=0

for row in "${HOPS[@]}"; do
	IFS='|' read -r chapter from to <<<"$row"
	echo "==> CAM-018 $chapter ($from → $to)"
	probe_log="$LOG_DIR/${from}-to-${to}.log"
	set +e
	env \
		DOLPHIN_NEWGAME=1 \
		DOLPHIN_SMOKE_MAP="$from" \
		DOLPHIN_CHANGELEVEL="$to" \
		DOLPHIN_TIMEOUT="$TIMEOUT" \
		DOLPHIN_FRAME_SAMPLE_SEC=8 \
		scripts/dolphin-boot-probe.sh >"$probe_log" 2>&1
	rc=$?
	set -e

	probe_dir="$(grep -E '^Logs: ' "$probe_log" | tail -n 1 | awk '{print $2}' || true)"
	status="FAIL"
	capfaces="none"
	nonblack="no"
	if grep -qsF "CHANGELEVEL_READY" "$probe_log"; then
		status="CHANGELEVEL_READY"
	elif [[ -n "$probe_dir" && -f "$probe_dir/stderr.log" ]] \
		&& grep -aqsF "map loaded ${to}" "$probe_dir/stderr.log"; then
		status="MAP_LOADED"
	fi

	if [[ -n "$probe_dir" && -f "$probe_dir/stderr.log" ]]; then
		if grep -aqsE "sampled_nonblack=1|VISUAL_STATUS: nonblack" "$probe_dir/stderr.log" "$probe_log" 2>/dev/null; then
			nonblack="yes"
		fi
		if grep -aqsF "low-res probe skips CapFaces" "$probe_dir/stderr.log"; then
			capfaces="tip-safe-skip"
		elif grep -aqsE "CapFaces sample ok map=${to}|CapFaces end map=${to}" "$probe_dir/stderr.log"; then
			capfaces="$(grep -aE "CapFaces sample ok map=${to}|CapFaces end map=${to}" "$probe_dir/stderr.log" \
				| tail -n 1 | grep -oE 'drawn=[0-9]+' | head -n 1 || echo "seen")"
		fi
	fi
	if grep -qsE "VISUAL_STATUS: nonblack|sampled_nonblack=1" "$probe_log"; then
		nonblack="yes"
	fi

	if [[ "$status" == "CHANGELEVEL_READY" && "$nonblack" == "yes" ]]; then
		pass=$((pass + 1))
	else
		fail=$((fail + 1))
	fi

	printf "| %s | %s | %s | %s | %s | nonblack=%s | %s |\n" \
		"$chapter" "$from" "$to" "$status" "$capfaces" "$nonblack" "${probe_dir:-exit=$rc}" >>"$SUMMARY"
	echo "  Result: $status capfaces=$capfaces nonblack=$nonblack"
done

{
	echo
	echo "- Pass: $pass"
	echo "- Fail: $fail"
	if (( pass >= 2 && fail == 0 )); then
		echo
		echo "**CAM018_VERIFY: PASS**"
	elif (( pass >= 1 )); then
		echo
		echo "**CAM018_VERIFY: PARTIAL**"
	else
		echo
		echo "**CAM018_VERIFY: FAIL**"
	fi
} >>"$SUMMARY"

cat "$SUMMARY"
(( pass >= 2 ))
