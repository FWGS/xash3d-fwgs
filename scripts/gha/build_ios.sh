#!/bin/bash

. scripts/lib.sh

cd $GITHUB_WORKSPACE || die

./waf configure --enable-lto --ios build install --destdir=build/ios || die_configure

cp -vr /Library/Frameworks/SDL2.framework ./build

pushd hlsdk || die
cmake -DCMAKE_SYSTEM_NAME=iOS -DGAMEDIR=$(realpath ../build/ios) -DCMAKE_BUILD_TYPE=Debug -B build -S .
cmake --build build --target install || die
popd

./scripts/ios/createipa.sh

mkdir -p artifacts/
mv "build/xash3d.ipa" "build/xash3d-fwgs-ios-arm64.ipa"
mv "build/xash3d-fwgs-ios-arm64.ipa" artifacts
