#!/bin/bash

. scripts/lib.sh

cd "$BUILDDIR" || die

echo "Building HLSDK..."

pushd hlsdk-portable || die
# This is not our bug if HLSDK doesn't build with -Werrors enabled
CC=clang-18 CXX=clang++-18 ./waf configure -T release --ps4 --disable-werror || die_configure
./waf build install --destdir=build/ps4 || die
popd || die

echo "Building engine..."

./waf configure -T release --ps4 --ps4-gamelibs=hlsdk-portable/build/ps4/valve || die_configure
./waf build || die

mkdir -p artifacts || die
cp build/engine/IV0000-XASH00001_00-XASH3DFWGSPS4000.pkg artifacts/xash3d-fwgs-ps4.pkg || die
