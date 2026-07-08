#!/usr/bin/env bash
set -euo pipefail

PASSES="${1:-5}"
REPO="${2:-$(git rev-parse --show-toplevel 2>/dev/null || pwd)}"

if [[ ! "$PASSES" =~ ^[1-9][0-9]*$ ]]; then
	echo "usage: $0 [positive-pass-count] [repository]" >&2
	exit 2
fi

cd "$REPO"
mkdir -p .ai/logs .ai/state

for (( i = 1; i <= PASSES; i++ )); do
	echo "============================================================"
	echo "AI PASS $i / $PASSES"
	echo "============================================================"

	if ! scripts/ai-aider-pass.sh "$REPO" ".ai/tasks/current.md"; then
		echo "Pass $i failed. Stopping for human review." >&2
		git status --short >&2
		exit 1
	fi

	scripts/ai-summarize-next-task.sh
done
