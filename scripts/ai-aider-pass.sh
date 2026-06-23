#!/usr/bin/env bash
set -euo pipefail

REPO="${1:-/home/tim/Desktop/xash3d-gc}"
TASK_FILE="${2:-.ai/tasks/current.md}"
CONTEXT_INPUTS=("${@:3}")

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
export AIDER_MODEL_TIMEOUT_SEC="${AIDER_MODEL_TIMEOUT_SEC:-1800}"
export AIDER_DISABLE_PLAYWRIGHT="${AIDER_DISABLE_PLAYWRIGHT:-true}"

command -v aider >/dev/null 2>&1 || {
	echo "ai-aider-pass: aider is not installed" >&2
	exit 1
}

[[ -f "$TASK_FILE" ]] || {
	echo "ai-aider-pass: task file not found: $TASK_FILE" >&2
	exit 1
}

CONTEXT_FILES=()
READ_CONTEXT_FILES=()
for context_file in "${CONTEXT_INPUTS[@]}"; do
	context_mode="file"
	if [[ "$context_file" == read:* ]]; then
		context_mode="read"
		context_file="${context_file#read:}"
	fi
	[[ -f "$context_file" ]] || {
		echo "ai-aider-pass: context file not found: $context_file" >&2
		exit 1
	}
	if [[ "$context_mode" == "read" ]]; then
		READ_CONTEXT_FILES+=("$context_file")
	else
		CONTEXT_FILES+=("$context_file")
	fi
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
TOKEN_LIMIT_RE="has hit a token limit|exceeds the .* token limit|context limit is exceeded|maximum context length|prompt contains at least|requested .* output tokens|VLLMValidationError"
AIDER_OUTPUT_TOKEN_BUDGETS=(
	"${AIDER_OUTPUT_TOKENS_INITIAL:-2048}"
	"${AIDER_OUTPUT_TOKENS_RETRY_1:-1024}"
	"${AIDER_OUTPUT_TOKENS_RETRY_2:-768}"
)
AIDER_CONTEXT_BYTE_LIMITS=(
	"${AIDER_CONTEXT_BYTES_INITIAL:-45000}"
	"${AIDER_CONTEXT_BYTES_RETRY_1:-20000}"
	"${AIDER_CONTEXT_BYTES_RETRY_2:-12000}"
)
TEMP_MODEL_SETTINGS=()

cleanup_temp_settings() {
	rm -f "${TEMP_MODEL_SETTINGS[@]}"
}
trap cleanup_temp_settings EXIT

cleanup_stale_git_lock() {
	local lock_file=".git/index.lock"
	local now lock_mtime age
	[[ -e "$lock_file" ]] || return 0
	if pgrep -af "git .*${REPO}" >/dev/null 2>&1; then
		return 0
	fi
	now="$(date +%s)"
	lock_mtime="$(stat -c '%Y' "$lock_file" 2>/dev/null || echo "$now")"
	age=$(( now - lock_mtime ))
	if (( age >= 30 )); then
		echo "ai-aider-pass: removing stale Git index lock (${age}s old)" >&2
		rm -f "$lock_file"
	fi
}

echo "== Aider pass: $STAMP =="
echo "Repo: $REPO"
echo "Task: $TASK_FILE"
if (( ${#CONTEXT_FILES[@]} )); then
	echo "Editable context: ${CONTEXT_FILES[*]}"
fi
if (( ${#READ_CONTEXT_FILES[@]} )); then
	echo "Read-only context: ${READ_CONTEXT_FILES[*]}"
fi
echo "Baseline: $BASELINE"
echo "Log: $LOG"

token_limit_seen() {
	local log_path="${1:-$LOG}"
	grep -Eiq "$TOKEN_LIMIT_RE" "$log_path"
}

write_model_settings() {
	local max_tokens="$1"
	local settings_file
	settings_file="$(mktemp .ai/logs/aider-model-settings-XXXXXX.yml)"
	TEMP_MODEL_SETTINGS+=("$settings_file")
	cat >"$settings_file" <<EOF
- name: openai/qwen-local
  edit_format: diff
  use_repo_map: false
  weak_model_name: null
  extra_params:
    max_tokens: $max_tokens
    chat_template_kwargs:
      enable_thinking: false
EOF
	printf '%s\n' "$settings_file"
}

context_args_for_attempt() {
	local attempt="$1"
	local limit="${AIDER_CONTEXT_BYTE_LIMITS[$(( attempt - 1 ))]}"
	local context_file size
	for context_file in "${CONTEXT_FILES[@]}"; do
		[[ -f "$context_file" ]] || continue
		if (( limit > 0 )); then
			size="$(stat -c '%s' "$context_file")"
			if (( size > limit )); then
				echo "ai-aider-pass: retry $attempt omits $context_file (${size} bytes > ${limit})" >&2
				continue
			fi
		fi
		printf '%s\n%s\n' --file "$context_file"
	done
	for context_file in "${READ_CONTEXT_FILES[@]}"; do
		[[ -f "$context_file" ]] || continue
		if (( limit > 0 )); then
			size="$(stat -c '%s' "$context_file")"
			if (( size > limit )); then
				echo "ai-aider-pass: retry $attempt omits read-only $context_file (${size} bytes > ${limit})" >&2
				continue
			fi
		fi
		printf '%s\n%s\n' --read "$context_file"
	done
}

run_aider_with_recovery() {
	local label="$1"
	shift
	local attempt attempt_log max_tokens model_settings status=0
	local settings_args=()
	local context_args=()
	for attempt in "${!AIDER_OUTPUT_TOKEN_BUDGETS[@]}"; do
		attempt=$(( attempt + 1 ))
		attempt_log="$(mktemp .ai/logs/aider-attempt-XXXXXX.log)"
		TEMP_MODEL_SETTINGS+=("$attempt_log")
		max_tokens="${AIDER_OUTPUT_TOKEN_BUDGETS[$(( attempt - 1 ))]}"
		context_args=()
		mapfile -t context_args < <(context_args_for_attempt "$attempt")
		settings_args=()
		if (( attempt > 1 )) || [[ "${AIDER_FORCE_TEMP_MODEL_SETTINGS:-1}" == "1" ]]; then
			model_settings="$(write_model_settings "$max_tokens")"
			settings_args=(--model-settings-file "$model_settings")
		fi
		if (( attempt > 1 )); then
			{
				echo
				echo "== ${label} recovery retry ${attempt}/${#AIDER_OUTPUT_TOKEN_BUDGETS[@]} =="
				echo "max_tokens: $max_tokens"
				echo "context files: $(( ${#context_args[@]} / 2 ))"
			} | tee -a "$LOG"
		fi
		set +e
		timeout --signal=TERM --kill-after=30 "$AIDER_MODEL_TIMEOUT_SEC" aider \
			--config .aider.conf.yml \
			--no-browser \
			--no-gui \
			--disable-playwright \
			--no-restore-chat-history \
			--no-auto-lint \
			--no-auto-test \
			--max-chat-history-tokens "${AIDER_MAX_CHAT_HISTORY_TOKENS:-2048}" \
			"${settings_args[@]}" \
			"${context_args[@]}" \
			"$@" \
			--yes-always \
			2>&1 | tee -a "$LOG" "$attempt_log"
		status="${PIPESTATUS[0]:-1}"
		set -e
		if (( status == 124 || status == 137 )); then
			return 17
		fi
		if token_limit_seen "$attempt_log"; then
			if (( attempt < ${#AIDER_OUTPUT_TOKEN_BUDGETS[@]} )); then
				echo "ai-aider-pass: ${label} hit a token/context limit; retrying with a smaller request" >&2
				continue
			fi
			return 18
		fi
		if (( status != 0 )); then
			return "$status"
		fi
		return 0
	done
	return 18
}

set +e
run_aider_with_recovery "Aider" --message-file "$TASK_FILE"
AIDER_STATUS="$?"
set -e

if (( AIDER_STATUS == 124 || AIDER_STATUS == 137 )); then
	echo "ai-aider-pass: Aider model call timed out after ${AIDER_MODEL_TIMEOUT_SEC}s; see $LOG" >&2
	exit 17
fi

if (( AIDER_STATUS == 17 )); then
	echo "ai-aider-pass: Aider model call timed out after ${AIDER_MODEL_TIMEOUT_SEC}s; see $LOG" >&2
	exit 17
fi

if (( AIDER_STATUS == 18 )); then
	echo "ai-aider-pass: Aider hit a token/context limit after autonomous retries; see $LOG" >&2
	exit 18
fi

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
	cleanup_stale_git_lock
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
	run_aider_with_recovery "autonomous repair" --message "$REPAIR_MESSAGE"
	REPAIR_STATUS="$?"
	set -e
	if (( REPAIR_STATUS == 124 || REPAIR_STATUS == 137 )); then
		echo "ai-aider-pass: autonomous repair timed out after ${AIDER_MODEL_TIMEOUT_SEC}s" >&2
		exit 17
	fi
	if (( REPAIR_STATUS == 17 )); then
		echo "ai-aider-pass: autonomous repair timed out after ${AIDER_MODEL_TIMEOUT_SEC}s" >&2
		exit 17
	fi
	if (( REPAIR_STATUS == 18 )); then
		echo "ai-aider-pass: autonomous repair hit a token/context limit after retries; see $LOG" >&2
		exit 18
	fi
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

cleanup_stale_git_lock
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
