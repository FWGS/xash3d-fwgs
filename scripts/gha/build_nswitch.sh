#!/bin/bash

. scripts/lib.sh

# args: branch name, gamedir name
build_hlsdk()
{
  echo "Building HLSDK: $1 branch..."
  git checkout switch-$1
  ./waf configure -T release --nswitch || die_configure
  ./waf build || die
  cp build/dlls/$1_nswitch_aarch64.so ../../pkgtemp/xash3d/$2/dlls/
  cp build/cl_dll/client_nswitch_aarch64.so ../../pkgtemp/xash3d/$2/cl_dlls/
  ./waf clean
}

export DEVKITPRO=/opt/devkitpro
source $DEVKITPRO/switchvars.sh || die
cd "$BUILDDIR" || die

rm -rf artifacts build pkgtemp hlsdktemp

mkdir -p pkgtemp/xash3d/{valve,gearbox,bshift}/{dlls,cl_dlls} || die

echo "Building engine..."

./waf configure -T release --nswitch || die_configure
./waf build || die

echo "Building HLSDK..."

mkdir -p hlsdktemp || die
cd hlsdktemp || die

# TODO: replace with FWGS/hlsdk-portable.git when PRs are merged
git clone --recursive https://github.com/fgsfdsfgs/hlsdk-xash3d.git
cd hlsdk-xash3d || die
build_hlsdk hl valve
build_hlsdk opfor gearbox
build_hlsdk bshift bshift

echo "Packaging artifacts..."

cp build/engine/xash.nro pkgtemp/xash3d/xash3d.nro
cp build/ref/gl/libref_gl.so pkgtemp/xash3d/
cp build/ref/soft/libref_soft.so pkgtemp/xash3d/
cp build/3rdparty/mainui/libmenu.so pkgtemp/xash3d/
cp build/3rdparty/extras/extras.pk3 pkgtemp/xash3d/valve/

mkdir -p artifacts/ || die
