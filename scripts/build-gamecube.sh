#!/usr/bin/env bash
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

GC_DATA="${XASH3D_GC_DATA:-Half-Life/valve}"
GC_ISO="${XASH3D_GC_ISO:-OUT/xash3d-gc.iso}"
GC_SMOKE_MAP="${XASH3D_GC_SMOKE_MAP:-}"

if [ -d "$GC_DATA" ]; then
	DISC_ARGS=(--output "$GC_ISO" --data "$GC_DATA")
	if [ -n "$GC_SMOKE_MAP" ]; then
		DISC_ARGS+=(--smoke-map "$GC_SMOKE_MAP")
	fi
	echo "Building GameCube disc from $GC_DATA ..."
	if python3 "$ROOT/scripts/build-gamecube-disc.py" "${DISC_ARGS[@]}"; then
		echo "Disc image ready: $GC_ISO"
	else
		echo "Disc build failed. Engine build is still in OUT/bin/." >&2
		exit 1
	fi
else
	echo "Half-Life data not found at $GC_DATA; skipping disc build."
	echo "Set XASH3D_GC_DATA or install retail assets, then run:"
	echo "  python3 scripts/build-gamecube-disc.py --output OUT/xash3d-gc.iso --data Half-Life/valve"
fi

echo "For DOL testing, provide Half-Life assets at sd:/xash3d/valve/ before launching OUT/bin/boot.dol."
