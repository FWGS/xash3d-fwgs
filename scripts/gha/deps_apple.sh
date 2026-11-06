#!/bin/bash

cd "$GITHUB_WORKSPACE" || exit 1

# not checked out by default, needed for --enable-tui
git submodule update --init --checkout 3rdparty/maintui || exit 1

wget "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL2-$SDL_VERSION.dmg" -O SDL2.dmg
hdiutil mount SDL2.dmg
sudo cp -vr /Volumes/SDL2/SDL2.framework /Library/Frameworks

git clone --recursive https://github.com/FWGS/hlsdk-portable hlsdk --depth=1
