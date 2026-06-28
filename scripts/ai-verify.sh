#!/usr/bin/env bash
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
BASELINE="${1:-}"
TMPDIR_AI="$(mktemp -d "${TMPDIR:-/tmp}/xash3d-gc-ai-verify.XXXXXX")"
trap 'rm -rf "$TMPDIR_AI"' EXIT

cd "$ROOT"

echo "== repository =="
git status --short
git diff --check
git diff --cached --check
git submodule foreach --quiet 'git diff --check'

echo
echo "== harness syntax =="
for shell_script in \
	scripts/ai-verify.sh \
	scripts/ai-aider-pass.sh \
	scripts/ai-loop.sh \
	scripts/ai-summarize-next-task.sh \
	scripts/build-gamecube.sh \
	scripts/xash3d-gc-aider-gui.sh \
	scripts/dolphin-boot-probe.sh \
	scripts/dolphin-probe-lock.sh \
	scripts/hlsdk-gamecube-probe.sh \
	scripts/hlsdk-gamecube-build.sh \
	scripts/gamecube-map-compat-probe.sh \
	scripts/gamecube-campaign-audit.sh \
	scripts/gamecube-rc-check.sh \
	scripts/gamecube-env.sh \
	scripts/ai-commit-gui-wip.sh
do
	bash -n "$shell_script"
done
python3 -c 'compile(open("scripts/build-gamecube-disc.py", encoding="utf-8").read(), "scripts/build-gamecube-disc.py", "exec")'

echo
echo "== probe script size guard =="
PROBE_SCRIPT="scripts/dolphin-boot-probe.sh"
PROBE_LINES="$(wc -l < "$PROBE_SCRIPT")"
PROBE_BYTES="$(stat -c '%s' "$PROBE_SCRIPT")"
PROBE_MAX_LINES="${DOLPHIN_PROBE_MAX_LINES:-300}"
PROBE_MAX_BYTES="${DOLPHIN_PROBE_MAX_BYTES:-15360}"
if (( PROBE_LINES > PROBE_MAX_LINES )); then
	echo "verify: $PROBE_SCRIPT has $PROBE_LINES lines (maximum $PROBE_MAX_LINES)" >&2
	exit 1
fi
if (( PROBE_BYTES > PROBE_MAX_BYTES )); then
	echo "verify: $PROBE_SCRIPT is ${PROBE_BYTES} bytes (maximum $PROBE_MAX_BYTES)" >&2
	exit 1
fi
echo "probe guard: $PROBE_SCRIPT ${PROBE_LINES} lines, ${PROBE_BYTES} bytes"

python3 -c 'compile(open("scripts/dolphin-probe-analyze.py", encoding="utf-8").read(), "scripts/dolphin-probe-analyze.py", "exec")'
python3 -c 'compile(open("scripts/xash3d-gc-aider-gui.py", encoding="utf-8").read(), "scripts/xash3d-gc-aider-gui.py", "exec")'
python3 -c 'compile(open("scripts/ai-goal-loop.py", encoding="utf-8").read(), "scripts/ai-goal-loop.py", "exec")'
python3 -c 'compile(open("scripts/ai-run-until-done.py", encoding="utf-8").read(), "scripts/ai-run-until-done.py", "exec")'
python3 -c 'compile(open("scripts/ai-evidence-gate.py", encoding="utf-8").read(), "scripts/ai-evidence-gate.py", "exec")'
python3 -c 'compile(open("scripts/gamecube-homebrew-compliance-check.py", encoding="utf-8").read(), "scripts/gamecube-homebrew-compliance-check.py", "exec")'
python3 -c 'compile(open("scripts/gamecube-quality-profile-check.py", encoding="utf-8").read(), "scripts/gamecube-quality-profile-check.py", "exec")'
python3 -c 'compile(open("scripts/hlsdk-gamecube-apply-patch.py", encoding="utf-8").read(), "scripts/hlsdk-gamecube-apply-patch.py", "exec")'
python3 -c 'compile(open("scripts/generate-hlsdk-gamecube-exports.py", encoding="utf-8").read(), "scripts/generate-hlsdk-gamecube-exports.py", "exec")'

echo
echo "== harness context pack =="
for context_file in \
	.ai/prompts/PROJECT_RULES.md \
	.ai/prompts/PORTING_PATTERNS.md \
	.ai/prompts/GAMECUBE_CONTEXT_INDEX.md \
	.ai/prompts/GAMECUBE_HARDWARE_NOTES.md \
	.ai/prompts/GAMECUBE_HOMEBREW_COMPLIANCE.md \
	.ai/prompts/GAMECUBE_AUDIO_NOTES.md \
	.ai/prompts/GAMECUBE_STORAGE_NOTES.md \
	.ai/prompts/GAMECUBE_GX_RENDERING_NOTES.md \
	.ai/prompts/GAMECUBE_NETWORKING_NOTES.md \
	.ai/prompts/GAMECUBE_MEMORY_BUDGET.md \
	.ai/prompts/GAMECUBE_FAILURE_MEMORY.md \
	.ai/prompts/GAMECUBE_LOCAL_EXAMPLES.md
do
	if [[ ! -s "$context_file" ]]; then
		echo "verify: missing GameCube context file: $context_file" >&2
		exit 1
	fi
done

scripts/gamecube-homebrew-compliance-check.py
scripts/gamecube-quality-profile-check.py

if command -v aider >/dev/null 2>&1; then
	aider --config .aider.conf.yml --help >/dev/null
else
	echo "verify: aider is not installed" >&2
	exit 1
fi

if [[ -n "$BASELINE" ]]; then
	echo
	echo "== autonomous patch safety =="
	git cat-file -e "$BASELINE^{commit}"
	if ! git merge-base --is-ancestor "$BASELINE" HEAD; then
		echo "verify: pass rewrote or left the baseline history" >&2
		exit 1
	fi

	if [[ "$BASELINE" == "$(git rev-parse HEAD)" ]]; then
		echo "verify: Aider did not create a commit" >&2
		exit 1
	fi

	commit_count="$(git rev-list --count "$BASELINE"..HEAD)"
	if (( commit_count > 3 )); then
		echo "verify: pass created $commit_count commits (maximum 3)" >&2
		exit 1
	fi

	if git diff --name-status "$BASELINE" | grep -q '^D'; then
		echo "verify: autonomous patch deletes tracked files" >&2
		git diff --name-status "$BASELINE" | grep '^D' >&2
		exit 1
	fi

	changed_count="$(git diff --name-only "$BASELINE" | sed '/^$/d' | wc -l)"
	if (( changed_count > 20 )); then
		echo "verify: patch changes $changed_count files (maximum 20)" >&2
		exit 1
	fi

	changed_lines="$(git diff --numstat "$BASELINE" | awk \
		'$3 != "scripts/xash3d-gc-aider-gui.py" && $3 != "scripts/xash3d-gc-aider-gui.sh" && \
		$1 != "-" && $2 != "-" { total += $1 + $2 } END { print total + 0 }')"
	if (( changed_lines > 2000 )); then
		echo "verify: patch changes $changed_lines lines (maximum 2000)" >&2
		exit 1
	fi

	if [[ "${AI_VERIFY_REQUIRE_DOC_UPDATE:-0}" == "1" ]]; then
		if ! git diff --name-only "$BASELINE" | \
			grep -qx 'docs/GAMECUBE_PORT_PLAN.md'; then
			echo "verify: patch did not update docs/GAMECUBE_PORT_PLAN.md" >&2
			exit 1
		fi
	fi

	scripts/ai-evidence-gate.py "$BASELINE" --repo "$ROOT"

	if [[ "${AI_GOAL_ID:-}" =~ ^G([0-9]+)$ ]] && (( BASH_REMATCH[1] >= 36 )); then
		changed_files="$(git diff --name-only "$BASELINE" | sed '/^$/d')"
		if [[ -n "$changed_files" ]] && \
			! grep -Eq '^(engine|ref|filesystem|common|public|stub|3rdparty/hlsdk-portable|docs/|\.ai/goals/|scripts/gamecube-rc-check\.sh|scripts/ai-goal-loop\.py)' <<<"$changed_files"; then
			echo "verify: G36+ patch has no source or tracked release-evidence change" >&2
			echo "$changed_files" >&2
			exit 1
		fi
		if [[ "${AI_ALLOW_PROBE_ONLY:-0}" != "1" && "${AI_G36_ALLOW_PROBE_CONTEXT:-0}" != "1" ]]; then
			non_probe_files="$(grep -Ev '^(scripts/dolphin-boot-probe\.sh|scripts/dolphin-probe-analyze\.py|scripts/gamecube-map-compat-probe\.sh|scripts/xash3d-gc-aider-gui\.(py|sh)|scripts/ai-(aider-pass|verify|goal-loop)\.(sh|py))$' <<<"$changed_files" || true)"
			if [[ -z "$non_probe_files" ]]; then
				echo "verify: G36+ probe/harness-only patch rejected; run scripts/gamecube-rc-check.sh or make a source/release-evidence change" >&2
				exit 1
			fi
		fi
	fi

	while IFS= read -r changed_file; do
		case "$changed_file" in
			*.c|*.cc|*.cpp|*.h|*.hpp|*.py|*.sh|*wscript)
				if git diff "$BASELINE" -- "$changed_file" | \
					grep -Eiq '^\+.*(DOLPHIN_SDK|REVOLUTION_SDK|RVL_SDK)'; then
					echo "verify: proprietary SDK reference added in $changed_file" >&2
					exit 1
				fi
				if git diff "$BASELINE" -- "$changed_file" | grep -Eq '^\+.*\bOSReport\b'; then
					echo "verify: OSReport added in $changed_file; use the established libogc/project reporting path" >&2
					exit 1
				fi
				;;
		esac
	done < <(git diff --name-only "$BASELINE")
fi

if [[ "${SKIP_GAMECUBE_BUILD:-0}" == "1" ]]; then
	echo
	echo "== GameCube build skipped by SKIP_GAMECUBE_BUILD =="
	printf 'verify: OK (build skipped)\n'
	exit 0
fi

DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
PPC_GCC="$DEVKITPRO/devkitPPC/bin/powerpc-eabi-gcc"

echo
echo "== build probe: GameCube toolchain =="
if [[ ! -x "$PPC_GCC" || ! -d "$DEVKITPRO/libogc" ]]; then
	echo "verify: devkitPPC/libogc not found under $DEVKITPRO" >&2
	echo "Set DEVKITPRO or use SKIP_GAMECUBE_BUILD=1 for harness-only checks." >&2
	exit 1
fi
echo "compiler: $PPC_GCC"
echo "libogc: $DEVKITPRO/libogc"

export DEVKITPRO
export PATH="$DEVKITPRO/devkitPPC/bin:$DEVKITPRO/tools/bin:$PATH"

echo
echo "== build probe: compile and link GameCube target =="
BUILD_LOG="$TMPDIR_AI/gamecube-build.log"
if ! scripts/build-gamecube.sh >"$BUILD_LOG" 2>&1; then
	echo "verify: GameCube build failed" >&2
	tail -120 "$BUILD_LOG" >&2
	exit 1
fi
tail -40 "$BUILD_LOG"

echo
echo "== build probe: required artifacts =="
if [[ ! -s OUT/bin/xash || ! -s OUT/bin/boot.dol ]]; then
	echo "verify: expected OUT/bin/xash and OUT/bin/boot.dol artifacts" >&2
	exit 1
fi
ls -lh OUT/bin/xash OUT/bin/boot.dol

echo
echo "verify: OK"
