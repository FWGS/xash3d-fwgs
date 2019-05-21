# Xash3D FWGS Engine
[![Build Status](https://api.travis-ci.org/FWGS/xash3d-fwgs.svg?branch=master)](https://travis-ci.org/FWGS/xash3d-fwgs) [![Discord Server](https://img.shields.io/discord/355697768582610945.svg)](https://discord.gg/TbnHcVb)

Xash3D FWGS is a fork of Xash3D Engine by Unkle Mike with extended features and crossplatform.

```
Xash3D is a game engine, aimed to provide compatibility with Half-Life Engine, 
as well as to give game developers well known workflow and extend it.
Read more about Xash3D on ModDB: https://www.moddb.com/engines/xash3d-engine
```

Latest release build: https://github.com/FWGS/xash3d-fwgs/releases/latest

Latest development build: https://github.com/FWGS/xash3d-deploy/tree/newengine-latest

## Fork features
* HLSDK 2.4 support.
* Crossplatform: officially supported x86 and ARM on Windows/Linux/BSD/macOS/Android/iOS/Haiku.
* Modern compilers support: say no more to MSVC6.
* Better multiplayer support: multiple master servers, headless dedicated server.
* Mobility API: allows better game integration on mobile devices(vibration, touch controls)
* Different input methods: touch, gamepad and classic mouse & keyboard.
* TrueType font rendering, as a part of mainui_cpp.
* A set of small improvements, without broken compatibility.

## Planned fork features
* Virtual Reality support and game API(in development!)
* Voice support
* Multiple renderers support(OpenGL, GLES, Vulkan, software)

## Contributing
* Before sending an issue, check if someone already reported your issue. Make sure you're following "How To Ask Questions The Smart Way" guide by Eric Steven Raymond. Read more: http://www.catb.org/~esr/faqs/smart-questions.html
* Before sending a PR, check if you followed our coding guide in CODING_STYLE.md file.

## Build instructions
We are using Waf build system. If you have some Waf-related questions, I recommend you to read https://waf.io/book/

If you're stuck somewhere and you need a clear example, read `.travis.yml` and `scripts/build*.sh`.

### Prerequisites
#### Windows(Visual Studio)
* Install Visual Studio.
* Install latest [Python](https://python.org) **OR** run `cinst python.install` if you have Chocolatey.
* Install latest [Git](https://git-scm.com/download/win) **OR** run `cinst git.install` if you have Chocolatey.
* Download [SDL2](https://libsdl.org/download-2.0.php) development package for Visual Studio.
* Clone this repository: `git clone --recursive https://github.com/FWGS/xash3d-fwgs`.
* Clone `vgui-dev` repository: `git clone https://github.com/FWGS/vgui-dev`.
* Make sure you have at least 12GB of free space to store all build-time dependencies: ~10GB for Visual Studio, 300 MB for Git, 100 MB for Python and other.

#### Linux
NOTE: Make sure you're OK with targetting 32-bit.

Even if Xash3D FWGS does support targetting 64-bit, you can't load games without recompiling them from source code!

* **Gentoo**: TODO
* **Debian**: TODO
* **ArchLinux**: `<AUR Helper> -S xash3d-git`

### Building
#### Windows(Visual Studio)
0) Open command line
1) Navigate to `xash3d-fwgs` directory.
2) Carefully examine which build options are available: `waf --help`
3) Configure build: `waf configure --build-type=release --sdl2=c:/path/to/SDL2 --vgui=c:/path/to/vgui-dev --prefix=c:/path/to/any/output/directory`
4) Compile: `waf build`
5) Install: `waf install`

#### Linux
0) Examine which build options are available: `./waf --help`
1) Configure build: `./waf configure --build-type=release --vgui=vgui-dev`
2) Compile: `./waf build`
3) Install(optional): `./waf install`

#### iOS
0) Open command line
1) Navigate to `xash3d-fwgs` directory.
2) Navigate to `ref_gl` directory.
3) Clone `nanogl` and `gl-wes-v2`: `git clone https://github.com/FWGS/gl-wes-v2 && git clone https://github.com/FWGS/nanogl`
3) Carefully examine which build options are available: `waf --help`
4) Configure build: `./waf configure -s /path/to/SDL2 --build-type=release --ios`
5) Compile: `./waf build`
This will leave the compiled binaries (and libraries!) inside the build directory.

## Running
0) Copy libraries and main executable somewhere, if you're skipped installation stage.
1) Copy game files to same directory
2) Run `xash3d.exe`/`xash3d.sh`/`xash3d` depending on which platform you're using.

For additional info, run Xash3D with `-help` command line key.
