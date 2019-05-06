#!/bin/sh

ARCHS="armeabi-v7a-hard armeabi x86"
TOOLCHAIN=4.9
API=19
ROOT="$PWD" # compile.sh must be run from root of android project sources
SUBDIRS="xash3d-fwgs hlsdk-xash3d"
SYMLINKS_APPEND=""

# Cleanup libraries
rm -rf android/lib/

# Generate configs
android/gen-config.sh android/
android/gen-version.sh android/

build_native_project()
{
	mkdir -p $ROOT/build-$1/$2
	if [ -L "$1-sl" ]; then
		cd $1-sl # need to change directory, as waf doesn't work well with symlinks(used in development purposes)
	else
		cd $1
	fi
	./waf -o $ROOT/build-$1/$2 configure -T release --android="$2,$3,$4" build || exit 1
	./waf install --destdir=$ROOT/android/
	cd $ROOT # obviously, we can't ../ from symlink directory, so change to our root directory
}

# Do it inside waf?
for i in $ARCHS; do
	for j in $SUBDIRS; do
		build_native_project "$j" "$i" "$TOOLCHAIN" "$API"
	done
done

# Run waf
./waf configure -T release build
