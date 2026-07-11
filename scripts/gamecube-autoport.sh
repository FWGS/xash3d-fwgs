#!/usr/bin/env bash
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || true)"
if [[ -z "$ROOT" ]]; then
	ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fi
cd "$ROOT"

if [[ -f "$ROOT/.ai/MANUAL_PORT_LOCK" || -f "/home/tim/Desktop/xash3d-gc-locks/MANUAL_PORT_LOCK" ]]; then
	echo "ERROR: manual port lock is set; refusing gamecube-autoport." >&2
	exit 75
fi

if [[ -f "$ROOT/scripts/gamecube-env.sh" ]]; then
	# shellcheck disable=SC1091
	source "$ROOT/scripts/gamecube-env.sh"
fi

export XASH3D_GC_ROOT="$ROOT"
mkdir -p .ai/logs .ai/state .ai/goals

usage() {
	cat <<'EOF'
Usage: scripts/gamecube-autoport.sh [mode] [options]

Modes:
  port-loop     Run the supervisor-driven GameCube port loop (default).
  goal-runner   Run the legacy goal-ledger auto-port loop.
  reagent       Run the compile/probe classifier once.
  autopilot     Run the legacy deterministic fixer loop.
  supervisor    Run the phased build/disc/Dolphin supervisor.

Examples:
  scripts/gamecube-autoport.sh
  scripts/gamecube-autoport.sh goal-runner --chunk-passes 20 --recoverable-retries 8
  scripts/gamecube-autoport.sh reagent
EOF
}

mode="${1:-port-loop}"
if [[ $# -gt 0 ]]; then
	shift
fi

case "$mode" in
	-h|--help|help)
		usage
		exit 0
		;;
	goal-runner)
		exec python3 scripts/ai-run-until-done.py --repo "$ROOT" "$@"
		;;
	port-loop)
		exec bash scripts/gc-port-loop.sh "$@"
		;;
	reagent)
		exec python3 scripts/agent/gc_reagent.py "$@"
		;;
	autopilot)
		exec python3 scripts/agent/gc_autopilot.py --repo "$ROOT" "$@"
		;;
	supervisor)
		exec python3 scripts/agent/gc_port_supervisor.py "$@"
		;;
	*)
		echo "Unknown mode: $mode" >&2
		usage >&2
		exit 2
		;;
esac
