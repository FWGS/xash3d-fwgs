#!/bin/bash

. scripts/lib.sh

# Build engine
cd $TRAVIS_BUILD_DIR
export CC="ccache i686-w64-mingw32-gcc"
export CXX="ccache i686-w64-mingw32-g++"
export CFLAGS="-static-libgcc -no-pthread -msse2 -O1 -g -gdwarf-2" # add sse2 to workaround mingw multiple definition of MemoryBarrier bug
export CXXFLAGS="-static-libgcc -static-libstdc++ -no-pthread -msse2"
export LDFLAGS="-static-libgcc -static-libstdc++ -no-pthread -Wl,--allow-multiple-definition" # workaround some other mingw bugs
export WINRC="i686-w64-mingw32-windres"
rm -rf build # clean build directory
./waf configure -s "SDL2_mingw/i686-w64-mingw32/" --build-type=none --prefix="." --win-style-install -v || die # can't compile VGUI support on MinGW, due to differnet C++ ABI
./waf build -v || die
cp $TRAVIS_BUILD_DIR/SDL2_mingw/i686-w64-mingw32//bin/SDL2.dll . # Install SDL2
cp vgui_support_bin/vgui_support.dll .
./waf install || die

7z a -t7z $TRAVIS_BUILD_DIR/xash3d-mingw.7z -m0=lzma2 -mx=9 -mfb=64 -md=32m -ms=on *.dll *.exe
