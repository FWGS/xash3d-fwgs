#!/bin/bash

#cd into script directory
SCRIPTDIR=${0%/*}
cd $SCRIPTDIR

MODPATH=mod-build/$1
if [ -z $1 ]; then
    MODPATH=mod-build/hlsdk
    git clone --recursive https://github.com/FWGS/hlsdk-portable -b mobile_hacks $MODPATH
else
    git clone --recursive https://github.com/FWGS/hlsdk-portable -b $1 $MODPATH
fi

mkdir ../../build/ios
IOSDIR=$(realpath ../../build/ios)
cd $MODPATH

#compiling hlsdk for ios release is broken, so just build for debug
cmake -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 -DGAMEDIR=$IOSDIR -DCMAKE_BUILD_TYPE=Debug -B build -S .
cmake --build build --target install

cd ../../
if [ -d mod-build ]; then
    rm -rf mod-build/
fi
exit 0