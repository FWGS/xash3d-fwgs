#!/bin/bash

. scripts/lib.sh

# Build engine

cd $TRAVIS_BUILD_DIR
export CC="/usr/bin/clang"
export CXX="/usr/bin/clang++"
export CFLAGS="-m32"
export CXXFLAGS="-m32"
python waf configure --sdl2=$HOME/Library/Frameworks/SDL2.framework/ --vgui=$TRAVIS_BUILD_DIR/vgui-dev --build-type=debug || die
python waf build -j2 || die
# mkdir -p pkg/
# cp engine/libxash.dylib game_launch/xash3d mainui/libxashmenu.dylib vgui_support/libvgui_support.dylib VGUI/vgui-dev-master/lib/vgui.dylib ../scripts/xash3d.sh # pkg/
# cp ~/Library/Frameworks/SDL2.framework/SDL2 pkg/libSDL2.dylib
# tar -cjf $TRAVIS_BUILD_DIR/xash3d-osx.tar.bz2 pkg/*
