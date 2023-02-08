#!/bin/bash

. scripts/lib.sh

# args: branch name, gamedir name
build_hlsdk()
{
  echo "Building HLSDK: $1 branch..."
  git checkout switch-$1
  ./waf configure -T release --nswitch || die_configure
  ./waf build || die
  cp build/dlls/$1_nswitch_arm64.so ../pkgtemp/xash3d/$2/dlls/
  cp build/cl_dll/client_nswitch_arm64.so ../pkgtemp/xash3d/$2/cl_dlls/
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
./waf build || die

echo "Building HLSDK..."

# TODO: replace with hlsdk-portable when PRs are merged
pushd hlsdk-xash3d
build_hlsdk hl valve
build_hlsdk opfor gearbox
build_hlsdk bshift bshift
popd

echo "Copying artifacts..."

cp build/engine/xash.nro pkgtemp/xash3d/xash3d.nro
cp build/filesystem/filesystem_stdio.so pkgtemp/xash3d/
cp build/ref/gl/libref_gl.so pkgtemp/xash3d/
cp build/ref/soft/libref_soft.so pkgtemp/xash3d/
cp build/3rdparty/mainui/libmenu.so pkgtemp/xash3d/
cp build/3rdparty/extras/extras.pk3 pkgtemp/xash3d/valve/
