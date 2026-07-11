#!/usr/bin/env bash
# Run local-LLM GameCube autoport for a bounded wall-clock window with
# heartbeat monitoring so NO_EDIT / hung aider passes cannot sit forever.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || true)"
if [[ -z "$ROOT" ]]; then
	ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fi
cd "$ROOT"

if [[ -f "$ROOT/scripts/gamecube-env.sh" ]]; then
	# shellcheck disable=SC1091
	source "$ROOT/scripts/gamecube-env.sh"
fi

RUNTIME_SEC="${AI_MAX_RUNTIME_SEC:-$((8 * 60 * 60))}"
HEARTBEAT_STALE_SEC="${AI_HEARTBEAT_STALE_SEC:-1800}"
WATCH_INTERVAL_SEC="${AI_WATCH_INTERVAL_SEC:-60}"
DISCOVERY_MODE="${AI_AUTO_DISCOVERY:-prefer}"
LOG_DIR="$ROOT/.ai/logs"
mkdir -p "$LOG_DIR" "$ROOT/.ai/state"
LOGFILE="${GC_AUTOPORT_LOG:-$LOG_DIR/overnight-autoport-$(date +%Y%m%d-%H%M%S).log}"
HEARTBEAT="$ROOT/.ai/state/autoport-heartbeat.json"
WATCHDOG_PID_FILE="$ROOT/.ai/state/autoport-watchdog.pid"
RUNNER_PID_FILE="$ROOT/.ai/state/autoport-runner.pid"

export OPENAI_API_KEY="${OPENAI_API_KEY:-local}"
export OPENAI_API_BASE="${OPENAI_API_BASE:-http://127.0.0.1:8072/v1}"
export CUDA_VISIBLE_DEVICES="${CUDA_VISIBLE_DEVICES:-1}"
export XASH3D_GC_ROOT="$ROOT"
export AI_SKIP_FAILED_PASS_RESET="${AI_SKIP_FAILED_PASS_RESET:-1}"
export AI_KEEP_FAILED_PATCH="${AI_KEEP_FAILED_PATCH:-1}"
export AI_VERIFY_REPAIR_ATTEMPTS="${AI_VERIFY_REPAIR_ATTEMPTS:-2}"
# Prefer open source goals (G72/G82); use discovery only after goals stall.
export AI_AUTO_DISCOVERY="${AI_AUTO_DISCOVERY:-after-goals}"
DISCOVERY_MODE="${AI_AUTO_DISCOVERY}"
export AI_MAX_ATTEMPTS_PER_GOAL="${AI_MAX_ATTEMPTS_PER_GOAL:-0}"
export AI_DISCOVERY_STUCK_THRESHOLD="${AI_DISCOVERY_STUCK_THRESHOLD:-3}"
export AI_DISCOVERY_STUCK_BACKOFF="${AI_DISCOVERY_STUCK_BACKOFF:-60}"
export AI_MAX_RUNTIME_SEC="$RUNTIME_SEC"
export AI_VERIFY_REQUIRE_DOC_UPDATE="${AI_VERIFY_REQUIRE_DOC_UPDATE:-0}"
export AI_MEMORY_SUMMARY_CHARS="${AI_MEMORY_SUMMARY_CHARS:-1200}"
export AI_SOURCE_FIRST="${AI_SOURCE_FIRST:-1}"
export AIDER_OVERNIGHT="${AIDER_OVERNIGHT:-1}"
# Local 7B is 32k; do not spend most of the window on fake estimator padding.
export AIDER_MODEL_MAX_CONTEXT="${AIDER_MODEL_MAX_CONTEXT:-32768}"
export AIDER_SYSTEM_OVERHEAD_TOKENS="${AIDER_SYSTEM_OVERHEAD_TOKENS:-4096}"
export AIDER_CONFIG_PROMPT_SLACK_TOKENS="${AIDER_CONFIG_PROMPT_SLACK_TOKENS:-1024}"
export AIDER_RESERVED_OUTPUT_SLACK="${AIDER_RESERVED_OUTPUT_SLACK:-512}"
# Diffs were dying at ~192-512 output tokens; give overnight room for one hunk.
export AIDER_OUTPUT_TOKENS_INITIAL="${AIDER_OUTPUT_TOKENS_INITIAL:-2048}"
export AIDER_OUTPUT_TOKENS_RETRY_1="${AIDER_OUTPUT_TOKENS_RETRY_1:-1536}"
export AIDER_OUTPUT_TOKENS_RETRY_2="${AIDER_OUTPUT_TOKENS_RETRY_2:-1024}"
export AIDER_OUTPUT_TOKENS_RETRY_3="${AIDER_OUTPUT_TOKENS_RETRY_3:-768}"
export AIDER_MAX_CHAT_HISTORY_TOKENS="${AIDER_MAX_CHAT_HISTORY_TOKENS:-128}"
export AIDER_CONFIG="${AIDER_CONFIG:-.aider.overnight.conf.yml}"
export DOLPHIN_PROBE_MAX_LINES="${DOLPHIN_PROBE_MAX_LINES:-400}"
export PYTHONUNBUFFERED=1

rm -f "$ROOT/.ai/goal-supervisor.lock" "$ROOT/.ai/gc-port-loop.lock"
rm -f "$ROOT/.ai/state/discovery-supervisor.json"

echo "Logging to $LOGFILE"
echo "Runtime budget: ${RUNTIME_SEC}s (~$((RUNTIME_SEC / 3600))h)"
echo "Discovery mode: $DISCOVERY_MODE (stuck threshold=${AI_DISCOVERY_STUCK_THRESHOLD})"

start_runner() {
	# Wall-clock limit is enforced by the outer watchdog deadline plus timeout(1).
	# Keep argv compatible with ai-run-until-done.py (no --max-runtime-sec required).
	nohup timeout --foreground --signal=TERM --kill-after=60 "$RUNTIME_SEC" \
		./scripts/gamecube-autoport.sh goal-runner \
		--chunk-passes 0 \
		--max-cycles 0 \
		--recoverable-retries 8 \
		--discovery-mode "$DISCOVERY_MODE" \
		--sleep 20 \
		>>"$LOGFILE" 2>&1 &
	echo $! >"$RUNNER_PID_FILE"
	echo "Started goal-runner pid=$(cat "$RUNNER_PID_FILE")"
}

heartbeat_age_sec() {
	python3 - <<'PY'
from pathlib import Path
from datetime import datetime, timezone
import json
path = Path(".ai/state/autoport-heartbeat.json")
if not path.is_file():
	print(999999)
	raise SystemExit
try:
	payload = json.loads(path.read_text(encoding="utf-8"))
	ts = datetime.fromisoformat(payload["timestamp"])
	if ts.tzinfo is None:
		ts = ts.replace(tzinfo=timezone.utc)
	print(int((datetime.now(timezone.utc) - ts).total_seconds()))
except Exception:
	print(999999)
PY
}

kill_runner_tree() {
	local pid
	pid="$(cat "$RUNNER_PID_FILE" 2>/dev/null || true)"
	if [[ -n "${pid:-}" ]] && kill -0 "$pid" 2>/dev/null; then
		pkill -TERM -P "$pid" 2>/dev/null || true
		kill -TERM "$pid" 2>/dev/null || true
		sleep 2
		pkill -KILL -P "$pid" 2>/dev/null || true
		kill -KILL "$pid" 2>/dev/null || true
	fi
	# Leftover aider / probe children from a hung pass.
	pkill -TERM -f 'scripts/ai-aider-pass.sh' 2>/dev/null || true
	pkill -TERM -f 'aider --config .aider.automation.conf.yml' 2>/dev/null || true
	sleep 1
	pkill -KILL -f 'scripts/ai-aider-pass.sh' 2>/dev/null || true
	pkill -KILL -f 'aider --config .aider.automation.conf.yml' 2>/dev/null || true
}

deadline=$(( $(date +%s) + RUNTIME_SEC ))
start_runner
echo $$ >"$WATCHDOG_PID_FILE"

restarts=0
while (( $(date +%s) < deadline )); do
	sleep "$WATCH_INTERVAL_SEC"
	runner_pid="$(cat "$RUNNER_PID_FILE" 2>/dev/null || true)"
	if [[ -z "${runner_pid:-}" ]] || ! kill -0 "$runner_pid" 2>/dev/null; then
		remaining=$(( deadline - $(date +%s) ))
		if (( remaining < 120 )); then
			echo "$(date -Is) runner exited near deadline; not restarting" | tee -a "$LOGFILE"
			break
		fi
		echo "$(date -Is) runner exited early; restarting (remaining=${remaining}s)" | tee -a "$LOGFILE"
		restarts=$((restarts + 1))
		# Shrink remaining budget for the child.
		export AI_MAX_RUNTIME_SEC="$remaining"
		RUNTIME_SEC="$remaining"
		start_runner
		continue
	fi

	age="$(heartbeat_age_sec)"
	if (( age > HEARTBEAT_STALE_SEC )); then
		echo "$(date -Is) heartbeat stale (${age}s > ${HEARTBEAT_STALE_SEC}s); restarting runner" | tee -a "$LOGFILE"
		kill_runner_tree
		restarts=$((restarts + 1))
		remaining=$(( deadline - $(date +%s) ))
		if (( remaining < 120 )); then
			break
		fi
		export AI_MAX_RUNTIME_SEC="$remaining"
		RUNTIME_SEC="$remaining"
		rm -f "$ROOT/.ai/goal-supervisor.lock"
		start_runner
	fi
done

echo "$(date -Is) 8h window complete; stopping runner (restarts=$restarts)" | tee -a "$LOGFILE"
kill_runner_tree
rm -f "$WATCHDOG_PID_FILE" "$RUNNER_PID_FILE"
echo "Done. Log: $LOGFILE"
