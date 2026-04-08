#!/bin/bash

#cd into script directory
SCRIPTDIR=${0%/*}
cd $SCRIPTDIR

MODPATH=mod-build/cs16-client
git clone --recursive https://github.com/Velaron/cs16-client mod-build/cs16-client

mkdir -p ../../build/ios/libs
LIBSDIR=$(realpath ../../build/ios/libs)
cd $MODPATH

cmake -DCMAKE_OSX_SYSROOT=/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk -DCMAKE_DEVELOPER_ROOT=/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 -DMAC=0 -DDEBUG=1 -DXASH_COMPAT=1 -DMAINUI_USE_STB=1 -DCMAKE_BUILD_TYPE=Debug -B build -S .
cmake --build build --config Debug
cmake --install build --prefix $LIBSDIR

cd ../../
./createipa.sh

if [ -d mod-build ]; then
    rm -rf mod-build/
fi

exit 0