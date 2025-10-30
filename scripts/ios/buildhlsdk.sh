#!/bin/

#cd into script directory
SCRIPTDIR=${0%/*}
cd $SCRIPTDIR

MODPATH=mod-build/$1
if [ -z $1 ]; then
    MODPATH=mod-build/hlsdk
    git clone --recursive https://github.com/FWGS/hlsdk-portable $MODPATH
else
    git clone --recursive https://github.com/FWGS/hlsdk-portable -b $1 $MODPATH
fi

cd $MODPATH

#compiling hlsdk for ios release is broken, so just build for debug
cmake -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_BUILD_TYPE=Debug -B build -S .
cmake --build build

if [ -z $2 ]; then
    mv build/cl_dll/client_arm64.dylib ../../../../ios/cl_dlls
    mv build/dlls/hl_arm64.dylib ../../../../ios/dlls
else
    mv build/cl_dll/client_arm64.dylib client_arm64$2.dylib
    cp client_arm64$2.dylib ../../../../ios/cl_dlls
    cp build/dlls/*.dylib ../../../../ios/dlls
fi

cd ../../
if [ -d mod-build ]; then
    rm -rf mod-build/
fi
exit 0