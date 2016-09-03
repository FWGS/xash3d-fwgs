====
Xash3D Android
====
[![Build Status](https://travis-ci.org/FWGS/xash3d-android-project.svg)](https://travis-ci.org/FWGS/xash3d-android-project)
### Users
#### Installation guide
0. Download the APK and extras.7z from Github Releases. https://github.com/SDLash3D/xash3d-android-project/releases/latest
1. Install the APK.
2. Create /sdcard/xash folder.
3. Copy "valve" folder from your Half-Life Steam version to /sdcard/xash/. Example: /sdcard/xash/valve -- game data
4. Unpack extras.7z. It have recommended game configs and sprites for WON-style menu.
5. Open game. 

#### Launching other mods
**Currently this port support only Half-Life. Only mods that doesn't have libraries will work perfectly. **

For example, if you want to play Half-Life: Uplink. 

1. Copy modification folder to /sdcard/xash
2. Open game and in console args write: 
> -game "NameOfModFolder"

#### Bugs

About all bugs please write to issues with your device and OS info. 

### Developers

+ For compiling, run `git submodule init && git submodule update`. Otherwise you will get an empty APK, without any libraries. 
+ ~~We use our SDL2 fork. See https://github.com/mittorn/SDL-mirror~~. Nevermind. We don't use SDL2 anymore.
