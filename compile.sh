#!/bin/sh

ARCHS="armeabi-v7a-hard armeabi x86"
TOOLCHAIN=4.9
API=21
ROOT="$PWD" # compile.sh must be run from root of android project sources

# Generate configs
android/gen-config.sh android/
android/gen-version.sh android/

build_native_project()
{
	./waf -t $1 -o build-$1-$2 configure -T release --android="$2,$3,$4" build
}

# Do it inside waf?
for i in $ARCHS; do
	build_native_project "xash3d-fwgs" "$i" "$TOOLCHAIN" "$API" || exit 1
done

# Run waf
./waf configure -T release build
