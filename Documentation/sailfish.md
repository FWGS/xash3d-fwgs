## Sailfish OS port

Xash3D FWGS runs on Sailfish OS as a native app with its own launcher. The package is called `harbour-xash3d-fwgs`.

### Requirements

* Sailfish OS 5.1 or newer. Older releases aren't supported: the port depends on the system libraries and compositor features that came with 5.1.
* Aurora OS compatibility isn't guaranteed due to lack of devices, patches are welcome.
* An aarch64 or armv7hl device. The port was tested on Jolla C2 (5.1.0.11) and Jolla Phone 2026 (5.2.0.22), both aarch64. The armv7hl package is built too, but it hasn't been tested on a device yet.
* Game data from your own copy of Half-Life: the `valve` folder from the Steam installation, plus supported mods you want to play.

### Installation

1. In the Settings app, open Untrusted software and turn on Allow untrusted software.
2. On the phone, download the RPM from the latest [automatic build](https://github.com/FWGS/xash3d-fwgs/releases/tag/continuous): `harbour-xash3d-fwgs.aarch64.rpm` for 64-bit devices, `harbour-xash3d-fwgs.armv7hl.rpm` for 32-bit ones. If you aren't sure which one you need, run `uname -m` in the terminal: it prints `aarch64` or `armv7l`.
3. Open the downloaded file, for example from Settings > Transfers, and confirm the installation. Stay online while it installs: the app needs the system's OpenGL ES 1 library, which isn't installed by default and comes from the Jolla repositories.

   With developer mode on, you can install it from the terminal instead:
   ```
   devel-su pkcon install-local harbour-xash3d-fwgs.aarch64.rpm
   ```
4. Start Xash3D FWGS once. It creates the game data folder, `Documents/xash3d-fwgs` (or create the folder yourself).
5. Copy the `valve` folder and any mod folders from your Half-Life installation into `Documents/xash3d-fwgs`, for example over USB. **Do not overwrite anything.** The launcher finds them when you come back to it, or after Refresh in the pull-down menu.

To update, install a newer RPM the same way. Removing the app doesn't delete the game data folder.

### Game libraries

By default, like on Android, we ship `mobile_hacks` game libraries which support Half-Life, Half-Life: Blue Shift and other "simple" mods.

For other mods, the launcher downloads Linux ARM builds of the game libraries from [hlsdk-mega-build](https://github.com/FWGS/hlsdk-mega-build) the first time you start the mod.

Counter-Strike and Team Fortress Classic are not supported in `hlsdk-mega-build` yet, so without compiling them from the source, you can't run them.

### Known issues

* Landscape only. The game doesn't follow the orientation sensor: to play the other way up, enter `vid_rotate 3` in the console, and `vid_rotate 1` to go back. Portrait isn't supported.
* While you play with touch controls, edge swipes are blocked so the game doesn't get minimized by accident. They work as usual in the menu and in the console: open the menu to leave the game.
* The home screen shows for about a second when a game starts: the launcher closes, and the engine opens its own window.
* The game keeps running when you switch away from it, sound is muted.
* The app switcher shows a live, sideways picture of the game instead of a proper cover.

### Where files are

* Game data: `~/Documents/xash3d-fwgs`, unless another folder is picked in Settings. `engine.log` is written here.
* Launcher settings: `~/.config/su.xash/Engine`.
* Crash reports: `~/.local/share/su.xash/Engine/crashes`.

### Building on the device

The RPM can be built on the phone itself, no Sailfish SDK needed. It takes a few minutes, depending on the phone hardware.

1. Install zypper, then the build dependencies with it:
   ```
   devel-su pkcon install zypper
   devel-su zypper in gcc gcc-c++ make git python3-base rpm-build meego-rpm-config SDL2-devel glib2-devel libaudioresource-devel wayland-devel zlib-devel bzip2-devel libogg-devel libvorbis-devel libsailfishapp-devel qt5-qtcore-devel qt5-qtgui-devel qt5-qtnetwork-devel qt5-qtdeclarative-devel qt5-qtdeclarative-qtquick-devel qt5-qttools-linguist
   ```
   The Qt development packages pull in libhybris development packages that need `android-headers`, which no repository has. zypper reports a problem for each of them: pick the solution that breaks the package by ignoring some of its dependencies. **Never pick a solution that removes packages**, like libhybris, droid-hal or hwcomposer ones: they're part of the device adaptation. The build doesn't need the missing headers.

   If an OS update complains about these packages later, remove them before updating:
   ```
   devel-su zypper rm libhybris-devel libhybris-libEGL-devel libhybris-libGLESv2-devel
   ```
2. Get the sources. The game libraries that come with the app are built from the `mobile_hacks` branch of hlsdk-portable:
   ```
   git clone --depth 1 --recursive --shallow-submodules https://github.com/FWGS/xash3d-fwgs
   git clone --depth 1 --recursive --shallow-submodules -b mobile_hacks https://github.com/FWGS/hlsdk-portable
   ```
3. Pack them the way the spec file expects, without a top-level directory. Keep `.git` in the archive: the engine gets its commit and build number from it.
   ```
   version=$(sed -n 's/^Version:[[:space:]]*//p' xash3d-fwgs/sailfish/harbour-xash3d-fwgs.spec)
   mkdir -p ~/rpmbuild/SOURCES
   tar czf ~/rpmbuild/SOURCES/harbour-xash3d-fwgs-$version.tar.gz -C xash3d-fwgs .
   tar czf ~/rpmbuild/SOURCES/hlsdk-portable.tar.gz -C hlsdk-portable .
   ```
4. Build and install it. Run `rpmbuild` in the `xash3d-fwgs` folder: the package's release number is the engine's build number, which the spec file gets from git.
   ```
   cd xash3d-fwgs
   rpmbuild -ba sailfish/harbour-xash3d-fwgs.spec
   devel-su pkcon install-local ~/rpmbuild/RPMS/*/harbour-xash3d-fwgs-*.rpm
   ```
