#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [[ -f .env ]]; then
	set -a
	# shellcheck disable=SC1091
	source .env
	set +a
fi
# shellcheck disable=SC1091
source scripts/gamecube-env.sh

if gamecube_gui_wip_dirty; then
	echo "ai-commit-gui-wip: committing GUI changes"
	gamecube_commit_gui_wip
else
	echo "ai-commit-gui-wip: no GUI changes to commit"
fi
