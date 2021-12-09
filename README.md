# Vulkan plus Ray Tracing (RTX) temporary fork of Xash3D FWGS engine
[![GitHub Actions Status](https://github.com/w23/xash3d-fwgs/actions/workflows/c-cpp.yml/badge.svg)](https://github.com/w23/xash3d-fwgs/actions/workflows/c-cpp.yml)

## TL;DR
- This fork adds Vulkan renderer to Xash3D-FWGS engine.
- This is work-in-progress. It is in very early stages and is not ready for unsupervised usage.
- Vulkan renderer targets two different modes:
  - Traditional rasterizer. It is intended to produce pixel-perfect identical frames to existing GL renderer as possible.
  - Ray tracing. It implements real time path traced global illumination lighting with PBR materials. It will look noticeably different from original game.
- It is intended to be merged back into upstream/master when it gets mature and stable enough.
- It primarily focuses Half-Life 1 game. Mods compatibility is not being considered at this time. This may change with maturity of the new renderer.
- Ray tracing requires 64-bit build. 32-bit drivers do not expose vulkan ray tracing extensions.

## Current status
- Not ready for any use.
- See Issues and [ref_vk/TODO.md](ref_vk/TODO.md)
- Traditional rasterizer works with some issues.
	- Works on Windows and Linux with any Vulkan GPU (and at some point it worked on Raspberry Pi 4 even).
	- It is slower than OpenGL renderer (1. I suck at Vulkan. 2. No visibility culling is performed).
	- Some features are not implemented yet: most of blending modes, some studio models features, sprites, beams, decals, tri api, etc)
- Ray tracer mostly works too, with dynamic GI and stuff.
	- It misses roughly the same set of features as traditional rasterizer (code is the same for the most part).
	- It also requires material remaster (i.e. newer textures for PBR parameters) and missing RAD files for most of the game maps. Work on these haven't been started yet.
	- Works under both Windows and Linux.
	- Works on both AMD and Nvidia GPUs.
- If you feel adventurous, you can follow [build instructions](https://github.com/w23/xash3d-fwgs/wiki/64-bit-build-on-Windows). Note that they might be slightly out of date, kek.

## Follow development
This project is 99.999% developed live on stream. I'm not a graphcis programmer, and have no idea what I'm doing. I'm essentially learning Vulkan, game engine renderer development, linear algebra, and ray tracing techniques while getting hands dirty with this. This is all for your amusement.

You can watch me making a fool of myself publicly here:
- [Archive playlist on YouTube/floba23](https://www.youtube.com/playlist?list=PLP0z1CQXyu5CrDa522FklxbOC0SM_Manl)
- [Twitch/provod](https://twitch.tv/provod)

---

Regular upstream Xash3D README.md follows.

---

# Xash3D FWGS Engine
[![GitHub Actions Status](https://github.com/FWGS/xash3d-fwgs/actions/workflows/c-cpp.yml/badge.svg)](https://github.com/FWGS/xash3d-fwgs/actions/workflows/c-cpp.yml) [![FreeBSD Build Status](https://img.shields.io/cirrus/github/FWGS/xash3d-fwgs?label=freebsd%20build)](https://cirrus-ci.com/github/FWGS/xash3d-fwgs) [![Discord Server](https://img.shields.io/discord/355697768582610945.svg)](http://discord.fwgs.ru/) \
[![Download Stable](https://img.shields.io/badge/download-stable-yellow)](https://github.com/FWGS/xash3d-fwgs/releases/latest) [![Download Testing](https://img.shields.io/badge/downloads-testing-orange)](https://github.com/FWGS/xash3d-fwgs/releases/tag/continuous) 

Xash3D FWGS is a fork of Xash3D Engine by Unkle Mike with extended features and crossplatform.

```
Xash3D is a game engine, aimed to provide compatibility with Half-Life Engine, 
as well as to give game developers well known workflow and extend it.
Read more about Xash3D on ModDB: https://www.moddb.com/engines/xash3d-engine
```

## Fork features
* HLSDK 2.4 support.
* Crossplatform: supported x86 and ARM on Windows/Linux/BSD/Android. ([see docs for more info](Documentation/ports.md))
* Modern compilers support: say no more to MSVC6.
* Better multiplayer support: multiple master servers, headless dedicated server.
* Mobility API: allows better game integration on mobile devices(vibration, touch controls)
* Different input methods: touch, gamepad and classic mouse & keyboard.
* TrueType font rendering, as a part of mainui_cpp.
* Multiple renderers support: OpenGL, GLESv1, GLESv2, Software
* A set of small improvements, without broken compatibility.

## Planned fork features
* Virtual Reality support and game API
* Voice support
* Vulkan renderer

## Installation & Running
0) Get Xash3D binaries: you can use [testing](https://github.com/FWGS/xash3d-fwgs/releases/tag/continuous) build or you can compile engine from source code.
Choose proper build package depending on which platform you're using.
1) Copy engine binaries to some directory.
2) Copy `valve` directory from [Half-Life](https://store.steampowered.com/app/70/HalfLife/) to directory with engine binaries.
Also if you're using Windows: you should copy `vgui.dll` library from Half-Life directory to Xash3D directory.
As alternative, you can compile [hlsdk-xash3d](https://github.com/FWGS/hlsdk-xash3d) instead of using official Valve game binaries, but you still needed to copy `valve` directory as all game resources located in there.
3) Download [extras.pak](https://github.com/FWGS/xash-extras/releases/tag/v0.19.2) and place it to `valve` directory.
4) Run `xash3d.exe`/`xash3d.sh`/`xash3d` depending on which platform you're using.

Note: on Linux, you may need to create an sh file with the command `LD_LIBRARY_PATH=. ./xash3d`.

For additional info, run Xash3D with `-help` command line key.

## Contributing
* Before sending an issue, check if someone already reported your issue. Make sure you're following "How To Ask Questions The Smart Way" guide by Eric Steven Raymond. Read more: http://www.catb.org/~esr/faqs/smart-questions.html
* Issues are accepted in both English and Russian
* Before sending a PR, check if you followed our contribution guide in CONTRIBUTING.md file.

## Build instructions
We are using Waf build system. If you have some Waf-related questions, I recommend you to read https://waf.io/book/

NOTE: NEVER USE GitHub's ZIP ARCHIVES. GitHub doesn't include external dependencies we're using!

### Prerequisites
#### Windows (Visual Studio)
* Install Visual Studio.
* Install latest [Python](https://python.org) **OR** run `cinst python.install` if you have Chocolatey.
* Install latest [Git](https://git-scm.com/download/win) **OR** run `cinst git.install` if you have Chocolatey.
* Download [SDL2](https://libsdl.org/download-2.0.php) development package for Visual Studio.
* Clone this repository: `git clone --recursive https://github.com/FWGS/xash3d-fwgs`.
* Make sure you have at least 12GB of free space to store all build-time dependencies: ~10GB for Visual Studio, 300 MB for Git, 100 MB for Python and other.

#### GNU/Linux
NOTE FOR USERS WITH X86 COMPATIBLE CPUs:
We have forced Waf to throw an error, if you're trying to build 64-bit engine. This was done for keeping compatibility with Steam releases of Half-Life and based on it's engine games.
Even if Xash3D FWGS does support targetting 64-bit, you can't load games without recompiling them from source code!

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

### Building
#### Windows (Visual Studio)
0) Open command line
1) Navigate to `xash3d-fwgs` directory.
2) Carefully examine which build options are available: `waf --help`
3) Configure build: `waf configure -T release --sdl2=c:/path/to/SDL2`
4) Compile: `waf build`
5) Install: `waf install --destdir=c:/path/to/any/output/directory`

#### Linux
0) Examine which build options are available: `./waf --help`
1) Configure build: `./waf configure -T release`
(You need to pass `-8` to compile 64-bit engine on 64-bit x86 processor)
2) Compile: `./waf build`
3) Install(optional): `./waf install --destdir=/path/to/any/output/directory`

Note: if compiling 32-bit on amd64, you may need to supply `export PKG_CONFIG_PATH=/usr/lib/i386-linux-gnu/pkgconfig` prior to running configure.   

