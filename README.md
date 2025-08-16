# Xash3D FWGS Engine <img align="right" width="128" height="128" src="https://github.com/FWGS/xash3d-fwgs/raw/master/game_launch/icon-xash-material.png" alt="Xash3D FWGS icon" />
[![GitHub Actions Status](https://github.com/FWGS/xash3d-fwgs/actions/workflows/c-cpp.yml/badge.svg)](https://github.com/FWGS/xash3d-fwgs/actions/workflows/c-cpp.yml) [![FreeBSD Build Status](https://img.shields.io/cirrus/github/FWGS/xash3d-fwgs?label=freebsd%20build)](https://cirrus-ci.com/github/FWGS/xash3d-fwgs) \
[![Discord Server](https://img.shields.io/discord/355697768582610945?logo=Discord&label=International%20Discord%20chat)](http://fwgsdiscord.mentality.rip/) [![Russian speakers Telegram Chat](https://img.shields.io/badge/Russian_speakers_Telegram_chat-gray?logo=Telegram)](https://t.me/flyingwithgauss) \
[![Download Daily Build](https://img.shields.io/badge/downloads-testing-orange)](https://github.com/FWGS/xash3d-fwgs/releases/tag/continuous)

Xash3D ([pronounced](https://ipa-reader.com/?text=ks%C9%91%CA%82) `[ksɑʂ]`) FWGS is a game engine, aimed to provide compatibility with Half-Life Engine and extend it, as well as to give game developers well known workflow.

Xash3D FWGS is a heavily modified fork of an original [Xash3D Engine](https://www.moddb.com/engines/xash3d-engine) by Unkle Mike.

## Donate
[![Donate to FWGS button](https://img.shields.io/badge/Donate_to_FWGS-%3C3-magenta)](Documentation/donate.md) \
If you like Xash3D FWGS, consider supporting individual engine maintainers. By supporting us, you help to continue developing this game engine further. The sponsorship links are available in [documentation](Documentation/donate.md).

## Fork features
* Steam Half-Life (HLSDK 2.5) support.
* Crossplatform and modern compilers support: supports Windows, Linux, BSD & Android on x86 & ARM and [many more](Documentation/ports.md).
* Better multiplayer: multiple master servers, headless dedicated server, voice chat, [GoldSrc protocol support](Documentation/goldsrc-protocol-support.md) and IPv6 support.
* Multiple renderers support: OpenGL, GLESv1, GLESv2 and Software.
* Advanced virtual filesystem: `.pk3` and `.pk3dir` support, compatibility with GoldSrc FS module, fast case-insensitivity emulation for crossplatform.
* Mobility API: better game integration on mobile devices (vibration, touch controls).
* Different input methods: touch and gamepad in addition to mouse & keyboard.
* TrueType font rendering, as a part of mainui_cpp.
* External VGUI support module.
* PNG & KTX2 image format support.
* Ogg Vorbis (`.ogg`) & Ogg Opus (`.opus`) audio formats support.
* [A set of small improvements](Documentation/), without broken compatibility.

## Installation & Running
0) Get Xash3D FWGS binaries: you can use [testing](https://github.com/FWGS/xash3d-fwgs/releases/tag/continuous) build or you can compile engine from source code.
1) Copy engine binaries to some directory.
2) Copy `valve` directory from [Half-Life](https://store.steampowered.com/app/70/HalfLife/) to directory with engine binaries.
If your CPU is NOT x86 compatible or you're running 64-bit version of the engine, you may want to compile [Half-Life SDK](https://github.com/FWGS/hlsdk-portable).
This repository contains our fork of HLSDK and restored source code for Half-Life expansions and some mods.
You still needed to copy `valve` directory as all game resources located there.
3) Run the main executable (`xash3d.exe` or AppImage).

For additional info, run Xash3D with `-help` command line key.

### Android
0) Install the APK file.
1) Copy `valve` directory to a folder named `xash` in the Internal storage.
2) Run games from within the app.

## Contributing
* Before sending an issue, check if someone already reported your issue. Make sure you're following "How To Ask Questions The Smart Way" guide by Eric Steven Raymond. Read more: http://www.catb.org/~esr/faqs/smart-questions.html.
* Issues are accepted in both English and Russian.
* Before sending a PR, check if you followed our contribution guide in CONTRIBUTING.md file.

## Build instructions
We are using Waf build system. If you have some Waf-related questions, I recommend you to read [Waf Book](https://waf.io/book/).

**NOTE: NEVER USE GitHub's ZIP ARCHIVES. GitHub doesn't include external dependencies we're using!**

### Prerequisites
If your CPU is x86 compatible and you're on Windows or Linux, we are building 32-bit code by default. This was done to maintain compatibility with Steam releases of Half-Life and based on it's engine games.
Even if Xash3D FWGS does support targetting 64-bit, you can't load games without recompiling them from source code!

If your CPU is NOT x86 compatible or you decided build 64-bit version of engine, you may want to compile [Half-Life SDK](https://github.com/FWGS/hlsdk-portable).
This repository contains our fork of HLSDK and restored source code for Half-Life expansions and some mods.

#### Windows (Visual Studio)
* Install Visual Studio.
* Install latest [Python](https://python.org) **OR** run `cinst python.install` if you have Chocolatey.
* Install latest [Git](https://git-scm.com/download/win) **OR** run `cinst git.install` if you have Chocolatey.
* Download [SDL2](https://libsdl.org/download-2.0.php) development package for Visual Studio.
* Clone this repository: `git clone --recursive https://github.com/FWGS/xash3d-fwgs`.
* Make sure you have at least 12GB of free space to store all build-time dependencies: ~10GB for Visual Studio, 300 MB for Git, 100 MB for Python and other.

#### GNU/Linux
##### Debian/Ubuntu
* Only for 32-bit engine on 64-bit x86 operating system:
  * Enable i386 on your system: `$ sudo dpkg --add-architecture i386`.
  * Install `aptitude` ([why?](https://github.com/FWGS/xash3d-fwgs/issues/1828#issuecomment-2415131759)):  `$ sudo apt update && sudo apt upgrade && sudo apt install aptitude`
  * Install development tools: `$ sudo aptitude --without-recommends install git build-essential gcc-multilib g++-multilib libsdl2-dev:i386 libfreetype-dev:i386 libopus-dev:i386 libbz2-dev:i386 libvorbis-dev:i386 libopusfile-dev:i386 libogg-dev:i386`.
  * Set PKG_CONFIG_PATH environment variable to point at 32-bit libraries: `$ export PKG_CONFIG_PATH=/usr/lib/i386-linux-gnu/pkgconfig`.

* For 64-bit engine on 64-bit x86 and other non-x86 systems:
  * Install development tools: `$ sudo apt install git build-essential python libsdl2-dev libfreetype6-dev libopus-dev libbz2-dev libvorbis-dev libopusfile-dev libogg-dev`.

* Clone this repostory: `$ git clone --recursive https://github.com/FWGS/xash3d-fwgs`.

##### RedHat/Fedora
* Only for 32-bit engine on 64-bit x86 operating system:
  * Install development tools: `$ sudo dnf install git gcc gcc-c++ glibc-devel.i686 SDL3-devel.i686 sdl2-compat-devel.i686 opus-devel.i686 freetype-devel.i686 bzip2-devel.i686 libvorbis-devel.i686 opusfile-devel.i686 libogg-devel.i686`.
  * Set PKG_CONFIG_PATH environment variable to point at 32-bit libraries: `$ export PKG_CONFIG_PATH=/usr/lib/pkgconfig`.

* For 64-bit engine on 64-bit x86 and other non-x86 systems:
  * Install development tools: `$ sudo dnf install git gcc gcc-c++ SDL3-devel sdl2-compat-devel opus-devel freetype-devel bzip2-devel libvorbis-devel opusfile-devel libogg-devel`.

* Clone this repostory: `$ git clone --recursive https://github.com/FWGS/xash3d-fwgs`.

#### Android (Windows/Linux/macOS)
* Install [Android Studio](https://developer.android.com/studio) (or the command line tools).
* Install [Python](https://python.org) (at least 2.7, latest is better).
* Install [Git](https://git-scm.com/download/win).
* Install [Ninja](https://ninja-build.org/).
* Install [CMake](https://cmake.org/) (for some dependencies).

* Clone this repostory: `$ git clone --recursive https://github.com/FWGS/xash3d-fwgs`.

### Building
#### Windows (Visual Studio)
0) Open command line.
1) Navigate to `xash3d-fwgs` directory.
2) (optional) Examine which build options are available: `waf --help`.
3) Configure build: `waf configure --sdl2=c:/path/to/SDL2`.
4) Compile: `waf build`.
5) Install: `waf install --destdir=c:/path/to/any/output/directory`.

#### Linux
If compiling 32-bit on amd64, make sure `PKG_CONFIG_PATH` from the previous step is set correctly, prior to running configure.

0) (optional) Examine which build options are available: `./waf --help`.
1) Configure build: `./waf configure` (you need to pass `-8` to compile 64-bit engine on 64-bit x86 processor).
2) Compile: `./waf build`.
3) Install: `./waf install --destdir=/path/to/any/output/directory`.

#### Android (Windows/Linux/macOS)
You can just open the `android` folder in Android Studio and build from here, or use `gradlew` to build from command line.