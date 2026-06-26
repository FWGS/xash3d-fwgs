#!/usr/bin/env bash
# Shared non-secret automation defaults for the GameCube port.

gamecube_export_dolphin_env() {
	local flatpak_id="${DOLPHIN_FLATPAK_ID:-org.DolphinEmu.dolphin-emu}"
	local root="${XASH3D_GC_ROOT:-$(git rev-parse --show-toplevel 2>/dev/null || pwd)}"

	# If DOLPHIN_EXECUTABLE is already set by the environment, respect it.
	# Always export the flatpak ID as it may be needed by callers for cleanup
	# or specific flatpak operations even if a native binary is preferred.
	export DOLPHIN_FLATPAK_ID="$flatpak_id"

	if [[ -n "${DOLPHIN_EXECUTABLE:-}" ]]; then
		export DOLPHIN_EXECUTABLE
		return 0
	fi

	if [[ -x "$root/3rdparty/dolphin/build/Binaries/dolphin-emu" ]]; then
		DOLPHIN_EXECUTABLE="$root/3rdparty/dolphin/build/Binaries/dolphin-emu"
	elif [[ -x "$root/3rdparty/dolphin/build/Binaries/dolphin-emu-nogui" ]]; then
		DOLPHIN_EXECUTABLE="$root/3rdparty/dolphin/build/Binaries/dolphin-emu-nogui"
	elif [[ -x "$root/3rdparty/dolphin/build/dolphin-emu" ]]; then
		DOLPHIN_EXECUTABLE="$root/3rdparty/dolphin/build/dolphin-emu"
	elif [[ -x "$root/3rdparty/dolphin/build/dolphin-emu-nogui" ]]; then
		DOLPHIN_EXECUTABLE="$root/3rdparty/dolphin/build/dolphin-emu-nogui"
	elif command -v dolphin-emu >/dev/null 2>&1; then
		DOLPHIN_EXECUTABLE="$(command -v dolphin-emu)"
	elif command -v dolphin >/dev/null 2>&1; then
		DOLPHIN_EXECUTABLE="$(command -v dolphin)"
	elif command -v flatpak >/dev/null 2>&1 && \
		flatpak info "$flatpak_id" >/dev/null 2>&1; then
		DOLPHIN_EXECUTABLE="flatpak:$flatpak_id"
	else
		# No Dolphin found - set to empty but don't fail here
		# The calling script will handle this error
		DOLPHIN_EXECUTABLE=""
	fi

	if [[ -n "${DOLPHIN_EXECUTABLE:-}" ]]; then
		export DOLPHIN_EXECUTABLE
	fi
	
	return 0
}

gamecube_export_dolphin_env

# Local LLM / Aider automation defaults (override in .env).
: "${OPENAI_API_BASE:=http://127.0.0.1:8072/v1}"
: "${AIDER_SERVED_MODEL:=qwen-local}"
: "${QWABLE_5_MAX_NUM_SEQS:=1}"
: "${QWABLE_5_GPU_MEMORY_UTILIZATION:=0.85}"
: "${QWABLE_5_MAX_MODEL_LEN:=65536}"
: "${QWABLE_5_REASONING_PARSER:=}"
: "${AIDER_SYSTEM_OVERHEAD_TOKENS:=8192}"
: "${AIDER_MAX_CHAT_HISTORY_TOKENS:=1024}"
: "${AIDER_MODEL_MAX_CONTEXT:=$QWABLE_5_MAX_MODEL_LEN}"
: "${TARGET_FRAME_TIME:=16.67}"
: "${DOLPHIN_FRAME_SAMPLE_SEC:=8}"
: "${DOLPHIN_HARNESS_GOAL:=G36}"
: "${VLLM_USE_FLASHINFER_SAMPLER:=0}"
: "${AI_DIRTY_COMMIT_EXCLUDE:=scripts/xash3d-gc-aider-gui.py:.ai/state/xash3d-gc-aider-gui-settings.json}"
: "${AI_ENFORCE_EDITABLE_CONTEXT:=1}"
export OPENAI_API_BASE AIDER_SERVED_MODEL QWABLE_5_MAX_NUM_SEQS \
	QWABLE_5_GPU_MEMORY_UTILIZATION QWABLE_5_MAX_MODEL_LEN \
	QWABLE_5_REASONING_PARSER AIDER_SYSTEM_OVERHEAD_TOKENS AIDER_MAX_CHAT_HISTORY_TOKENS \
	AIDER_MODEL_MAX_CONTEXT TARGET_FRAME_TIME DOLPHIN_FRAME_SAMPLE_SEC \
	DOLPHIN_HARNESS_GOAL VLLM_USE_FLASHINFER_SAMPLER AI_DIRTY_COMMIT_EXCLUDE \
	AI_ENFORCE_EDITABLE_CONTEXT

gamecube_checkpoint_dirty_worktree() {
	local subject="$1"
	local body="${2:-Checkpoint uncommitted work before automation starts.}"
	local path

	if [[ -z "$(git status --porcelain)" ]]; then
		return 0
	fi

	git add -A
	IFS=':' read -ra _gc_exclude_paths <<< "$AI_DIRTY_COMMIT_EXCLUDE"
	for path in "${_gc_exclude_paths[@]}"; do
		[[ -n "$path" ]] || continue
		git restore --staged -- "$path" 2>/dev/null || true
	done
	if git diff --cached --quiet; then
		echo "gamecube-env: dirty worktree only had excluded GUI paths; skipping checkpoint" >&2
		return 0
	fi
	git commit -m "$subject" -m "$body"
}
