#!/bin/bash

. scripts/lib.sh

build_hlsdk()
{
	echo "Building HLSDK: $1 branch..."
	git checkout $1
	./waf configure -T release --nswitch || die_configure
	./waf build install --destdir=../pkgtemp/xash3d || die
	./waf clean
}

echo "Setting up environment..."

# we can't actually download dkp-toolchain-vars even from here, so
export PORTLIBS_ROOT=${DEVKITPRO}/portlibs
export PATH=${DEVKITPRO}/tools/bin:${DEVKITPRO}/devkitA64/bin:$PATH
export TOOL_PREFIX=aarch64-none-elf-
export CC=${TOOL_PREFIX}gcc
export CXX=${TOOL_PREFIX}g++
export AR=${TOOL_PREFIX}gcc-ar
export RANLIB=${TOOL_PREFIX}gcc-ranlib
export PORTLIBS_PREFIX=${DEVKITPRO}/portlibs/switch
export PATH=$PORTLIBS_PREFIX/bin:$PATH
export ARCH="-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIC -ftls-model=local-exec"
export CFLAGS="${ARCH} -O2 -ffunction-sections -fdata-sections"
export CXXFLAGS="${CFLAGS}"
export CPPFLAGS="-D__SWITCH__ -I${PORTLIBS_PREFIX}/include -isystem ${DEVKITPRO}/libnx/include"
export LDFLAGS="${ARCH} -L${PORTLIBS_PREFIX}/lib -L${DEVKITPRO}/libnx/lib"
export LIBS="-lnx"

# forgive me father, for I have sinned
ln -s /usr/bin/python3 /usr/bin/python

echo "Building libsolder..."

make -C libsolder install || die

echo "Building engine..."

./waf configure -T release --nswitch || die_configure
./waf build install --destdir=pkgtemp/xash3d || die

echo "Building HLSDK..."

# TODO: replace with hlsdk-portable when PRs are merged
pushd hlsdk-portable
build_hlsdk mobile_hacks valve
build_hlsdk opfor gearbox
build_hlsdk bshift bshift
popd
