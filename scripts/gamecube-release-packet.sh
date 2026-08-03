#!/usr/bin/env bash
set -u

root="$(git rev-parse --show-toplevel)"
cd "$root" || exit 1
stamp="$(date +%Y%m%d-%H%M%S)"
packet=".ai/logs/dolphin-release-packet-$stamp"
mkdir -p "$packet"

latest_log_dir() {
	local phase_log="$1"
	sed -n 's/^Logs: //p' "$root/.ai/logs/supervisor/$phase_log" | tail -1
}

runtime_log="$(latest_log_dir runtime_probe.log)"
gameplay_log="$(latest_log_dir gameplay_probe.log)"
if [[ -z "$runtime_log" || -z "$gameplay_log" ]]; then
	echo "RELEASE_PACKET: FAIL"
	echo "missing supervisor runtime/gameplay log directories"
	exit 1
fi

python3 scripts/gamecube-reproducibility-check.py --log-dir "$packet/reproducibility" >"$packet/reproducibility.log" 2>&1 || true
python3 scripts/gamecube-memory-evidence.py \
	--log "$runtime_log/stderr.log" \
	--output "$packet/memory-report.json" \
	--markdown "$packet/memory-report.md"
python3 scripts/gamecube-audio-compliance.py --log-dir "$packet/audio" >"$packet/audio.log" 2>&1 || true

python3 scripts/gamecube-release-packet.py \
	--output "$packet" \
	--runtime-log "$runtime_log" \
	--gameplay-log "$gameplay_log" \
	--map-report .ai/logs/supervisor/map_compat_probe.log \
	--memory-report "$packet/memory-report.json" \
	--audio-report "$packet/audio" \
	--soak-report .ai/logs/dolphin-release-soak/report.json
