#!/bin/bash

. scripts/lib.sh

# args: branch name, gamedir name
build_hlsdk()
{
  echo "Building HLSDK: $1 branch..."
  git checkout switch-$1
  ./waf configure -T release --nswitch || die_configure
  ./waf build || die
  cp build/dlls/$1_nswitch_aarch64.so ../pkgtemp/xash3d/$2/dlls/
  cp build/cl_dll/client_nswitch_aarch64.so ../pkgtemp/xash3d/$2/cl_dlls/
  ./waf clean
}

echo "Downloading missing deps..."

dkp-pacman -S --noconfirm dkp-toolchain-vars || die
# forgive me father, for I have sinned
ln -s /usr/bin/python3 /usr/bin/python

source $DEVKITPRO/switchvars.sh

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
cp build/ref/gl/libref_gl.so pkgtemp/xash3d/
cp build/ref/soft/libref_soft.so pkgtemp/xash3d/
cp build/3rdparty/mainui/libmenu.so pkgtemp/xash3d/
cp build/3rdparty/extras/extras.pk3 pkgtemp/xash3d/valve/