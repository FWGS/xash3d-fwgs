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
	if [ -z "$TOOLCHAIN" ]; then
		TOOLCHAIN=4.9
	fi
fi

if [ -z "$ARCHS" ]; then
	ARCHS="armeabi-v7a armeabi x86"
fi

if [ -z "$API" ]; then
	API=9
fi
ROOT="$PWD" # compile.sh must be run from root of android project sources

if [ -z "$1" ]; then
	BUILD_TYPE=debug
else
	BUILD_TYPE=$1
	if [ "$TOOLCHAIN" = "host" ]; then
		ENGINE_FLAGS="--enable-poly-opt"
		SDK_FLAGS="--enable-poly-opt"
	fi
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

die()
{
	cat $1/config.log
	exit 1
}

build_native_project()
{
	prj=$1
	shift
	arch=$1
	shift

	out="$ROOT/build-$prj/$arch"

	mkdir -p $out
	if [ -L "$prj-sl" ]; then
		cd $prj-sl # need to change directory, as waf doesn't work well with symlinks(used in development purposes)
	else
		cd $prj
	fi
	./waf -o "$out" configure -T $BUILD_TYPE --android="$arch,$TOOLCHAIN,$API" $* build || die "$out"
	./waf install --destdir=$ROOT/build/android/
	cd $ROOT # obviously, we can't ../ from symlink directory, so change to our root directory
}

# Do it inside waf?
for i in $ARCHS; do
	build_native_project "xash3d-fwgs" "$i" $ENGINE_FLAGS
	build_native_project "hlsdk-xash3d" "$i" $SDK_FLAGS
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
