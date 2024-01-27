# Xash3D FWGS Engine <img align="right" width="128" height="128" src="https://github.com/FWGS/xash3d-fwgs/raw/master/game_launch/icon-xash-material.png" alt="Xash3D FWGS icon" />
[![builds.sr.ht status](https://builds.sr.ht/~a1batross/xash3d-fwgs.svg)](https://builds.sr.ht/~a1batross/xash3d-fwgs?) [![GitHub Actions Status](https://github.com/FWGS/xash3d-fwgs/actions/workflows/c-cpp.yml/badge.svg)](https://github.com/FWGS/xash3d-fwgs/actions/workflows/c-cpp.yml) [![FreeBSD Build Status](https://img.shields.io/cirrus/github/FWGS/xash3d-fwgs?label=freebsd%20build)](https://cirrus-ci.com/github/FWGS/xash3d-fwgs) [![Discord Server](https://img.shields.io/discord/355697768582610945.svg)](http://fwgsdiscord.mentality.rip/) \
[![Download Stable](https://img.shields.io/badge/download-stable-yellow)](https://github.com/FWGS/xash3d-fwgs/releases/latest) [![Download Testing](https://img.shields.io/badge/downloads-testing-orange)](https://github.com/FWGS/xash3d-fwgs/releases/tag/continuous)

Xash3D FWGS is a game engine, aimed to provide compatibility with Half-Life Engine and extend it, as well as to give game developers well known workflow.

Xash3D FWGS is a heavily modified fork of an original [Xash3D Engine](https://www.moddb.com/engines/xash3d-engine) by Unkle Mike.

## Donate
[![Donate to FWGS button](https://img.shields.io/badge/Donate_to_FWGS-%3C3-magenta)](Documentation/donate.md) \
If you like Xash3D FWGS, consider supporting individual engine maintainers. By supporting us, you help to continue developing this game engine further. The sponsorship links are available in [documentation](Documentation/donate.md).

## Fork features
* Steam Half-Life (HLSDK 2.4) support.
* Crossplatform and modern compilers support: supports Windows, Linux, BSD & Android on x86 & ARM and [many more](Documentation/ports.md).
* Better multiplayer support: multiple master servers, headless dedicated server, voice chat and IPv6 support.
* Multiple renderers support: OpenGL, GLESv1, GLESv2 and Software.
* Advanced virtual filesystem: `.pk3` and `.pk3dir` support, compatibility with GoldSrc FS module, fast case-insensitivity emulation for crossplatform.
* Mobility API: better game integration on mobile devices (vibration, touch controls)
* Different input methods: touch and gamepad in addition to mouse & keyboard.
* TrueType font rendering, as a part of mainui_cpp.
* External VGUI support module.
* PNG & KTX2 image format support.
* [A set of small improvements](Documentation/), without broken compatibility.

## Installation & Running
0) Get Xash3D FWGS binaries: you can use [testing](https://github.com/FWGS/xash3d-fwgs/releases/tag/continuous) build or you can compile engine from source code.
1) Copy engine binaries to some directory.
2) Copy `valve` directory from [Half-Life](https://store.steampowered.com/app/70/HalfLife/) to directory with engine binaries.
If your CPU is NOT x86 compatible or you're running 64-bit version of the engine, you may want to compile [Half-Life SDK](https://github.com/FWGS/hlsdk-portable).
This repository contains our fork of HLSDK and restored source code for some of the mods. Not all of them, of course.
You still needed to copy `valve` directory as all game resources located there.
3) Run the main executable (`xash3d.exe` or AppImage).

For additional info, run Xash3D with `-help` command line key.

## Contributing
* Before sending an issue, check if someone already reported your issue. Make sure you're following "How To Ask Questions The Smart Way" guide by Eric Steven Raymond. Read more: http://www.catb.org/~esr/faqs/smart-questions.html
* Issues are accepted in both English and Russian
* Before sending a PR, check if you followed our contribution guide in CONTRIBUTING.md file.

## Build instructions
We are using Waf build system. If you have some Waf-related questions, I recommend you to read https://waf.io/book/

NOTE: NEVER USE GitHub's ZIP ARCHIVES. GitHub doesn't include external dependencies we're using!

### Prerequisites

If your CPU is x86 compatible, we are building 32-bit code by default. This was done to maintain compatibility with Steam releases of Half-Life and based on it's engine games.
Even if Xash3D FWGS does support targetting 64-bit, you can't load games without recompiling them from source code!

If your CPU is NOT x86 compatible or you decided build 64-bit version of engine, you may want to compile [Half-Life SDK](https://github.com/FWGS/hlsdk-portable).
This repository contains our fork of HLSDK and restored source code for some of the mods. Not all of them, of course.

#### Windows (Visual Studio)
* Install Visual Studio.
* Install latest [Python](https://python.org) **OR** run `cinst python.install` if you have Chocolatey.
* Install latest [Git](https://git-scm.com/download/win) **OR** run `cinst git.install` if you have Chocolatey.
* Download [SDL2](https://libsdl.org/download-2.0.php) development package for Visual Studio.
* Clone this repository: `git clone --recursive https://github.com/FWGS/xash3d-fwgs`.
* Make sure you have at least 12GB of free space to store all build-time dependencies: ~10GB for Visual Studio, 300 MB for Git, 100 MB for Python and other.

#### GNU/Linux
##### Debian/Ubuntu
* Enable i386 on your system, if you're compiling 32-bit engine on amd64. If not, skip this

`$ sudo dpkg --add-architecture i386`
* Install development tools
  * For 32-bit engine on amd64: \
    `$ sudo apt install build-essential gcc-multilib g++-multilib python libsdl2-dev:i386 libfontconfig-dev:i386 libfreetype6-dev:i386`
  * For everything else: \
    `$ sudo apt install build-essential python libsdl2-dev libfontconfig-dev libfreetype6-dev`
* Clone this repostory:
`$ git clone --recursive https://github.com/FWGS/xash3d-fwgs`

#### PSP
* Build pspsdk(GCC 9.3) from https://github.com/pspdev
* Clone this repository: `git clone --recursive https://github.com/Crow-bar/xash3d-fwgs`.

### Building
#### Windows (Visual Studio)
0) Open command line
1) Navigate to `xash3d-fwgs` directory.
2) Carefully examine which build options are available: `waf --help`
3) Configure build: `waf configure -T release --sdl2=c:/path/to/SDL2`
4) Compile: `waf build`
5) Install: `waf install --destdir=c:/path/to/any/output/directory`

#### Linux
If compiling 32-bit on amd64, you may need to supply `export PKG_CONFIG_PATH=/usr/lib/i386-linux-gnu/pkgconfig` prior to running configure.

0) Examine which build options are available: `./waf --help`
1) Configure build: `./waf configure -T release`
(You need to pass `-8` to compile 64-bit engine on 64-bit x86 processor)
2) Compile: `./waf build`
3) Install(optional): `./waf install --destdir=/path/to/any/output/directory`

#### PSP
0) Navigate to `xash3d-fwgs` directory.
1) Examine which build options are available: `./waf --help`
2) Configure build:
   Normal: `./waf configure -T fast --psp=prx,660,HW --prefix=/path/to/any/output/directory`
   Profiling: `./waf configure -T debug --psp=elf,660,HW --enable-profiling --prefix=/path/to/any/output/directory`
3) Compile: `./waf build`
4) Install(optional): `./waf install`


## Running
0) Copy libraries and main executable somewhere, if you're skipped installation stage.
1) Copy game files to same directory
2) Run `xash3d.exe`/`xash3d.sh`/`xash3d` depending on which platform you're using.

For additional info, run Xash3D with `-help` command line key.
