#!/bin/sh

ARCHS="armeabi-v7a-hard armeabi x86"
TOOLCHAIN=4.9
API=19
ROOT="$PWD" # compile.sh must be run from root of android project sources
SUBDIRS="xash3d-fwgs hlsdk-xash3d"
SYMLINKS_APPEND=""
if [ $# -ne 2 ]; then
	BUILD_TYPE=debug
else
	BUILD_TYPE=$1
fi

# Cleanup libraries
rm -rf android/lib/

# Generate extras.pak
python xash3d-fwgs/scripts/makepak.py xash-extras android/assets/extras.pak

# Generate configs
android/gen-config.sh android/
android/gen-version.sh android/

# configure android project
./waf configure -T $BUILD_TYPE || exit 1

build_native_project()
{
	mkdir -p $ROOT/build-$1/$2
	if [ -L "$1-sl" ]; then
		cd $1-sl # need to change directory, as waf doesn't work well with symlinks(used in development purposes)
	else
		cd $1
	fi
	./waf -o $ROOT/build-$1/$2 configure -T $BUILD_TYPE --android="$2,$3,$4" build || exit 1
	./waf install --destdir=$ROOT/build/android/
	cd $ROOT # obviously, we can't ../ from symlink directory, so change to our root directory
}

# Do it inside waf?
for i in $ARCHS; do
	for j in $SUBDIRS; do
		build_native_project "$j" "$i" "$TOOLCHAIN" "$API"
	done
done

find $ROOT/build/android/lib -name "*.a" -delete

# Run waf
./waf build || exit 1

# sign
cp build/android/xashdroid-src.apk xashdroid.apk
apksigner sign --ks ../myks.keystore xashdroid.apk
