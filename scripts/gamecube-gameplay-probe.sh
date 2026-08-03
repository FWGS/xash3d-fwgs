#!/usr/bin/env bash
set -u

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
probe_output="$(mktemp -d "${TMPDIR:-/tmp}/gc-gameplay.XXXXXX")"
trap 'rm -rf "$probe_output"' EXIT

set +e
DOLPHIN_SKIP_BUILD=1 \
DOLPHIN_NEWGAME=1 \
DOLPHIN_FULLPHYSICS=1 \
DOLPHIN_SMOKE_MAP="${DOLPHIN_SMOKE_MAP:-c0a0}" \
DOLPHIN_FRAME_SAMPLE_SEC="${DOLPHIN_FRAME_SAMPLE_SEC:-45}" \
"$repo_dir/scripts/dolphin-boot-probe.sh" OUT/xash3d-gc.iso >"$probe_output/probe.log" 2>&1
probe_code=$?
set -e

cat "$probe_output/probe.log"
log_dir="$(sed -n 's/^Logs: //p' "$probe_output/probe.log" | tail -1)"
if [[ "$log_dir" != /* ]]; then
	log_dir="$repo_dir/$log_dir"
fi
if [[ -z "$log_dir" || ! -d "$log_dir" ]]; then
	echo "GAMEPLAY_GATE: FAIL"
	echo "GAMEPLAY_MISSING: Dolphin probe did not report a log directory"
	exit 1
fi

python3 "$repo_dir/scripts/gamecube-gameplay-gate.py" --log-dir "$log_dir"
gate_code=$?
if (( probe_code != 0 || gate_code != 0 )); then
	exit 1
fi
