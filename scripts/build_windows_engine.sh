#!/bin/bash

. scripts/lib.sh

# Build engine
cd $TRAVIS_BUILD_DIR
./waf.bat configure --sdl2=$TRAVIS_BUILD_DIR/SDL2_VC --vgui=$TRAVIS_BUILD_DIR/vgui-dev --build-type=debug
./waf.bat build
echo After build

cp $TRAVIS_BUILD_DIR/SDL2_VC/lib/x86/SDL2.dll . # Install SDL2
cp vgui-dev/lib/win32_vc6/vgui.dll .
cp build/*/*.dll build/*/*.exe build/*/*.pdb build/*.pdb .

7z a -t7z $TRAVIS_BUILD_DIR/xash3d-vc.7z -m0=lzma2 -mx=9 -mfb=64 -md=32m -ms=on *.dll *.exe
