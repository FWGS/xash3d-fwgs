#!/usr/bin/env bash
set -euo pipefail

REPO="${1:-/home/tim/Desktop/xash3d-gc}"
TASK_FILE="${2:-.ai/tasks/current.md}"
CONTEXT_FILES=("${@:3}")

cd "$REPO"
REPO="$(git rev-parse --show-toplevel)"
cd "$REPO"

# Load local env if present
if [ -f .env ]; then
	set -a
	source .env
	set +a
fi

: "${OPENAI_API_KEY:?Set OPENAI_API_KEY first}"
export OPENAI_API_BASE="${OPENAI_API_BASE:-http://127.0.0.1:8072/v1}"

command -v aider >/dev/null 2>&1 || {
	echo "ai-aider-pass: aider is not installed" >&2
	exit 1
}

[[ -f "$TASK_FILE" ]] || {
	echo "ai-aider-pass: task file not found: $TASK_FILE" >&2
	exit 1
}

AIDER_FILE_ARGS=()
for context_file in "${CONTEXT_FILES[@]}"; do
	[[ -f "$context_file" ]] || {
		echo "ai-aider-pass: context file not found: $context_file" >&2
		exit 1
	}
	AIDER_FILE_ARGS+=(--file "$context_file")
done

if [[ -n "$(git status --porcelain)" ]]; then
	echo "ai-aider-pass: refusing to run with a dirty worktree" >&2
	git status --short >&2
	echo "Commit or stash these changes explicitly before an autonomous pass." >&2
	exit 1
fi

mkdir -p .ai/logs

STAMP="$(date +%F-%H%M%S)"
LOG=".ai/logs/aider-pass-$STAMP.log"
BASELINE="$(git rev-parse HEAD)"

echo "== Aider pass: $STAMP =="
echo "Repo: $REPO"
echo "Task: $TASK_FILE"
if (( ${#CONTEXT_FILES[@]} )); then
	echo "Editable context: ${CONTEXT_FILES[*]}"
fi
echo "Baseline: $BASELINE"
echo "Log: $LOG"

set +e
aider \
	--config .aider.conf.yml \
	"${AIDER_FILE_ARGS[@]}" \
	--message-file "$TASK_FILE" \
	--yes-always \
	2>&1 | tee "$LOG"
AIDER_STATUS="${PIPESTATUS[0]}"
set -e

if (( AIDER_STATUS != 0 )); then
	echo "ai-aider-pass: Aider exited $AIDER_STATUS; see $LOG" >&2
	exit "$AIDER_STATUS"
fi

if [[ "$BASELINE" == "$(git rev-parse HEAD)" ]]; then
	if [[ -n "$(git status --porcelain)" ]]; then
		echo "ai-aider-pass: Aider left uncommitted changes; stopping for review" >&2
		git status --short >&2
		exit 11
	fi
	echo "ai-aider-pass: Aider made no edit; see $LOG" >&2
	exit 10
fi

echo
echo "== verifier =="
set +e
scripts/ai-verify.sh "$BASELINE" 2>&1 | tee -a "$LOG"
VERIFY_STATUS="${PIPESTATUS[0]}"
set -e

if (( VERIFY_STATUS != 0 )); then
	echo "ai-aider-pass: verification failed after $BASELINE; see $LOG" >&2
	exit "$VERIFY_STATUS"
fi

echo
echo "== accepted patch =="
git log --oneline "$BASELINE"..HEAD
git diff --stat "$BASELINE"..HEAD
