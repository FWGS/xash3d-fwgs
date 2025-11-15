#!/bin/bash

. scripts/lib.sh

curl -L http://libsdl.org/release/SDL2-devel-$SDL_VERSION-VC.zip -o SDL2.zip
unzip -q SDL2.zip
mv SDL2-$SDL_VERSION SDL2_VC

if [ "$GH_CPU_ARCH" = "i386" ]; then
	rustup target add i686-pc-windows-msvc
fi

curl -L https://github.com/FWGS/potential-meme/releases/download/prebuilts/mingw-w64-x86_64-pkgconf-1.2.3.0-1-any.pkg.tar.zst -o pkgconf.tar.zst
7z x pkgconf.tar.zst
7z x pkgconf.tar
rm pkgconf.tar*
mv mingw64 pkgconf

FFMPEG_ARCHIVE=$(get_ffmpeg_archive)
curl -L https://github.com/FWGS/FFmpeg-Builds/releases/download/latest/$FFMPEG_ARCHIVE.zip -o ffmpeg.zip
if [ -f ffmpeg.zip ]; then
	unzip -x ffmpeg.zip
	mv $FFMPEG_ARCHIVE ffmpeg
fi
