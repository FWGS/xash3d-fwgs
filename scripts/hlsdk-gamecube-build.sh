#!/usr/bin/env bash
# Build an external hlsdk-portable checkout for the GameCube package tree.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
HLSDK_DIR="${HLSDK_PORTABLE_DIR:-$ROOT/3rdparty/hlsdk-portable}"
DESTDIR="${HLSDK_GAMECUBE_DESTDIR:-$ROOT/OUT/hlsdk-gamecube}"
BRANCH="${HLSDK_GAMECUBE_BRANCH:-mobile_hacks}"
GAMEDIR="${HLSDK_GAMECUBE_GAMEDIR:-valve}"

set +e
"$ROOT/scripts/hlsdk-gamecube-probe.sh"
status=$?
set -e
if (( status != 0 )); then
	case "$status" in
		2)
			echo "build: clone hlsdk-portable or set HLSDK_PORTABLE_DIR first" >&2
			;;
		3)
			echo "build: hlsdk-portable is present but lacks GameCube build hooks" >&2
			echo "build: run scripts/hlsdk-gamecube-apply-patch.py or upstream GameCube support" >&2
			;;
	esac
	exit "$status"
fi

if git -C "$HLSDK_DIR" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
	git -C "$HLSDK_DIR" checkout "$BRANCH"
fi

echo "Building HLSDK branch $BRANCH for GameCube..."
(
	cd "$HLSDK_DIR"
	./waf configure -T release --gamecube --disable-werror
	./waf build install --destdir="$DESTDIR"
	if [[ -s build/game_shared/libvcs_info.a ]]; then
		mkdir -p "$DESTDIR/lib"
		cp build/game_shared/libvcs_info.a "$DESTDIR/lib/"
	fi
)

SERVER_ARCHIVE="$DESTDIR/$GAMEDIR/dlls/libhl_gamecube_ppc.a"
OBJCOPY="${DEVKITPRO:-/opt/devkitpro}/devkitPPC/bin/powerpc-eabi-objcopy"
if [[ -s "$SERVER_ARCHIVE" && -x "$OBJCOPY" ]]; then
	"$OBJCOPY" \
		--redefine-sym g_engfuncs=gamecube_hlsdk_g_engfuncs \
		--redefine-sym gpGlobals=gamecube_hlsdk_gpGlobals \
		--redefine-sym VectorAngles=gamecube_hlsdk_VectorAngles \
		"$SERVER_ARCHIVE"
fi

echo "HLSDK GameCube install complete: $DESTDIR/$GAMEDIR"
