#!/bin/sh
# Build Xash3D FWGS for Nintendo GameCube (requires devkitPro)
set -e

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"

ROOT="$(git rev-parse --show-toplevel)"
HLSDK_DIR="${HLSDK_PORTABLE_DIR:-$ROOT/3rdparty/hlsdk-portable}"
HLSDK_DESTDIR="${HLSDK_GAMECUBE_DESTDIR:-$ROOT/OUT/hlsdk-gamecube}"
HLSDK_GAMEDIR="${HLSDK_GAMECUBE_GAMEDIR:-valve}"
HLSDK_SERVER_ARCHIVE="$HLSDK_DESTDIR/$HLSDK_GAMEDIR/dlls/libhl_gamecube_ppc.a"
HLSDK_EXPORTS="$HLSDK_DESTDIR/$HLSDK_GAMEDIR/dlls/gamecube_server_entity_exports.inc"
NM="${DEVKITPRO:-/opt/devkitpro}/devkitPPC/bin/powerpc-eabi-nm"

if [ -s "$HLSDK_SERVER_ARCHIVE" ] && [ -d "$HLSDK_DIR/dlls" ] && [ -x "$NM" ]; then
	python3 "$ROOT/scripts/generate-hlsdk-gamecube-exports.py" \
		--hlsdk-dir "$HLSDK_DIR" \
		--archive "$HLSDK_SERVER_ARCHIVE" \
		--output "$HLSDK_EXPORTS"
fi

./waf configure --gamecube \
	-T release \
	--disable-gl --disable-soft --enable-gx \
	--low-memory-mode=2 \
	--disable-werror \
	"$@"

./waf build

mkdir -p OUT/bin
cp build/engine/xash OUT/bin/xash

if command -v powerpc-eabi-strip >/dev/null 2>&1; then
	powerpc-eabi-strip OUT/bin/xash
fi

if command -v elf2dol >/dev/null 2>&1; then
	elf2dol OUT/bin/xash OUT/bin/boot.dol
elif command -v powerpc-eabi-objcopy >/dev/null 2>&1; then
	powerpc-eabi-objcopy -O binary OUT/bin/xash OUT/bin/boot.dol 2>/dev/null || true
fi

./waf install --destdir=OUT

echo "GameCube build installed to OUT/"
echo "For Dolphin disc testing, build/run OUT/xash3d-gc.iso with scripts/build-gamecube-disc.py."
echo "For DOL testing, provide Half-Life assets at sd:/xash3d/valve/ before launching OUT/bin/boot.dol."
