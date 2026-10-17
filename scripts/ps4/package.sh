#!/bin/sh
# Packages Xash3D FWGS PS4 build into installable .pkg
# Usage: scripts/ps4/package.sh [build directory] [output directory]
set -e

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${1:-$ROOT/build}
OUT=${2:-$BUILD}

TITLE="Xash3D FWGS"
VERSION="1.00"
TITLE_ID="XASH00001"
CONTENT_ID="IV0000-${TITLE_ID}_00-XASH3DFWGSPS4000"

if [ -z "$OO_PS4_TOOLCHAIN" ]; then
	echo "Set OO_PS4_TOOLCHAIN to OpenOrbis toolchain directory" >&2
	exit 1
fi

TOOLS="$OO_PS4_TOOLCHAIN/bin/linux"
SAMPLE="$OO_PS4_TOOLCHAIN/samples/hello_world"
STAGE="$BUILD/ps4pkg"

# PkgTool.Core is built on old .NET that needs these to run on modern distros
export DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1

rm -rf "$STAGE"
mkdir -p "$STAGE/sce_sys/about" "$STAGE/sce_module" "$STAGE/valve"

fself_lib()
{
	in="$1"
	out="$2"
	"$TOOLS/create-fself" -in="$in" -out="$STAGE/$out.oelf" --lib="$STAGE/$out" --paid 0x3800000000000011 > /dev/null
	rm -f "$STAGE/$out.oelf"
	echo "  $out"
}

echo "Creating signed images:"
"$TOOLS/create-fself" -in="$BUILD/engine/xash" -out="$STAGE/eboot.oelf" --eboot "$STAGE/eboot.bin" --paid 0x3800000000000011 > /dev/null
rm -f "$STAGE/eboot.oelf"
echo "  eboot.bin"

fself_lib "$BUILD/filesystem/filesystem_stdio.so" filesystem_stdio.prx
fself_lib "$BUILD/3rdparty/mainui/libmenu.so" libmenu.prx

found_ref=0
for name in soft gl gles1 gles2 gl4es gles3compat null; do
	key=$(echo "$name" | tr '[:lower:]' '[:upper:]')
	# only renderers enabled in current configuration
	grep -q "^$key = True" "$BUILD/c4che/_cache.py" || continue
	ref=$(ls "$BUILD"/ref/*/libref_$name.so 2>/dev/null | head -n 1)
	[ -n "$ref" ] || continue
	fself_lib "$ref" "libref_$name.prx"
	found_ref=1
done

if [ $found_ref = 0 ]; then
	echo "ERROR: no renderers found in $BUILD/ref" >&2
	exit 1
fi

cp "$BUILD/3rdparty/extras/extras.pk3" "$STAGE/valve/extras.pk3"

# Half-Life game libraries from hlsdk-portable (its scripts/ps4/build.sh), so the package
# runs with just the game data copied to the console. Libraries uploaded to
# /data/xash/valve/{cl_dlls,dlls} are used instead of these when present.
GAMELIBS=${HLSDK_PS4_BUILD:-$ROOT/../hlsdk-portable/build/ps4/valve}
if ls "$GAMELIBS"/cl_dlls/*.prx "$GAMELIBS"/dlls/*.prx > /dev/null 2>&1; then
	for f in "$GAMELIBS"/cl_dlls/*.prx "$GAMELIBS"/dlls/*.prx; do
		cp "$f" "$STAGE/"
		echo "  $(basename "$f") (game library)"
	done
else
	echo "  WARNING: no game libraries in $GAMELIBS, build hlsdk-portable first or set HLSDK_PS4_BUILD;"
	echo "  the package will need them in /data/xash/valve/cl_dlls and dlls"
fi
# icon0.png - 512x512 icon, pic0.png/pic1.png - 1920x1080 images, the system shows
# one of them as startup splash until the engine hides it after initialization
cp "$ROOT/engine/platform/ps4/sce_sys/icon0.png" "$ROOT/engine/platform/ps4/sce_sys/pic0.png" "$STAGE/sce_sys/"

# custom images (e.g. from your copy of the game) override defaults, this directory is ignored by git
CUSTOM="$ROOT/ps4_sce_sys"
for f in icon0.png pic0.png pic1.png; do
	if [ -f "$CUSTOM/$f" ]; then
		cp "$CUSTOM/$f" "$STAGE/sce_sys/$f"
		echo "  using custom $f"
	fi
done
# same image is used for both if only one is given
[ -f "$STAGE/sce_sys/pic1.png" ] || cp "$STAGE/sce_sys/pic0.png" "$STAGE/sce_sys/pic1.png"
cp "$SAMPLE/sce_sys/about/right.sprx" "$STAGE/sce_sys/about/right.sprx"
cp "$SAMPLE/sce_module/libc.prx" "$SAMPLE/sce_module/libSceFios2.prx" "$STAGE/sce_module/"

echo "Creating param.sfo"
SFO="$STAGE/sce_sys/param.sfo"
"$TOOLS/PkgTool.Core" sfo_new "$SFO" > /dev/null
set_sfo() { "$TOOLS/PkgTool.Core" sfo_setentry "$SFO" "$@" > /dev/null; }
set_sfo APP_TYPE --type Integer --maxsize 4 --value 1
set_sfo APP_VER --type Utf8 --maxsize 8 --value "$VERSION"
# 0x2: common dialogs (system keyboard) confirm with Cross, not Circle
set_sfo ATTRIBUTE --type Integer --maxsize 4 --value 2
set_sfo CATEGORY --type Utf8 --maxsize 4 --value gd
set_sfo CONTENT_ID --type Utf8 --maxsize 48 --value "$CONTENT_ID"
set_sfo DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
set_sfo SYSTEM_VER --type Integer --maxsize 4 --value 0
set_sfo TITLE --type Utf8 --maxsize 128 --value "$TITLE"
set_sfo TITLE_ID --type Utf8 --maxsize 12 --value "$TITLE_ID"
set_sfo VERSION --type Utf8 --maxsize 8 --value "$VERSION"

echo "Creating package"
cd "$STAGE"
python3 "$ROOT/scripts/ps4/make-gp4.py" "$CONTENT_ID"
"$TOOLS/PkgTool.Core" pkg_build pkg.gp4 "$OUT" > /dev/null

echo "Done: $OUT/$CONTENT_ID.pkg"
