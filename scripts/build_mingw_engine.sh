#!/bin/bash

# Build engine
cd $TRAVIS_BUILD_DIR
export CC="ccache i686-w64-mingw32-gcc"
export CXX="ccache i686-w64-mingw32-g++"
export CFLAGS="-static-libgcc -no-pthread"
export CXXFLAGS="-static-libgcc -static-libstdc++"
export WINRC="i686-w64-mingw32-windres"
./waf configure -o build-mingw --sdl2=$TRAVIS_BUILD_DIR/SDL2_mingw/i686-w64-mingw32/ --no-vgui --build-type=debug --verbose # can't compile VGUI support on MinGW, due to differnet C++ ABI
./waf build -o build-mingw -j2 --verbose
cp $TRAVIS_BUILD_DIR/SDL2_mingw/i686-w64-mingw32//bin/SDL2.dll . # Install SDL2
cp vgui_support_bin/vgui_support.dll .
cp build-mingw/engine/xash.dll .
cp build-mingw/mainui/menu.dll .
cp build-mingw/game_launch/xash3d.exe .
7z a -t7z $TRAVIS_BUILD_DIR/xash3d-mingw.7z -m0=lzma2 -mx=9 -mfb=64 -md=32m -ms=on *.dll *.exe
