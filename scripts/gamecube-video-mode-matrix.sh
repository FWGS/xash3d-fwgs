#!/usr/bin/env bash
# TODO-VIDEO-022: Dolphin NTSC 480i / progressive / PAL mode matrix.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT" || exit 1

STAMP="$(date +%Y%m%d-%H%M%S)"
LOG_DIR="${VIDEO022_LOG_DIR:-.ai/logs/video-mode-matrix-$STAMP}"
TIMEOUT="${VIDEO022_TIMEOUT:-120}"
SMOKE_MAP="${VIDEO022_SMOKE_MAP:-c0a0e}"
mkdir -p "$LOG_DIR"

# mode|expected_tag|progressive_expect
MODES=(
	"ntsc|ntsc480i|0"
	"prog|ntsc480p|1"
	"pal|pal528i|0"
)

SUMMARY="$LOG_DIR/summary.md"
{
	echo "# VIDEO-022 Region/Cable Mode Matrix"
	echo
	echo "- Generated: $(date -u +"%Y-%m-%dT%H:%M:%SZ")"
	echo "- Smoke map: \`$SMOKE_MAP\`"
	echo "- Timeout: ${TIMEOUT}s"
	echo
	echo "| Mode | Tag | Progressive | Status | Nonblack | Probe |"
	echo "|---|---|---|---|---|---|"
} >"$SUMMARY"

pass=0
fail=0

for row in "${MODES[@]}"; do
	IFS='|' read -r mode tag prog_expect <<<"$row"
	echo "==> VIDEO-022 mode=$mode (expect tag=$tag progressive=$prog_expect)"
	probe_log="$LOG_DIR/${mode}.log"
	set +e
	env \
		DOLPHIN_NEWGAME=1 \
		DOLPHIN_SMOKE_MAP="$SMOKE_MAP" \
		DOLPHIN_VIDEO_MODE="$mode" \
		DOLPHIN_TIMEOUT="$TIMEOUT" \
		DOLPHIN_FRAME_SAMPLE_SEC=6 \
		scripts/dolphin-boot-probe.sh >"$probe_log" 2>&1
	rc=$?
	set -e

	probe_dir="$(grep -E '^Logs: ' "$probe_log" | tail -n 1 | awk '{print $2}' || true)"
	status="FAIL"
	nonblack="no"
	got_tag="none"
	got_prog="?"

	if [[ -n "$probe_dir" && -f "$probe_dir/stderr.log" ]]; then
		marker="$(grep -aE 'VIDEO-022 mode=' "$probe_dir/stderr.log" | tail -n 1 || true)"
		if [[ -n "$marker" ]]; then
			got_tag="$(sed -n 's/.*VIDEO-022 mode=\([^ ]*\).*/\1/p' <<<"$marker")"
			got_prog="$(sed -n 's/.*progressive=\([01]\).*/\1/p' <<<"$marker")"
		fi
		if grep -aqsE 'sampled_nonblack=1|VISUAL_STATUS: nonblack' "$probe_dir/stderr.log" "$probe_log" 2>/dev/null; then
			nonblack="yes"
		fi
	fi
	if grep -qsE 'VISUAL_STATUS: nonblack|sampled_nonblack=1' "$probe_log"; then
		nonblack="yes"
	fi
	if grep -qsE 'MAP_READY|NEWGAME_READY|CHANGELEVEL_READY' "$probe_log"; then
		status="MAP_READY"
	fi

	ok=0
	if [[ "$got_tag" == "$tag" && "$got_prog" == "$prog_expect" && "$nonblack" == "yes" && "$status" == "MAP_READY" ]]; then
		ok=1
		pass=$((pass + 1))
		status="PASS"
	else
		fail=$((fail + 1))
		status="FAIL(tag=$got_tag prog=$got_prog)"
	fi

	printf "| %s | %s | %s | %s | nonblack=%s | %s |\n" \
		"$mode" "$tag" "$prog_expect" "$status" "$nonblack" "${probe_dir:-$probe_log}" >>"$SUMMARY"
	echo "  Result: $status nonblack=$nonblack tag=$got_tag progressive=$got_prog"
done

{
	echo
	echo "- Pass: $pass"
	echo "- Fail: $fail"
	echo
	if [[ "$fail" -eq 0 ]]; then
		echo "**VIDEO022_VERIFY: PASS**"
	else
		echo "**VIDEO022_VERIFY: FAIL**"
	fi
	echo
	echo "## Hardware sign-off (still required)"
	echo
	echo "- [ ] NTSC 480i analog/CRT capture on real GameCube"
	echo "- [ ] Progressive override (component) if claimed"
	echo "- [ ] PAL 528i safe fb + first nonblack on PAL console"
} >>"$SUMMARY"

cat "$SUMMARY"
[[ "$fail" -eq 0 ]]
