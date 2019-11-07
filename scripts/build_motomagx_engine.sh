#!/bin/bash

. scripts/lib.sh
. /opt/toolchains/motomagx/setenv-z6.sh

cd $TRAVIS_BUILD_DIR

mkdir -p Xash/valve/cl_dlls
mkdir -p Xash/valve/dlls

cd hlsdk
./waf configure -T fast --enable-magx build
cp build/cl_dll/client.so ../Xash/valve/cl_dlls/client_armv6l.so
cp build/dlls/hl.so ../Xash/valve/dlls/hl_armv6l.so
cd ../

./waf configure -T fast --enable-magx --win-style-install --prefix='' build install --destdir=Xash/

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


7z a -t7z $TRAVIS_BUILD_DIR/xash3d-magx.7z -m0=lzma2 -mx=9 -mfb=64 -md=32m -ms=on -r Xash/
