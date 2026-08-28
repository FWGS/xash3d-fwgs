#!/usr/bin/env bash
# TODO-GX-015: verify Flipper NPC studio texture bind on denser c1a0d route.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT" || exit 1

STAMP="$(date +%Y%m%d-%H%M%S)"
LOG_DIR="${GX015_LOG_DIR:-.ai/logs/npc-studio-verify-$STAMP}"
TIMEOUT="${GX015_TIMEOUT:-420}"

mkdir -p "$LOG_DIR"

# Tip-safe dual-hop: tram → AM → denser lab (proven G369/G371 route).
echo "==> GX-015 NPC studio verify c0a0e→c1a0→c1a0d (timeout=${TIMEOUT}s)"
PROBE_OUT="$LOG_DIR/probe.log"
set +e
env \
	DOLPHIN_NEWGAME=1 \
	DOLPHIN_SMOKE_MAP=c0a0e \
	DOLPHIN_CHANGELEVEL=c1a0 \
	DOLPHIN_CHANGELEVEL2=c1a0d \
	DOLPHIN_TIMEOUT="$TIMEOUT" \
	scripts/dolphin-boot-probe.sh >"$PROBE_OUT" 2>&1
PROBE_RC=$?
set -e

PROBE_DIR="$(grep -E '^Logs: ' "$PROBE_OUT" | tail -n 1 | awk '{print $2}' || true)"
SUMMARY="$LOG_DIR/summary.md"

check_log() {
	local pattern="$1"
	local label="$2"
	if [[ -n "$PROBE_DIR" && -f "$PROBE_DIR/stderr.log" ]] \
		&& grep -aqF "$pattern" "$PROBE_DIR/stderr.log" "$PROBE_DIR/stdout.log" 2>/dev/null; then
		echo "PASS: $label"
		return 0
	fi
	if grep -qF "$pattern" "$PROBE_OUT" 2>/dev/null; then
		echo "PASS: $label"
		return 0
	fi
	echo "FAIL: $label"
	return 1
}

FAILS=0
{
	echo "# GX-015 NPC Studio Verify"
	echo
	echo "- Generated: $(date -u +"%Y-%m-%dT%H:%M:%SZ")"
	echo "- Probe exit: $PROBE_RC"
	echo "- Probe log: \`$PROBE_OUT\`"
	echo "- Probe dir: \`${PROBE_DIR:-missing}\`"
	echo
	echo "## Checks"
	echo
} >"$SUMMARY"

run_check() {
	if check_log "$1" "$2" >>"$SUMMARY"; then
		:
	else
		FAILS=$((FAILS + 1))
	fi
}

run_check "CHANGELEVEL_READY" "changelevel continuity"
run_check "map loaded c1a0d" "dest map c1a0d"
run_check "G376 studio GX bind ok" "Flipper skin bind"
run_check "scientist.mdl gx_tris=" "scientist Flipper draw"

if [[ -n "$PROBE_DIR" && -f "$PROBE_DIR/stderr.log" ]] \
	&& grep -aqF "G377 lean studio anim explode fallback" "$PROBE_DIR/stderr.log" 2>/dev/null; then
	echo "FAIL: explode fallback seen" >>"$SUMMARY"
	FAILS=$((FAILS + 1))
else
	echo "PASS: no explode fallback" >>"$SUMMARY"
fi

{
	echo
	if (( FAILS == 0 && PROBE_RC == 0 )); then
		echo "**GX015_VERIFY: PASS**"
	else
		echo "**GX015_VERIFY: FAIL** ($FAILS check(s), probe exit $PROBE_RC)"
	fi
} >>"$SUMMARY"

cat "$SUMMARY"
(( FAILS == 0 && PROBE_RC == 0 ))
