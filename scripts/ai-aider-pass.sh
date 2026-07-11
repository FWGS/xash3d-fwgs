#!/usr/bin/env bash
set -euo pipefail

REPO="${1:-$(git rev-parse --show-toplevel 2>/dev/null || pwd)}"
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
if [ -f scripts/gamecube-env.sh ]; then
	source scripts/gamecube-env.sh
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

if [[ -z "${AI_FORBIDDEN_EDIT_PATHS:-}" ]] &&
	[[ "${AI_COMMIT_SUBJECT:-}" =~ ^perf:\ (reduce\ GameCube\ runtime\ frame\ cost|close\ GameCube\ worst-case\ scenes)$ ]]; then
	AI_FORBIDDEN_EDIT_PATHS="engine/platform/gamecube/sys_gamecube.c"
fi

CONTEXT_FILES=()
READ_CONTEXT_FILES=()
REQUIRED_CONTEXT_FILES=()
ALLOWED_EDIT_PATHS=()
RAW_CONTEXT_SPECS=()

edit_path_forbidden() {
	local candidate="$1"
	local forbidden
	[[ -n "${AI_FORBIDDEN_EDIT_PATHS:-}" ]] || return 1
	IFS=',' read -r -a _forbidden_paths <<<"$AI_FORBIDDEN_EDIT_PATHS"
	for forbidden in "${_forbidden_paths[@]}"; do
		forbidden="${forbidden#"${forbidden%%[![:space:]]*}"}"
		forbidden="${forbidden%"${forbidden##*[![:space:]]}"}"
		[[ -n "$forbidden" ]] || continue
		if [[ "$candidate" == "$forbidden" || "$candidate" == "$forbidden"/* ]]; then
			return 0
		fi
	done
	return 1
}

cleanup_forbidden_dirty_paths() {
	local status path
	[[ -n "${AI_FORBIDDEN_EDIT_PATHS:-}" ]] || return 0
	while IFS= read -r status; do
		[[ -n "$status" ]] || continue
		path="${status:3}"
		if [[ "$path" == *" -> "* ]]; then
			path="${path##* -> }"
		fi
		if edit_path_forbidden "$path"; then
			echo "ai-aider-pass: discarding forbidden dirty path before checkpoint: $path" >&2
			git restore --staged -- "$path" 2>/dev/null || true
			git restore --worktree -- "$path" 2>/dev/null || true
		fi
	done < <(git status --porcelain)
}

for context_file in "${CONTEXT_INPUTS[@]}"; do
	context_mode="file"
	if [[ "$context_file" == read:* ]]; then
		context_mode="read"
		context_file="${context_file#read:}"
	elif [[ "$context_file" == required:* ]]; then
		context_mode="required"
		context_file="${context_file#required:}"
	elif [[ "$context_file" == slice-read:* ]]; then
		context_mode="slice"
		RAW_CONTEXT_SPECS+=("$context_file")
		continue
	fi
	[[ -f "$context_file" ]] || {
		echo "ai-aider-pass: context file not found: $context_file" >&2
		exit 1
	}
	RAW_CONTEXT_SPECS+=("${context_mode}:${context_file}")
	if [[ "$context_mode" == "read" ]]; then
		READ_CONTEXT_FILES+=("$context_file")
	elif [[ "$context_mode" == "required" ]]; then
		CONTEXT_FILES+=("$context_file")
		REQUIRED_CONTEXT_FILES+=("$context_file")
	else
		CONTEXT_FILES+=("$context_file")
	fi
done

drop_ephemeral_discovery_state() {
	local state_file=".ai/state/discovery-supervisor.json"
	if [[ -e "$state_file" ]]; then
		rm -f "$state_file"
	fi
}

dirty_status_without_ephemeral_state() {
	drop_ephemeral_discovery_state
	cleanup_forbidden_dirty_paths
	git status --porcelain
}

if gamecube_gui_wip_dirty && [[ "${AI_SKIP_DIRTY_CHECKPOINT:-0}" != "1" ]]; then
	echo "ai-aider-pass: committing standalone GUI changes before goal pass" >&2
	gamecube_commit_gui_wip || exit 2
fi
if [[ -n "$(dirty_status_without_ephemeral_state)" ]]; then
	if [[ "${AI_SKIP_DIRTY_CHECKPOINT:-0}" == "1" ]]; then
		echo "ai-aider-pass: leaving remaining dirty files for goal-loop checkpoint" >&2
		git status --short >&2
	else
		DIRTY_COMMIT_SUBJECT="${AI_DIRTY_COMMIT_SUBJECT:-chore: checkpoint dirty automation state}"
		DIRTY_COMMIT_BODY="${AI_DIRTY_COMMIT_BODY:-Checkpoint existing uncommitted changes before automation starts.}"
		echo "ai-aider-pass: dirty worktree detected; creating checkpoint commit: $DIRTY_COMMIT_SUBJECT" >&2
		git status --short >&2
		gamecube_checkpoint_dirty_worktree "$DIRTY_COMMIT_SUBJECT" "$DIRTY_COMMIT_BODY"
	fi
fi

mkdir -p .ai/logs

STAMP="$(date +%F-%H%M%S)"
LOG=".ai/logs/aider-pass-$STAMP.log"
BASELINE="$(git rev-parse HEAD)"
TOKEN_LIMIT_RE="has hit a token limit|exceeds the .* token limit|context limit is exceeded|maximum context length|prompt contains at least|requested .* output tokens|VLLMValidationError|Empty response received from LLM"
AIDER_OUTPUT_TOKEN_BUDGETS=(
	"${AIDER_OUTPUT_TOKENS_INITIAL:-4096}"
	"${AIDER_OUTPUT_TOKENS_RETRY_1:-2048}"
	"${AIDER_OUTPUT_TOKENS_RETRY_2:-1024}"
	"${AIDER_OUTPUT_TOKENS_RETRY_3:-768}"
)
AIDER_CONTEXT_BYTE_LIMITS=(
	"${AIDER_CONTEXT_BYTES_INITIAL:-45000}"
	"${AIDER_CONTEXT_BYTES_RETRY_1:-20000}"
	"${AIDER_CONTEXT_BYTES_RETRY_2:-12000}"
	"${AIDER_CONTEXT_BYTES_RETRY_3:-8000}"
)
AIDER_CONFIG="${AIDER_CONFIG:-}"
if [[ -z "$AIDER_CONFIG" ]]; then
	if [[ "${AIDER_AUTOMATION:-1}" == "1" && -f .aider.automation.conf.yml ]]; then
		AIDER_CONFIG=".aider.automation.conf.yml"
	else
		AIDER_CONFIG=".aider.conf.yml"
	fi
fi
TEMP_MODEL_SETTINGS=()
BUDGETED_CONTEXT_ACTIVE=0

cleanup_temp_settings() {
	rm -f "${TEMP_MODEL_SETTINGS[@]}"
}
trap cleanup_temp_settings EXIT

cleanup_stale_git_lock() {
	local min_age="${1:-30}"
	local lock_file=".git/index.lock"
	local now lock_mtime age
	[[ -e "$lock_file" ]] || return 0
	if pgrep -af "git .*${REPO}" >/dev/null 2>&1; then
		return 0
	fi
	now="$(date +%s)"
	lock_mtime="$(stat -c '%Y' "$lock_file" 2>/dev/null || echo "$now")"
	age=$(( now - lock_mtime ))
	if (( age >= min_age )); then
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
echo "Aider config: $AIDER_CONFIG"
if [[ -n "${DOLPHIN_EXECUTABLE:-}" ]]; then
	echo "Dolphin: $DOLPHIN_EXECUTABLE"
else
	echo "Dolphin: unavailable"
fi

token_limit_seen() {
	local log_path="${1:-$LOG}"
	grep -Eiq "$TOKEN_LIMIT_RE" "$log_path"
}

load_token_budget() {
	local attempt="${1:-1}"
	if ! command -v python3 >/dev/null 2>&1 || [[ ! -f scripts/aider-token-budget.py ]]; then
		return 0
	fi
	local override_output_initial="${AIDER_OUTPUT_TOKENS_INITIAL:-}"
	local override_output_retry_1="${AIDER_OUTPUT_TOKENS_RETRY_1:-}"
	local override_output_retry_2="${AIDER_OUTPUT_TOKENS_RETRY_2:-}"
	local override_output_retry_3="${AIDER_OUTPUT_TOKENS_RETRY_3:-}"
	local override_history="${AIDER_MAX_CHAT_HISTORY_TOKENS:-}"
	local budget_file
	budget_file="$(mktemp .ai/logs/aider-budget-XXXXXX.env)"
	TEMP_MODEL_SETTINGS+=("$budget_file")
	if AIDER_BUDGET_ATTEMPT="$attempt" python3 scripts/aider-token-budget.py \
		--attempt "$attempt" --quiet >"$budget_file" 2>/dev/null; then
		set -a
		# shellcheck disable=SC1090
		source "$budget_file"
		set +a
		[[ -n "$override_output_initial" ]] && AIDER_OUTPUT_TOKENS_INITIAL="$override_output_initial"
		[[ -n "$override_output_retry_1" ]] && AIDER_OUTPUT_TOKENS_RETRY_1="$override_output_retry_1"
		[[ -n "$override_output_retry_2" ]] && AIDER_OUTPUT_TOKENS_RETRY_2="$override_output_retry_2"
		[[ -n "$override_output_retry_3" ]] && AIDER_OUTPUT_TOKENS_RETRY_3="$override_output_retry_3"
		[[ -n "$override_history" ]] && AIDER_MAX_CHAT_HISTORY_TOKENS="$override_history"
		AIDER_OUTPUT_TOKEN_BUDGETS=(
			"${AIDER_OUTPUT_TOKENS_INITIAL:-4096}"
			"${AIDER_OUTPUT_TOKENS_RETRY_1:-2048}"
			"${AIDER_OUTPUT_TOKENS_RETRY_2:-1024}"
			"${AIDER_OUTPUT_TOKENS_RETRY_3:-768}"
		)
		AIDER_CONTEXT_BYTE_LIMITS=(
			"${AIDER_CONTEXT_BYTES_INITIAL:-45000}"
			"${AIDER_CONTEXT_BYTES_RETRY_1:-20000}"
			"${AIDER_CONTEXT_BYTES_RETRY_2:-12000}"
			"${AIDER_CONTEXT_BYTES_RETRY_3:-8000}"
		)
		echo "ai-aider-pass: token budget attempt=$attempt output=${AIDER_OUTPUT_TOKEN_BUDGETS[0]} history=${AIDER_MAX_CHAT_HISTORY_TOKENS:-2048} context_bytes=${AIDER_CONTEXT_BYTE_LIMITS[0]}" >&2
	fi
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

is_required_context_file() {
	local candidate="$1"
	local required_file
	for required_file in "${REQUIRED_CONTEXT_FILES[@]}"; do
		[[ "$candidate" == "$required_file" ]] && return 0
	done
	return 1
}

materialize_slice_read() {
	local spec="$1"
	local source="${spec#slice-read:}"
	local rel="${source%%:*}"
	local ranges="${source#*:}"
	local slice_file
	slice_file="$(python3 scripts/aider-context-slice.py \
		--source "$rel" --ranges "$ranges" 2>/dev/null)" || return 1
	[[ -f "$slice_file" ]] || return 1
	printf '%s' "$slice_file"
}

rebuild_context_arrays() {
	local spec context_mode context_path
	CONTEXT_FILES=()
	READ_CONTEXT_FILES=()
	REQUIRED_CONTEXT_FILES=()
	for spec in "${BUDGETED_CONTEXT_SPECS[@]}"; do
		context_mode="file"
		context_path="$spec"
		if [[ "$spec" == read:* ]]; then
			context_mode="read"
			context_path="${spec#read:}"
		elif [[ "$spec" == required:* ]]; then
			context_mode="required"
			context_path="${spec#required:}"
		elif [[ "$spec" == slice-read:* ]]; then
			context_path="$(materialize_slice_read "$spec")" || continue
			context_mode="read"
		fi
		[[ -f "$context_path" ]] || continue
		if [[ "$context_mode" == "read" ]]; then
			READ_CONTEXT_FILES+=("$context_path")
		elif [[ "$context_mode" == "required" ]]; then
			CONTEXT_FILES+=("$context_path")
			REQUIRED_CONTEXT_FILES+=("$context_path")
		else
			CONTEXT_FILES+=("$context_path")
		fi
	done
}

load_budgeted_context() {
	local attempt="$1"
	local max_tokens="$2"
	local -a budget_args=()
	local spec
	if [[ ! -f scripts/aider-context-budget.py ]]; then
		BUDGETED_CONTEXT_SPECS=("${RAW_CONTEXT_SPECS[@]}")
		rebuild_context_arrays
		return 0
	fi
	budget_args=(python3 scripts/aider-context-budget.py --repo "$REPO"
		--attempt "$attempt" --output-tokens "$max_tokens")
	for spec in "${RAW_CONTEXT_SPECS[@]}"; do
		budget_args+=("$spec")
	done
	mapfile -t BUDGETED_CONTEXT_SPECS < <("${budget_args[@]}" 2>/dev/null) || true
	BUDGETED_CONTEXT_ACTIVE=1
	if (( ${#BUDGETED_CONTEXT_SPECS[@]} == 0 )); then
		BUDGETED_CONTEXT_SPECS=("${RAW_CONTEXT_SPECS[@]}")
		BUDGETED_CONTEXT_ACTIVE=0
	fi
	rebuild_context_arrays
	echo "ai-aider-pass: budget attempt=$attempt files=${#CONTEXT_FILES[@]} reads=${#READ_CONTEXT_FILES[@]}" >&2
}

preflight_context_estimate() {
	local attempt="$1"
	local max_tokens="$2"
	local max_context="${AIDER_MODEL_MAX_CONTEXT:-65536}"
	if ! command -v python3 >/dev/null 2>&1 || [[ ! -f scripts/aider-context-estimate.py ]]; then
		return 0
	fi
	local -a estimate_args=()
	local estimate_output estimate_status
	estimate_args=(python3 scripts/aider-context-estimate.py --repo "$REPO"
		--attempt "$attempt" --output-tokens "$max_tokens" --max-context "$max_context"
		--task-file "$TASK_FILE")
	local spec context_file
	if (( BUDGETED_CONTEXT_ACTIVE )); then
		for context_file in "${CONTEXT_FILES[@]}"; do
			if is_required_context_file "$context_file"; then
				estimate_args+=("required:${context_file}")
			else
				estimate_args+=("$context_file")
			fi
		done
		for context_file in "${READ_CONTEXT_FILES[@]}"; do
			estimate_args+=("read:${context_file}")
		done
	else
		for spec in "${RAW_CONTEXT_SPECS[@]}"; do
			estimate_args+=("$spec")
		done
	fi
	estimate_output="$("${estimate_args[@]}" --quiet 2>&1)" || estimate_status=$?
	estimate_status="${estimate_status:-0}"
	if (( estimate_status == 0 )); then
		return 0
	fi
	if [[ "$estimate_output" == *"OVER_BUDGET"* ]]; then
		echo "ai-aider-pass: pre-flight estimate over budget for attempt=$attempt (output=$max_tokens)" >&2
		echo "$estimate_output" >&2
		return 1
	fi
	echo "ai-aider-pass: pre-flight estimate unavailable; continuing without preflight" >&2
	[[ -n "$estimate_output" ]] && echo "$estimate_output" >&2
	return 0
}

editable_context_args_for_attempt() {
	local attempt="$1"
	local limit="${AIDER_CONTEXT_BYTE_LIMITS[$(( attempt - 1 ))]}"
	local context_file size
	for context_file in "${CONTEXT_FILES[@]}"; do
		[[ -f "$context_file" ]] || continue
		if (( ! BUDGETED_CONTEXT_ACTIVE && limit > 0 )) && ! is_required_context_file "$context_file"; then
			size="$(stat -c '%s' "$context_file")"
			if (( size > limit )); then
				echo "ai-aider-pass: repair retry $attempt omits $context_file (${size} bytes > ${limit})" >&2
				continue
			fi
		fi
		printf '%s\n%s\n' --file "$context_file"
	done
}

context_args_for_attempt() {
	local attempt="$1"
	local limit="${AIDER_CONTEXT_BYTE_LIMITS[$(( attempt - 1 ))]}"
	local context_file size
	for context_file in "${CONTEXT_FILES[@]}"; do
		[[ -f "$context_file" ]] || continue
		if (( ! BUDGETED_CONTEXT_ACTIVE && limit > 0 )) && ! is_required_context_file "$context_file"; then
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

editable_context_arg_count() {
	local arg
	local count=0
	for arg in "$@"; do
		if [[ "$arg" == "--file" ]]; then
			count=$(( count + 1 ))
		fi
	done
	printf '%s\n' "$count"
}

run_aider_with_recovery() {
	local label="$1"
	local editable_only=0
	shift
	if [[ "${1:-}" == "--editable-only" ]]; then
		editable_only=1
		shift
	fi
	local attempt attempt_log max_tokens model_settings status=0
	local settings_args=()
	local context_args=()
	for attempt in "${!AIDER_OUTPUT_TOKEN_BUDGETS[@]}"; do
		attempt=$(( attempt + 1 ))
		load_token_budget "$attempt"
		attempt_log="$(mktemp .ai/logs/aider-attempt-XXXXXX.log)"
		TEMP_MODEL_SETTINGS+=("$attempt_log")
		max_tokens="${AIDER_OUTPUT_TOKEN_BUDGETS[$(( attempt - 1 ))]}"
		load_budgeted_context "$attempt" "$max_tokens"
		if ! preflight_context_estimate "$attempt" "$max_tokens"; then
			if (( attempt < ${#AIDER_OUTPUT_TOKEN_BUDGETS[@]} )); then
				echo "ai-aider-pass: pre-flight context estimate over budget; retrying with a smaller request" >&2
				continue
			fi
			return 18
		fi
		context_args=()
		if (( editable_only )); then
			mapfile -t context_args < <(editable_context_args_for_attempt "$attempt")
		else
			mapfile -t context_args < <(context_args_for_attempt "$attempt")
		fi
		local editable_count
		editable_count="$(editable_context_arg_count "${context_args[@]}")"
		if (( editable_count == 0 )); then
			echo "ai-aider-pass: no editable context remains after budgeting on attempt=$attempt" >&2
			return 19
		fi
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
			--config "$AIDER_CONFIG" \
			--no-browser \
			--no-gui \
			--no-detect-urls \
			--no-auto-accept-architect \
			--no-cache-prompts \
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
if command -v python3 >/dev/null 2>&1 && [[ -f scripts/aider-token-budget.py ]]; then
	python3 scripts/aider-token-budget.py --sync-metadata --quiet >/dev/null 2>&1 || true
fi
load_token_budget "${AIDER_BUDGET_ATTEMPT:-1}"
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
	echo "ai-aider-pass: flattening unexpected Aider commit back to staged changes" >&2
	git reset --soft "$BASELINE"
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
COMMIT_BODY="${AI_COMMIT_BODY:-}"

edit_path_allowed() {
	local candidate="$1"
	local context_file
	for context_file in "${ALLOWED_EDIT_PATHS[@]}"; do
		if [[ "$candidate" == "$context_file" ]]; then
			return 0
		fi
	done
	return 1
}

build_allowed_edit_paths() {
	local path extra
	ALLOWED_EDIT_PATHS=()
	for path in "${CONTEXT_FILES[@]}" "${REQUIRED_CONTEXT_FILES[@]}"; do
		ALLOWED_EDIT_PATHS+=("$path")
	done
	if [[ "${AI_VERIFY_REQUIRE_DOC_UPDATE:-0}" == "1" ]]; then
		for path in docs/GAMECUBE_PORT_PLAN.md .ai/goals/GAMECUBE_PORT_GOALS.md; do
			if [[ -f "$path" ]] && ! edit_path_allowed "$path"; then
				ALLOWED_EDIT_PATHS+=("$path")
			fi
		done
	fi
	if [[ -n "${AI_ALLOWED_EDIT_EXTRA:-}" ]]; then
		IFS=',' read -r -a _extra_paths <<<"$AI_ALLOWED_EDIT_EXTRA"
		for extra in "${_extra_paths[@]}"; do
			extra="${extra#"${extra%%[![:space:]]*}"}"
			extra="${extra%"${extra##*[![:space:]]}"}"
			[[ -n "$extra" ]] || continue
			if ! edit_path_allowed "$extra"; then
				ALLOWED_EDIT_PATHS+=("$extra")
			fi
		done
	fi
}

reject_out_of_scope_edits() {
	local changed_file
	if [[ "${AI_ENFORCE_EDITABLE_CONTEXT:-1}" != "1" ]]; then
		return 0
	fi
	build_allowed_edit_paths
	while IFS= read -r changed_file; do
		[[ -n "$changed_file" ]] || continue
		if edit_path_forbidden "$changed_file"; then
			echo "ai-aider-pass: forbidden edit for this pass: $changed_file" >&2
			echo "ai-aider-pass: forbidden files: ${AI_FORBIDDEN_EDIT_PATHS}" >&2
			return 21
		fi
		if ! edit_path_allowed "$changed_file"; then
			echo "ai-aider-pass: edit outside loaded editable context: $changed_file" >&2
			echo "ai-aider-pass: allowed editable files: ${ALLOWED_EDIT_PATHS[*]}" >&2
			return 16
		fi
	done < <(git diff --cached --name-only)
	return 0
}

unstage_out_of_scope_edits() {
	local staged_file
	if [[ "${AI_ENFORCE_EDITABLE_CONTEXT:-1}" != "1" ]]; then
		return 0
	fi
	build_allowed_edit_paths
	while IFS= read -r staged_file; do
		[[ -n "$staged_file" ]] || continue
		if edit_path_forbidden "$staged_file"; then
			echo "ai-aider-pass: unstaging forbidden edit for this pass: $staged_file" >&2
			git restore --staged -- "$staged_file" 2>/dev/null || true
			git restore --worktree -- "$staged_file" 2>/dev/null || true
			continue
		fi
		if ! edit_path_allowed "$staged_file"; then
			echo "ai-aider-pass: unstaging edit outside loaded editable context: $staged_file" >&2
			git restore --staged -- "$staged_file" 2>/dev/null || true
			git restore --worktree -- "$staged_file" 2>/dev/null || true
		fi
	done < <(git diff --cached --name-only)
}

stage_and_validate_patch() {
	cleanup_stale_git_lock
	git add -A
	gamecube_unstage_excluded_paths
	unstage_out_of_scope_edits
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
	if ! reject_out_of_scope_edits; then
		return 16
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

verification_summary() {
	local verify_log="$1"
	python3 - "$verify_log" <<'PY'
from pathlib import Path
import re
import sys

path = Path(sys.argv[1])
lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
patterns = (
	re.compile(r":\d+:\d+: (fatal )?error:"),
	re.compile(r"syntax error near unexpected token"),
	re.compile(r"undefined reference"),
	re.compile(r"implicit declaration"),
	re.compile(r"Build failed"),
	re.compile(r"verify: .*failed"),
	re.compile(r"verify: .*rejected"),
	re.compile(r"task in '.*' failed"),
)
matches = []
for line in lines:
	if any(pattern.search(line) for pattern in patterns):
		matches.append(line)

if not matches:
	for line in lines[-20:]:
		if line.strip():
			matches.append(line)

for line in matches[-24:]:
	print(line[:240])
PY
}

repair_context_files_for_verify_log() {
	local verify_log="$1"
	shift
	python3 - "$verify_log" "$@" <<'PY'
from pathlib import Path
import re
import sys

log_path = Path(sys.argv[1])
context_files = list(sys.argv[2:])
markers = ("engine/", "ref/", "common/", "filesystem/", "public/", "stub/")


def normalize(path: str) -> str:
	path = path.replace("\\", "/")
	while path.startswith("../"):
		path = path[3:]
	if path.startswith("./"):
		path = path[2:]
	for marker in markers:
		index = path.find(marker)
		if index >= 0:
			return path[index:]
	return path


context_by_norm = {normalize(path): path for path in context_files}
text = log_path.read_text(encoding="utf-8", errors="replace")
path_re = re.compile(
	r"(?P<path>(?:\.\./|\./|/)?[^\s:]*"
	r"(?:engine|ref|common|filesystem|public|stub)/[^\s:]+)"
	r":\d+(?::\d+)?:"
)

for match in path_re.finditer(text):
	normalized = normalize(match.group("path"))
	if normalized in context_by_norm:
		print(context_by_norm[normalized])
		raise SystemExit(0)

for path in context_files:
	print(path)
PY
}

keep_failed_patch_enabled() {
	[[ "${AI_SKIP_FAILED_PASS_RESET:-0}" =~ ^(1|true|TRUE|yes)$ ]] || \
		[[ "${AI_KEEP_FAILED_PATCH:-0}" =~ ^(1|true|TRUE|yes)$ ]]
}

discard_failed_patch() {
	if keep_failed_patch_enabled; then
		# Leave the broken patch in the worktree so the next pass can repair it
		# instead of burning cycles on edit→verify-fail→wipe thrash.
		git reset >/dev/null 2>&1 || true
		echo "ai-aider-pass: keeping failed patch for repair (AI_SKIP_FAILED_PASS_RESET/AI_KEEP_FAILED_PATCH)" >&2
		git status --short >&2 || true
		return 0
	fi
	git reset >/dev/null 2>&1 || true
	local changed=()
	mapfile -t changed < <(git diff --name-only)
	if (( ${#changed[@]} )); then
		git restore -- "${changed[@]}" >/dev/null 2>&1 || true
	fi
}

stage_and_validate_patch
VERIFY_LOG=".ai/logs/aider-verify-$STAMP-1.log"
echo
echo "== pre-commit verifier =="
if ! run_precommit_verifier "$VERIFY_LOG"; then
	# Default to two repair attempts when keeping failed patches overnight.
	if keep_failed_patch_enabled; then
		VERIFY_REPAIR_ATTEMPTS="${AI_VERIFY_REPAIR_ATTEMPTS:-2}"
	else
		VERIFY_REPAIR_ATTEMPTS="${AI_VERIFY_REPAIR_ATTEMPTS:-1}"
	fi
	ORIGINAL_CONTEXT_FILES=("${CONTEXT_FILES[@]}")
	ORIGINAL_READ_CONTEXT_FILES=("${READ_CONTEXT_FILES[@]}")
	ORIGINAL_REQUIRED_CONTEXT_FILES=("${REQUIRED_CONTEXT_FILES[@]}")
	ORIGINAL_RAW_CONTEXT_SPECS=("${RAW_CONTEXT_SPECS[@]}")
	repair_round=0
	verify_ok=0
	while (( repair_round < VERIFY_REPAIR_ATTEMPTS )); do
		repair_round=$((repair_round + 1))
		echo "ai-aider-pass: verification failed; autonomous repair ${repair_round}/${VERIFY_REPAIR_ATTEMPTS}" >&2
		cleanup_stale_git_lock 0
		git reset
		mapfile -t REPAIR_CONTEXT_FILES < <(repair_context_files_for_verify_log "$VERIFY_LOG" "${ORIGINAL_CONTEXT_FILES[@]}")
		if (( ${#REPAIR_CONTEXT_FILES[@]} == 0 )); then
			REPAIR_CONTEXT_FILES=("${ORIGINAL_CONTEXT_FILES[@]}")
		fi
		# Prefer already-dirty source files so repair stays on the broken patch.
		mapfile -t DIRTY_REPAIR_FILES < <(git diff --name-only --diff-filter=ACMR -- 'engine/' 'ref/' 'common/' 2>/dev/null || true)
		if (( ${#DIRTY_REPAIR_FILES[@]} )); then
			REPAIR_CONTEXT_FILES=("${DIRTY_REPAIR_FILES[@]}")
		fi
		CONTEXT_FILES=("${REPAIR_CONTEXT_FILES[@]}")
		READ_CONTEXT_FILES=()
		REQUIRED_CONTEXT_FILES=()
		RAW_CONTEXT_SPECS=()
		for context_file in "${CONTEXT_FILES[@]}"; do
			RAW_CONTEXT_SPECS+=("file:$context_file")
		done
		echo "ai-aider-pass: repair context narrowed to: ${CONTEXT_FILES[*]}" >&2
		REPAIR_ALLOWED="$(printf '%s\n' "${CONTEXT_FILES[@]}")"
		REPAIR_MESSAGE="$(printf '%s\n' \
			'The current uncommitted patch failed verification. Fix the compiler or verifier failure now.' \
			'There is no interactive human. Do not ask questions, explain options, or only propose commands.' \
			'Make the smallest safe edit in the already loaded editable file or files only, and do not commit.' \
			'Do not add or edit helper scripts mentioned by logs, diagnostics, or progress output.' \
			'Do not add files merely because they appear in build progress logs.' \
			'' 'Editable files for this repair pass:' "$REPAIR_ALLOWED" \
			'' 'Verification errors:'; verification_summary "$VERIFY_LOG")"
		set +e
		run_aider_with_recovery "autonomous repair ${repair_round}" --editable-only --message "$REPAIR_MESSAGE"
		REPAIR_STATUS="$?"
		set -e
		CONTEXT_FILES=("${ORIGINAL_CONTEXT_FILES[@]}")
		READ_CONTEXT_FILES=("${ORIGINAL_READ_CONTEXT_FILES[@]}")
		REQUIRED_CONTEXT_FILES=("${ORIGINAL_REQUIRED_CONTEXT_FILES[@]}")
		RAW_CONTEXT_SPECS=("${ORIGINAL_RAW_CONTEXT_SPECS[@]}")
		if (( REPAIR_STATUS == 124 || REPAIR_STATUS == 137 || REPAIR_STATUS == 17 )); then
			echo "ai-aider-pass: autonomous repair timed out after ${AIDER_MODEL_TIMEOUT_SEC}s" >&2
			discard_failed_patch
			exit 17
		fi
		if (( REPAIR_STATUS == 18 )); then
			echo "ai-aider-pass: autonomous repair hit a token/context limit after retries; see $LOG" >&2
			discard_failed_patch
			exit 18
		fi
		if (( REPAIR_STATUS != 0 )); then
			echo "ai-aider-pass: autonomous repair exited $REPAIR_STATUS" >&2
			if (( repair_round >= VERIFY_REPAIR_ATTEMPTS )); then
				discard_failed_patch
				exit "$REPAIR_STATUS"
			fi
			continue
		fi
		if [[ "$BASELINE" != "$(git rev-parse HEAD)" ]]; then
			echo "ai-aider-pass: flattening unexpected repair commit back to staged changes" >&2
			git reset --soft "$BASELINE"
		fi
		if ! stage_and_validate_patch; then
			if (( repair_round >= VERIFY_REPAIR_ATTEMPTS )); then
				discard_failed_patch
				exit 15
			fi
			continue
		fi
		VERIFY_LOG=".ai/logs/aider-verify-$STAMP-r${repair_round}.log"
		echo
		echo "== repaired pre-commit verifier (round ${repair_round}) =="
		if run_precommit_verifier "$VERIFY_LOG"; then
			verify_ok=1
			break
		fi
	done
	if (( ! verify_ok )); then
		if keep_failed_patch_enabled; then
			echo "ai-aider-pass: repaired patch still fails; keeping it for the next goal pass" >&2
		else
			echo "ai-aider-pass: repaired patch still fails; discarding failed patch" >&2
		fi
		discard_failed_patch
		exit 15
	fi
fi

cleanup_stale_git_lock
if [[ -n "$COMMIT_BODY" ]]; then
	git commit -m "$COMMIT_SUBJECT" -m "$COMMIT_BODY"
else
	git commit -m "$COMMIT_SUBJECT"
fi

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
