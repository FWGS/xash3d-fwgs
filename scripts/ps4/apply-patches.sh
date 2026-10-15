#!/bin/sh
# Applies PS4 platform support to submodules that are maintained in separate repositories.
# Safe to run multiple times.
set -e

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PATCHES="$ROOT/scripts/ps4/patches"

apply_patch()
{
	dir="$1"
	patch="$2"

	if git -C "$dir" apply --reverse --check "$patch" 2>/dev/null; then
		echo "already applied: $(basename "$patch")"
		return
	fi

	# older version of the patch could be applied, restore files it touches
	git -C "$dir" apply --numstat "$patch" | cut -f3 | while read -r file; do
		git -C "$dir" checkout -- "$file" 2>/dev/null || true
	done

	if git -C "$dir" apply --check "$patch" 2>/dev/null; then
		git -C "$dir" apply "$patch"
		echo "applied: $(basename "$patch")"
	else
		echo "ERROR: can't apply $(basename "$patch") to $dir" >&2
		echo "Run: git submodule update --init --recursive" >&2
		exit 1
	fi
}

apply_patch "$ROOT/3rdparty/mainui" "$PATCHES/mainui-ps4.patch"
