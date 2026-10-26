# Xash3D FWGS Engine (Wii port) <img align="right" width="128" height="128" src="https://github.com/FWGS/xash3d-fwgs/raw/master/game_launch/icon-xash-material.png" alt="Xash3D FWGS icon" />

Xash3D ([pronounced](https://ipa-reader.com/?text=ks%C9%91%CA%82) `[ksɑʂ]`) FWGS is a game engine, aimed to provide compatibility with Half-Life Engine and extend it, as well as to give game developers well known workflow. This is the Wii/Gamecube port of the engine

Xash3D FWGS is a heavily modified fork of an original [Xash3D Engine](https://www.moddb.com/engines/xash3d-engine) by Unkle Mike.

## Installation & Running

0) Rename `xash.dol` to `boot.dol` if it was compiled
1) Move the `boot.dol` file to some directory inside the `apps` folder
2) Copy `valve` directory to a folder named `xash3d` in the root of the sd card
3) (Optional) copy `extras.pk3` inside the `xash3d` folder to fix the main menu
4) Run it via the Homebrew Channel

## Build instructions
The Wii/GC port currently uses cmake to build its binaries. Will integrate it into waf at some point

**NOTE: NEVER USE GitHub's ZIP ARCHIVES. GitHub doesn't include external dependencies we're using!**

### Prerequisites

*  Install CMake
*  Install [devkitPro](https://devkitpro.org/wiki/Getting_Started)
*  Install devkitPPC and the needed libraries
 `sudo (dkp-)pacman -S wii-dev wii-sdl2 wii-opengx ppc-bzip2 ppc-freetype ppc-zlib`
*  Create a development directory
*  Clone the following repositories in the same directory
```
git clone --recursive https://github.com/MintFerret/xash3d-fwgs
git clone --recursive https://github.com/MintFerret/mainui_cpp
git clone --recursive https://github.com/MintFerret/hlsdk-portable
```


### Building
1) Configure build `cmake -S. -Bbuild -DCMAKE_TOOLCHAIN_FILE="/opt/devkitpro/cmake/Wii.cmake"`
2) Compile `make -C build`

This will build:
- the filesystem
-  hlsdk (game libraries)
-  mainui
-  vgui
-  opengl renderer
-  the engine itself

### Note
- This is a work in progress
- Expect crashes and instability

### Credits
- Uncle Mike for the original Xash3D Engine
- FWGS team for Xash3D FWGS fork
- mardy for the SDL2 port and OpenGX
- devkitPro team

