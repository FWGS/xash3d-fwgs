#!/bin/bash

. scripts/lib.sh
. /opt/toolchains/motomagx/setenv-z6.sh

cd $TRAVIS_BUILD_DIR

mkdir -p Xash/valve/cl_dlls
mkdir -p Xash/valve/dlls

cd hlsdk
./waf configure -T fast --enable-magx build
cp build/cl_dlls/client.so ../Xash/valve/cl_dlls/client_armv6l.so
cp build/dlls/hl.so ../Xash/valve/cl_dlls/hl_armv6l.so
cd ../

./waf configure -T fast --enable-magx --win-style-install --prefix='' build install --destdir=Xash/ \

7z a -t7z $TRAVIS_BUILD_DIR/xash3d-magx.7z -m0=lzma2 -mx=9 -mfb=64 -md=32m -ms=on -r Xash/
