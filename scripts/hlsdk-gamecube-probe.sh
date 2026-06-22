#!/usr/bin/env bash
# Inspect an external hlsdk-portable checkout for GameCube port readiness.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
HLSDK_DIR="${HLSDK_PORTABLE_DIR:-$ROOT/3rdparty/hlsdk-portable}"

echo "HLSDK source: $HLSDK_DIR"

if [[ ! -d "$HLSDK_DIR" ]]; then
	echo "probe: hlsdk-portable source is missing" >&2
	echo "Set HLSDK_PORTABLE_DIR or clone FWGS/hlsdk-portable to 3rdparty/hlsdk-portable." >&2
	exit 2
fi

if [[ ! -f "$HLSDK_DIR/wscript" ]]; then
	echo "probe: $HLSDK_DIR does not look like an hlsdk-portable Waf checkout" >&2
	exit 2
fi

if git -C "$HLSDK_DIR" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
	echo "branch: $(git -C "$HLSDK_DIR" branch --show-current 2>/dev/null || true)"
	echo "commit: $(git -C "$HLSDK_DIR" rev-parse --short HEAD)"
	if git -C "$HLSDK_DIR" rev-parse --verify --quiet mobile_hacks >/dev/null; then
		echo "mobile_hacks: local branch present"
	else
		echo "mobile_hacks: local branch not present"
	fi
else
	echo "branch: non-git checkout"
fi

if grep -Rqs -- '--gamecube\|GAMECUBE\|__GAMECUBE__' \
	"$HLSDK_DIR/wscript" "$HLSDK_DIR/scripts" "$HLSDK_DIR/cmake" 2>/dev/null; then
	echo "gamecube hooks: present"
	echo "probe: OK"
	exit 0
fi

echo "gamecube hooks: missing" >&2
echo "probe: hlsdk-portable needs GameCube platform naming/build support before map loading." >&2
exit 3
