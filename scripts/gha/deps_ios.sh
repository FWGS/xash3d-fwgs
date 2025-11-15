#!/bin/bash

cd $GITHUB_WORKSPACE

git clone https://github.com/libsdl-org/SDL -b release-$SDL_VERSION

cd SDL/Xcode/SDL
xcodebuild -scheme xcFramework-iOS -target xcFramework-iOS build -configuration Release
sudo cp -vr Products/SDL2.xcframework/ios-arm64/SDL2.framework /Library/Frameworks

cd $GITHUB_WORKSPACE

git clone https://github.com/FWGS/hlsdk-portable hlsdk -b mobile_hacks --depth=1
