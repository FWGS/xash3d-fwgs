#!/usr/bin/env bash
# Commit, push, and record GameCube-owned submodule updates in the parent repo.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

SYNC_SUBMODULES=(
	3rdparty/dolphin
	3rdparty/library_suffix
)

usage() {
	cat <<'EOF'
usage: scripts/gamecube-submodule-sync.sh [--push-only] [--no-parent-commit]

For each GameCube-owned submodule:
  1. commit any staged/unstaged tracked changes inside the submodule
  2. push the current branch to origin
  3. stage the updated gitlink in the parent repo

Unless --no-parent-commit is set, create one parent commit when gitlinks move.
Use --push-only to skip in-submodule commits and only push existing HEADs.
EOF
}

PUSH_ONLY=0
PARENT_COMMIT=1

while [[ $# -gt 0 ]]; do
	case "$1" in
		--push-only) PUSH_ONLY=1 ;;
		--no-parent-commit) PARENT_COMMIT=0 ;;
		-h|--help) usage; exit 0 ;;
		*) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
	esac
	shift
done

submodule_branch() {
	git -C "$1" rev-parse --abbrev-ref HEAD
}

submodule_ready() {
	git -C "$1" rev-parse HEAD >/dev/null 2>&1
}

submodule_dirty_tracked() {
	! git -C "$1" diff --quiet --ignore-submodules=untracked 2>/dev/null ||
		! git -C "$1" diff --cached --quiet 2>/dev/null
}

submodule_ahead_of_origin() {
	local path="$1"
	local branch remote head remote_head

	branch="$(submodule_branch "$path")"
	[[ "$branch" != "HEAD" ]] || return 1

	remote="$(git -C "$path" rev-parse --abbrev-ref --symbolic-full-name '@{u}' 2>/dev/null || true)"
	if [[ -z "$remote" ]]; then
		remote="origin/$branch"
	fi

	head="$(git -C "$path" rev-parse HEAD)"
	remote_head="$(git -C "$path" rev-parse "$remote" 2>/dev/null || true)"
	[[ -n "$remote_head" && "$head" != "$remote_head" ]]
}

sync_one_submodule() {
	local path="$1"
	local branch subject recorded actual

	if ! submodule_ready "$path"; then
		echo "gamecube-submodule-sync: skip unavailable submodule checkout: $path" >&2
		return 0
	fi

	branch="$(git -C "$path" rev-parse --abbrev-ref HEAD)"
	if [[ "$branch" == "HEAD" ]]; then
		echo "gamecube-submodule-sync: $path is detached; checkout a branch before syncing" >&2
		return 1
	fi

	if (( PUSH_ONLY == 0 )) && submodule_dirty_tracked "$path"; then
		subject="chore: sync GameCube submodule changes from $path"
		echo "gamecube-submodule-sync: committing tracked changes in $path" >&2
		git -C "$path" add -A
		git -C "$path" diff --cached --check
		git -C "$path" commit -m "$subject"
	fi

	if submodule_ahead_of_origin "$path"; then
		echo "gamecube-submodule-sync: pushing $path ($branch)" >&2
		git -C "$path" push origin "$branch"
	fi

	recorded="$(git ls-tree HEAD "$path" 2>/dev/null | awk '{print $3}' || true)"
	actual="$(git -C "$path" rev-parse HEAD)"
	if [[ "$recorded" != "$actual" ]]; then
		echo "gamecube-submodule-sync: staging parent gitlink for $path ($recorded -> $actual)" >&2
		git add "$path"
	fi
}

for path in "${SYNC_SUBMODULES[@]}"; do
	sync_one_submodule "$path"
done

if git diff --cached --quiet; then
	echo "gamecube-submodule-sync: parent gitlinks already match submodule HEAD"
	exit 0
fi

if (( PARENT_COMMIT == 0 )); then
	echo "gamecube-submodule-sync: staged parent gitlink updates; skipping parent commit"
	git diff --cached --submodule=log
	exit 0
fi

git diff --cached --check
git commit -m "$(cat <<EOF
chore: update GameCube submodule pointers

Refresh owned submodule gitlinks after pushing local master branches:
${SYNC_SUBMODULES[*]}
EOF
)"
git diff --cached --submodule=log >/dev/null
echo "gamecube-submodule-sync: parent commit recorded submodule pointer updates"
