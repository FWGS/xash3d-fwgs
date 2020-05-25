# Xash3D FWGS Engine
[![Build Status](https://api.travis-ci.org/FWGS/xash3d-fwgs.svg?branch=master)](https://travis-ci.org/FWGS/xash3d-fwgs) [![FreeBSD Build Status](https://img.shields.io/cirrus/github/FWGS/xash3d-fwgs?label=freebsd%20build)](https://cirrus-ci.com/github/FWGS/xash3d-fwgs) [![Discord Server](https://img.shields.io/discord/355697768582610945.svg)](https://discord.gg/TbnHcVb) \
[![Download Stable](https://img.shields.io/badge/download-stable-yellow)](https://github.com/FWGS/xash3d-fwgs/releases/latest) [![Download Testing](https://img.shields.io/badge/downloads-testing-orange)](https://github.com/FWGS/xash3d-deploy/tree/anewengine-master)

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

## Contributing
* Before sending an issue, check if someone already reported your issue. Make sure you're following "How To Ask Questions The Smart Way" guide by Eric Steven Raymond. Read more: http://www.catb.org/~esr/faqs/smart-questions.html
* Before sending a PR, check if you followed our coding guide in CODING_STYLE.md file.

## Build instructions
We are using Waf build system. If you have some Waf-related questions, I recommend you to read https://waf.io/book/

If you're stuck somewhere and you need a clear example, read `.travis.yml` and `scripts/build*.sh`.

NOTE: NEVER USE GitHub's ZIP ARCHIVES. They are broken and don't contain external dependencies sources we're using.

### Prerequisites
#### Windows(Visual Studio)
* Install Visual Studio.
* Install latest [Python](https://python.org) **OR** run `cinst python.install` if you have Chocolatey.
* Install latest [Git](https://git-scm.com/download/win) **OR** run `cinst git.install` if you have Chocolatey.
* Download [SDL2](https://libsdl.org/download-2.0.php) development package for Visual Studio.
* Clone this repository: `git clone --recursive https://github.com/FWGS/xash3d-fwgs`.
* Make sure you have at least 12GB of free space to store all build-time dependencies: ~10GB for Visual Studio, 300 MB for Git, 100 MB for Python and other.

#### GNU/Linux
NOTE FOR USERS WITH X86 COMPATIBLE CPUs:
We have forced build system to throw an error, if you're trying to build 64-bit engine. This done for keeping compatibility with Steam releases of Half-Life and based on it's engine games.
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

#### ArchLinux
* ArchLinux users are lucky enough and there is a `xash3d-git` AUR package for them.

### Building
#### Windows(Visual Studio)
0) Open command line
1) Navigate to `xash3d-fwgs` directory.
2) Carefully examine which build options are available: `waf --help`
3) Configure build: `waf configure -T release --sdl2=c:/path/to/SDL2 --prefix=c:/path/to/any/output/directory`
4) Compile: `waf build`
5) Install: `waf install`

#### Linux
0) Examine which build options are available: `./waf --help`
1) Configure build: `./waf configure -T release --prefix=/path/to/any/output/directory`
(To compile 64-bit engine on 64-bit x86 processor, you need to pass `-8` also)
2) Compile: `./waf build`
3) Install(optional): `./waf install`

## Running
0) Copy libraries and main executable somewhere, if you're skipped installation stage.
1) Copy game files to same directory
2) Run `xash3d.exe`/`xash3d.sh`/`xash3d` depending on which platform you're using.

For additional info, run Xash3D with `-help` command line key.
