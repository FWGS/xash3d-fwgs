#!/bin/bash

. scripts/lib.sh
. /opt/toolchains/motomagx/setenv-z6.sh

cd $GITHUB_WORKSPACE || die

mkdir -p Xash/valve/cl_dlls
mkdir -p Xash/valve/dlls

pushd hlsdk || die
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
cd $mypath
sleep 1

exec $mypath/xash -dev $@
EOF

mkdir -p artifacts/
7z a -t7z artifacts/xash3d-fwgs-magx.7z -m0=lzma2 -mx=9 -mfb=64 -md=32m -ms=on -r Xash/
