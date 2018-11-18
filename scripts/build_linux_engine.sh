#!/bin/bash

# Build custom SDL2

cd $TRAVIS_BUILD_DIR/SDL2_src
export CC="ccache gcc -msse2 -march=i686 -m32 -ggdb -O2"
./configure \
	--disable-dependency-tracking \
	--disable-render \
	--disable-haptic \
	--disable-power \
	--disable-filesystem \
	--disable-file \
	--enable-alsa-shared \
	--enable-pulseaudio-shared \
	--enable-wayland-shared \
	--enable-x11-shared \
	--disable-libudev \
	--disable-dbus \
	--disable-ibus \
	--disable-ime \
	--disable-fcitx \
	--prefix / # get rid of /usr/local stuff
make -j2
mkdir -p $TRAVIS_BUILD_DIR/SDL2_linux
make install DESTDIR=$TRAVIS_BUILD_DIR/SDL2_linux

# Build engine
cd $TRAVIS_BUILD_DIR
export CC="ccache gcc"
export CXX="ccache g++"
./waf configure --sdl2=$TRAVIS_BUILD_DIR/SDL2_linux --vgui=$TRAVIS_BUILD_DIR/vgui-dev --build-type=debug --use-stb
./waf build -j2
# cp engine/xash3d mainui/libxashmenu.so vgui_support/libvgui_support.so vgui_support/vgui.so ../scripts/xash3d.sh .
# cp $TRAVIS_BUILD_DIR/sdl2-linux/usr/local/lib/$(readlink $TRAVIS_BUILD_DIR/sdl2-linux/usr/local/lib/libSDL2-2.0.so.0) libSDL2-2.0.so.0
# 7z a -t7z $TRAVIS_BUILD_DIR/xash3d-linux.7z -m0=lzma2 -mx=9 -mfb=64 -md=32m -ms=on xash3d libSDL2-2.0.so.0 libvgui_support.so vgui.so libxashmenu.so xash3d.sh
