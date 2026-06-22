#!/bin/sh
# Build Xash3D FWGS for Nintendo GameCube (requires devkitPro)
set -e

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"

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
echo "Copy Half-Life assets to sd:/xash3d/valve/ and run OUT/bin/boot.dol in Dolphin"
