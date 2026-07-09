#!/usr/bin/env bash
# Start the unattended GameCube port automation loop.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

if [[ -f .env ]]; then
	set -a
	# shellcheck disable=SC1091
	source .env
	set +a
fi

if [[ -f scripts/gamecube-env.sh ]]; then
	# shellcheck disable=SC1091
	source scripts/gamecube-env.sh
fi

: "${OPENAI_API_KEY:=local}"
export OPENAI_API_KEY
export OPENAI_API_BASE="${OPENAI_API_BASE:-http://127.0.0.1:8072/v1}"
export GC_PORT_CONTINUOUS="${GC_PORT_CONTINUOUS:-1}"
export PYTHONUNBUFFERED=1

mkdir -p .ai/logs
LOGFILE="${GC_PORT_LOOP_LOG:-.ai/logs/gc-port-loop-$(date +%Y%m%d).log}"

exec python3 scripts/agent/gc_run_until_done.py "$@" 2>&1 | tee -a "$LOGFILE"
