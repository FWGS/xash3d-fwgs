#!/bin/sh

if echo "$HOME" | grep "com.termux"; then
	echo "-- Configuring for termux"
	export JAVAC=ecj
	export JAVA=true # /bin/true does nothing but returns success
	export JAR=true
	export JAVADOC=true
	TERMUX_ARG="--termux"
	TOOLCHAIN=host
else
	echo "-- Configuring for Android SDK/NDK"
	if [ "$TOOLCHAIN" = "" ]; then
		TOOLCHAIN=4.9
	fi
fi

if [ "$ARCHS" = "" ]; then
	ARCHS="armeabi-v7a armeabi x86"
fi
API=9
ROOT="$PWD" # compile.sh must be run from root of android project sources
SUBDIRS="xash3d-fwgs hlsdk-xash3d"
SYMLINKS_APPEND=""
if [ $1 == "" ]; then
	BUILD_TYPE=debug
else
	BUILD_TYPE=$1
fi

# Cleanup libraries
rm -rf android/lib/

# Generate extras.pak(TODO: move this to waf somehow)
if [ -L "xash3d-fwgs-sl" ]; then
	python xash3d-fwgs-sl/scripts/makepak.py xash-extras android/assets/extras.pak
else
	python xash3d-fwgs/scripts/makepak.py    xash-extras android/assets/extras.pak
fi

# Generate configs
android/gen-config.sh android/
android/gen-version.sh android/

# configure android project
./waf configure -T $BUILD_TYPE $TERMUX_ARG|| exit 1

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

# Run waf
./waf build -v|| exit 1

if [ "$BUILD_TYPE" != "debug" ] && [ "$USER" = "a1ba" ]; then
	# :)
	cp build/android/xashdroid.apk xashdroid.apk

	apksigner sign --ks ../myks.keystore xashdroid.apk
else
	cp build/android/xashdroid-signed.apk xashdroid.apk
fi
