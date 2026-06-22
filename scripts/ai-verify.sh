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
bash -n scripts/ai-verify.sh scripts/ai-aider-pass.sh scripts/ai-loop.sh \
	scripts/ai-summarize-next-task.sh scripts/build-gamecube.sh \
	scripts/xash3d-gc-aider-gui.sh scripts/dolphin-boot-probe.sh \
	scripts/hlsdk-gamecube-probe.sh scripts/hlsdk-gamecube-build.sh
python3 -c 'compile(open("scripts/build-gamecube-disc.py", encoding="utf-8").read(), "scripts/build-gamecube-disc.py", "exec")'
python3 -c 'compile(open("scripts/xash3d-gc-aider-gui.py", encoding="utf-8").read(), "scripts/xash3d-gc-aider-gui.py", "exec")'
python3 -c 'compile(open("scripts/ai-goal-loop.py", encoding="utf-8").read(), "scripts/ai-goal-loop.py", "exec")'

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
		'$1 != "-" && $2 != "-" { total += $1 + $2 } END { print total + 0 }')"
	if (( changed_lines > 2000 )); then
		echo "verify: patch changes $changed_lines lines (maximum 2000)" >&2
		exit 1
	fi

	if ! git diff --name-only "$BASELINE" | \
		grep -qx 'docs/GAMECUBE_PORT_PLAN.md'; then
		echo "verify: patch did not update docs/GAMECUBE_PORT_PLAN.md" >&2
		exit 1
	fi

	while IFS= read -r changed_file; do
		case "$changed_file" in
			*.c|*.cc|*.cpp|*.h|*.hpp|*.py|*.sh|*wscript)
				if git diff "$BASELINE" -- "$changed_file" | \
					grep -Eiq '^\+.*(DOLPHIN_SDK|REVOLUTION_SDK|RVL_SDK)'; then
					echo "verify: proprietary SDK reference added in $changed_file" >&2
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
