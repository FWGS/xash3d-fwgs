#!/bin/bash

. scripts/lib.sh
. /opt/toolchains/motomagx/setenv-z6.sh

cd $GITHUB_WORKSPACE || die

./waf configure -T fast --enable-magx build -v install --destdir=Xash/ --strip || die

pushd hlsdk || die
./waf configure -T fast --enable-magx build -v install --destdir=../Xash --strip || die
git checkout opfor
./waf configure -T fast --enable-magx build -v install --destdir=../Xash --strip || die
popd

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
