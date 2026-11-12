#!/usr/bin/env bash
# Score a swiss-evidence.txt (or SD mount) against G38/G66 HW sign-off markers.
set -euo pipefail
ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

LOG="${1:-}"
if [[ -z "$LOG" ]]; then
	echo "Usage: $0 <swiss-evidence.txt|sd-mount>" >&2
	exit 2
fi
if [[ -d "$LOG" ]]; then
	for cand in \
		"$LOG/xash3d/valve/logs/swiss-evidence.txt" \
		"$LOG/valve/logs/swiss-evidence.txt" \
		"$LOG/swiss-evidence.txt"
	do
		[[ -f "$cand" ]] && { LOG="$cand"; break; }
	done
fi
[[ -f "$LOG" ]] || { echo "FAIL: no evidence file: $LOG" >&2; exit 2; }

pass=0
fail=0
check() {
	local name="$1" pat="$2"
	if grep -qE "$pat" "$LOG"; then
		echo "PASS  $name"
		pass=$((pass + 1))
	else
		echo "FAIL  $name"
		fail=$((fail + 1))
	fi
}

echo "== G38/G66 checklist: $LOG =="
check "FAT preferred" "FAT preferred volume"
check "FAT volume ready" "FAT volume ready (sd:|carda:|cardb:)/"
check "FAT shutdown / Swiss return" "FAT shutdown \\(return to Swiss loader via exit stub\\)"
check "map loaded" "map loaded "
check "entity or playable" "entity lump spawn ready|playable present .*nonblack=1"
check "playable nonblack" "playable present .*nonblack=1"
check "controller G45" "G45 controller ready"
check "audio voice" "audio voice started"
check "save probe" "save probe ready"
check "changelevel continuity" "G68 changelevel ready|changelevel begin|map loaded c0a0a"
check "dest playable (c0a0a)" "playable present map=c0a0a"
echo "== result: pass=$pass fail=$fail =="
(( fail == 0 ))
