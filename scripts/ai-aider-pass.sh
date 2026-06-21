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

if [[ "$BASELINE" != "$(git rev-parse HEAD)" ]]; then
	echo "ai-aider-pass: Aider created an unexpected commit; stopping for review" >&2
	exit 12
fi

if [[ -z "$(git status --porcelain)" ]]; then
	echo "ai-aider-pass: Aider made no edit; see $LOG" >&2
	exit 10
fi

COMMIT_SUBJECT="${AI_COMMIT_SUBJECT:-}"
if (( ${#COMMIT_SUBJECT} > 72 )) || \
	[[ ! "$COMMIT_SUBJECT" =~ ^(fix|feat|build|chore|ci|docs|style|refactor|perf|test):\ [[:alnum:]] ]]; then
	echo "ai-aider-pass: invalid deterministic commit subject: $COMMIT_SUBJECT" >&2
	exit 13
fi

stage_and_validate_patch() {
	git add -A
	git diff --cached --check
	if git diff --cached --quiet; then
		echo "ai-aider-pass: no staged edit remains after Aider response" >&2
		return 10
	fi
	if ! git diff --cached --diff-filter=D --quiet --; then
		echo "ai-aider-pass: Aider deleted tracked files; stopping for review" >&2
		git diff --cached --name-status --diff-filter=D >&2
		return 14
	fi
}

run_precommit_verifier() {
	local verify_log="$1"
	set +e
	scripts/ai-verify.sh 2>&1 | tee -a "$LOG" "$verify_log"
	local status="${PIPESTATUS[0]}"
	set -e
	return "$status"
}

stage_and_validate_patch
VERIFY_LOG=".ai/logs/aider-verify-$STAMP-1.log"
echo
echo "== pre-commit verifier =="
if ! run_precommit_verifier "$VERIFY_LOG"; then
	echo "ai-aider-pass: first verification failed; requesting one autonomous repair" >&2
	git reset
	REPAIR_MESSAGE="$(printf '%s\n' \
		'The current uncommitted patch failed verification. Fix the compiler or verifier failure now.' \
		'There is no interactive human. Do not ask questions, explain options, or only propose commands.' \
		'Make the smallest safe edit, preserve the goal and documentation, and do not commit.' \
		'' 'Verification tail:'; tail -80 "$VERIFY_LOG")"
	set +e
	aider \
		--config .aider.conf.yml \
		"${AIDER_FILE_ARGS[@]}" \
		--message "$REPAIR_MESSAGE" \
		--yes-always \
		2>&1 | tee -a "$LOG"
	REPAIR_STATUS="${PIPESTATUS[0]}"
	set -e
	if (( REPAIR_STATUS != 0 )); then
		echo "ai-aider-pass: autonomous repair exited $REPAIR_STATUS" >&2
		exit "$REPAIR_STATUS"
	fi
	if [[ "$BASELINE" != "$(git rev-parse HEAD)" ]]; then
		echo "ai-aider-pass: repair created an unexpected commit" >&2
		exit 12
	fi
	stage_and_validate_patch
	VERIFY_LOG=".ai/logs/aider-verify-$STAMP-2.log"
	echo
	echo "== repaired pre-commit verifier =="
	if ! run_precommit_verifier "$VERIFY_LOG"; then
		echo "ai-aider-pass: repaired patch still fails; leaving it for review" >&2
		exit 15
	fi
fi

git commit -m "$COMMIT_SUBJECT"

echo
echo "== post-commit safety =="
set +e
SKIP_GAMECUBE_BUILD=1 scripts/ai-verify.sh "$BASELINE" 2>&1 | tee -a "$LOG"
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
