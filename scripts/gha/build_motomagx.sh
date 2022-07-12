#!/bin/bash

. scripts/lib.sh
. /opt/toolchains/motomagx/setenv-z6.sh

cd $GITHUB_WORKSPACE

mkdir -p Xash/valve/cl_dlls
mkdir -p Xash/valve/dlls

pushd hlsdk
./waf configure -T fast --enable-magx --enable-simple-mod-hacks build install --destdir=../Xash || die
popd

./waf configure -T fast --enable-magx build install --destdir=Xash/ || die

cat > Xash/run.sh << 'EOF'
mypath=${0%/*}
LIBDIR1=/ezxlocal/download/mystuff/games/lib
LIBDIR2=/mmc/mmca1/games/lib
LIBDIR3=$mypath
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$LIBDIR1:$LIBDIR2:$LIBDIR3
export HOME=$mypath
export SDL_QT_INVERT_ROTATION=1
export SWAP_PATH=$HOME/xash.swap
export XASH3D_EXTRAS_PAK1=$HOME/extras.pak
cd $mypath
sleep 1

exec $mypath/xash -dev $@
EOF

python3 scripts/makepak.py xash-extras/ Xash/extras.pak

mkdir -p artifacts/
7z a -t7z artifacts/xash3d-fwgs-magx.7z -m0=lzma2 -mx=9 -mfb=64 -md=32m -ms=on -r Xash/
