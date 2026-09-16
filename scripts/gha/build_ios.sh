#!/bin/bash

. scripts/lib.sh

cd "$GITHUB_WORKSPACE" || die

cp -vr /Library/Frameworks/SDL2.framework ./

pushd hlsdk || die
mkdir -p ../build/ios/libs || die
cmake -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 -DCMAKE_INSTALL_PREFIX=$(realpath ../build/ios/libs) -DCMAKE_BUILD_TYPE=Debug -B build -S .
cmake --build build --target install || die
popd || die

cd ios || die
xcodebuild -scheme Launcher -target Launcher build -configuration Release \
    CODE_SIGNING_ALLOWED=NO \
    CODE_SIGNING_REQUIRED=NO \
    CODE_SIGN_IDENTITY="" \
    CODE_SIGN_ENTITLEMENTS="" || die

cd .. || die

mkdir -p artifacts/
mv "build/xash3d.ipa" "artifacts/xash3d-fwgs-ios-arm64.ipa"
