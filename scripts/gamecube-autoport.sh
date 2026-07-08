#!/usr/bin/env bash
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

export XASH3D_GC_ROOT="$ROOT"
mkdir -p .ai/logs .ai/state .ai/goals

usage() {
	cat <<'EOF'
Usage: scripts/gamecube-autoport.sh [mode] [options]

Modes:
  goal-runner   Run the main evidence-gated auto-port loop (default).
  reagent       Run the compile/probe classifier once.
  autopilot     Run the legacy deterministic fixer loop.
  supervisor    Run the phased build/disc/Dolphin supervisor.

Examples:
  scripts/gamecube-autoport.sh
  scripts/gamecube-autoport.sh goal-runner --chunk-passes 20 --recoverable-retries 8
  scripts/gamecube-autoport.sh reagent
EOF
}

mode="${1:-goal-runner}"
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
