#!/bin/bash

cd $GITHUB_WORKSPACE

wget https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL2-$SDL_VERSION.dmg -O SDL2.dmg
hdiutil mount SDL2.dmg
sudo cp -vr /Volumes/SDL2/SDL2.framework /Library/Frameworks

git clone https://github.com/FWGS/hlsdk-portable hlsdk --depth=1
