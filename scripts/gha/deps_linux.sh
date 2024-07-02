#!/bin/bash

cd $GITHUB_WORKSPACE

# TODO: add libpipewire-dev after we migrate from 20.04

if [ "$GH_CPU_ARCH" == "i386" ]; then
	sudo dpkg --add-architecture i386
	sudo apt update
	sudo apt install gcc-multilib g++-multilib libx11-dev:i386 libxext-dev:i386 x11-utils libgl1-mesa-dev libasound-dev libstdc++6:i386 libfuse2:i386 zlib1g:i386 libpulse0:i386 libpulse-dev libjack-dev:i386 libwayland-dev:i386

	wget "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-i686.AppImage" -O appimagetool.AppImage
elif [ "$GH_CPU_ARCH" == "amd64" ]; then
	sudo apt update
	sudo apt install libx11-dev libxext-dev x11-utils libgl1-mesa-dev libasound-dev libstdc++6 libfuse2 zlib1g libpulse-dev libjack-dev libwayland-dev

	wget "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage" -O appimagetool.AppImage
elif [ "$GH_CPU_ARCH" == "aarch64" ]; then
	sudo apt update
	sudo apt install libx11-dev libxext-dev x11-utils libgl1-mesa-dev libasound-dev libstdc++6 libfuse2 zlib1g libpulse-dev libjack-dev libwayland-dev

	wget "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-aarch64.AppImage" -O appimagetool.AppImage
else
	exit 1
fi

chmod +x appimagetool.AppImage

wget http://libsdl.org/release/SDL2-$SDL_VERSION.zip -O SDL2.zip
unzip -q SDL2.zip
mv SDL2-$SDL_VERSION SDL2_src
