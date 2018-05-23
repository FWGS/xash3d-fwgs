Xash3D FWGS Android
====
[![Build Status](https://travis-ci.org/FWGS/xash3d-android-project.svg)](https://travis-ci.org/FWGS/xash3d-android-project)
### Users
#### Installation guide
0. Download the APK from Xash3D FWGS repo releases page: https://github.com/FWGS/xash3d/releases/latest
1. Install the APK.
2. Create /sdcard/xash folder.
3. Copy "valve" folder from your Half-Life Steam version to /sdcard/xash/. Example: /sdcard/xash/valve -- game data
4. Run the game. 

#### Launching other mods
**This app can run only Half-Life and mods that doesn't have own game libraries.**

**Any mod with own custom game libraries required separate launcher with mod game libraries which was ported on Android.**

For example, if you want to play Half-Life: C.A.G.E.D.

1. Copy caged folder from your steam version of Half-Life: C.A.G.E.D. to /sdcard/xash.
2. Open Xash3D FWGS launcher and write to the command-line args: 
> -game caged

Example for Half-Life: Blue Shift.

1. Copy bshift folder from your steam version of Half-Life: Blue Shift to /sdcard/xash.
2. Install separate launcher for Half-Life: Blue Shift from [here](https://github.com/nekonomicon/BS-android/releases/latest) and run.

You can always find mods with own game libraries which was ported on Android( Actually separate launchers with game libraries which was ported on Android ) in [Play Market](https://play.google.com/store/apps/dev?id=7039367093104802597) and [ModDB](https://www.moddb.com/games/xash3d-android/downloads).

For more information about supported mods, see this [article](https://github.com/FWGS/xash3d/wiki/List-of-mods-which-work-on-Android-and-other-non-Windows-platforms-without-troubles).

#### Bugs

About all bugs please write to issues with your device and OS info. 

### Developers

+ For compiling, run `git submodule init && git submodule update --init --recursive`. Otherwise you will get an empty APK, without any libraries. 
+ ~~We use our SDL2 fork. See https://github.com/mittorn/SDL-mirror~~. Nevermind. We don't use SDL2 anymore.
