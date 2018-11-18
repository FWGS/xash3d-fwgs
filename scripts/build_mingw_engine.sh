#!/bin/bash

# Build engine

cd $TRAVIS_BUILD_DIR
export CC="ccache i686-w64-mingw32-gcc"
export CXX="ccache i686-w64-mingw32-g++"
export CFLAGS="-static-libgcc -no-pthread"
export CXXFLAGS="-static-libgcc -static-libstdc++"
./waf configure -o build-mingw --sdl2=$TRAVIS_BUILD_DIR/SDL2_mingw/i686-w64-mingw32/ --no-vgui --build-type=debug # can't compile VGUI support on MinGW, due to differnet C++ ABI
./waf build -o build-mingw -j2
# cp SDL2/SDL2-2.0.7/i686-w64-mingw32/bin/SDL2.dll . # Install SDL2
# cp /usr/i686-w64-mingw32/lib/libwinpthread-1.dll . # a1ba: remove when travis will be updated to xenial
# 7z a -t7z $TRAVIS_BUILD_DIR/xash3d-mingw.7z -m0=lzma2 -mx=9 -mfb=64 -md=32m -ms=on xash_sdl.exe menu.dll SDL2.dll vgui_support.dll libwinpthread-1.dll
