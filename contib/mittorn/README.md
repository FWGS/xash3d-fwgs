**Table of Contents**  *generated with [DocToc](http://doctoc.herokuapp.com/)*

- [Building](#building)
        - [Manual build (without CMake)](#manual-build-without-cmake)
                - [Building Engine](#building-engine)
                - [Building launch binary](#building-launch-binary)
        - [Building on Windows](#building-on-windows)
                - [Visual Studio 2013 (recommended for Windows)](#visual-studio-2013-recommended-for-windows)
                - [Visual Studio 6](#visual-studio-6)
                - [MinGW](#mingw)
        - [Building Game mods](#building-game-mods)
                - [Linux](#linux)
                        - [microndk](#microndk)
                        - [Makefile.linux](#makefile-linux)
                - [Windows](#windows)
- [Running](#running)
        - [Manual running](#manual-running)
        - [Running with GDB](#running-with-gdb)
        - [Using DLL Loader](#using-dll-loader)
        - [Running MinGW builds](#running-mingw-builds)
                - [Running MinGW builds under GDB](#running-mingw-builds-under-gdb)

# Building

## Manual build (without CMake)

### Building engine

Clone Xash3D repository using git:

    git clone https://github.com/FWGS/xash3d

Move to the Xash3D folder:

    cd xash3d/engine

    make -f Makefile.linux XASH_VGUI=1

or same for dedicated

    make -f Makefile.linux XASH_DEDICATED=1

To enable dll support on linux, build loader library:

    cd ../loader
    make -f Makefile.linux libloader.so libloader.a
    cp libloader.so libloader.a ../engine/

And built engine with XASH_DLL_LOADER=1:

    cd ../engine
    make -f Makefile.linux XASH_VGUI=1 XASH_SDL=1 XASH_DLL_LOADER=1

or same for dedicated server

    cd ../engine
    make -f Makefile.linux XASH_DEDICATED=1 XASH_DLL_LOADER=1

### Building engine in separate library

Some old distros does not support hidden symbol visibility
This results in crash when loading server library

Building launch binary

    cd (xash3d)/game_launch
    gcc xash.c -o xash -ldl -lrt -lm

Building engine library:

    make -f Makefile.linux XASH_SINGLE_BINARY=0

## Building on Windows

### Visual Studio 2013 (recommended for Windows)

Download latest prebuilt SDL2 from

https://www.libsdl.org/release/SDL2-devel-2.0.4-VC.zip

Unzip and rename `SDL2-2.0.4` folder to `SDL2` and put it next to xash3d project folder.

    ..\xash3d\
    ..\SDL2\

Open `xash.sln` with Visual Studio 2013 and make a build. After building, copy contents of `Debug` or
`Release` folder to directory you choose. Copy `valve` folder and `vgui.dll` from your Half Life game
installation folder and `SDL2.dll` form `\SDL2\lib\x86\` to it.
Move `vgui_support.dll` into `valve` folder.

    ..\valve\
    ..\valve\vgui_support.dll
    ..\menu.dll
    ..\SDL2.dll
    ..\vgui.dll
    ..\xash.dll
    ..\xash.exe

Now you good to go, just run `xash.exe`.

### Visual Studio 6

This is legacy configuration, but msvc6 seems to generate more stable and more effective code

Setup your msvc6 enviroment, unpack SDL2-2.0.4 to xash3d folder and do:

    cd (xash3d)\engine
    ..\msvc6\build.bat
    cd (xash3d)\game_launch
    ..\msvc6\build_launch.bat

### MinGW

The most effective configuration, but maybe unstable.
Setup your MinGW environment and run:

    cd (xash3d)\engine\
    CC=gcc mingw32-make -f Makefile.mingw

    engine will be built to single exe binary

## Building game mods

### Linux

#### microndk

All mods that ported to android may be build to linux using Android.mk with microndk:

[https://github.com/SDLash3D/microndk]

Clone microndk repo somewhere, change xash3d_config to preffered configuration (change arch to x86
for example)

Go to dlls folder if you are building server or cl_dlls if you are building client and do:

    make -f /path/to/microndk/microndk.mk -j4

Do:

    make -f /path/to/microndk/microndk.mk -j4 clean

every time when you build client after building server

#### Makefile.linux

### Windows

On windows common way is using Visual Studio as many mods does not correctly work with mingw.

Just open project and build it.

Other is using mingw and microndk, but it was not tested yet.

hlsdk-xash3d based mods may work fine with mingw.

You may use microndk to build it. Build process is very similar to linux one.

After setting up MinGW enviroment, do:<br>
`mingw32-make -f \path\to\microndk\Microndk.mk`

to build 64-bit library, use:<br>
`mingw32-make -f \path\to\microndk\Microndk.mk 64BIT=1`

Edit xash3d_config.mk to set optimal CFLAGS if you are running server

# Running

Copy **valve** folder from Half-Life:

    cp -r $HOME/.steam/steam/steamapps/common/Half-Life/valve $HOME/Games/Xash3D

**NOTE**: If you build with CMake, you can now use `make install`. It will install binaries where
they must be located. So just run `xash3d` from command line in directory where is gamedata is located.
For additional info look to the CMakeLists.txt in repo root and xash3d.sh script.

After a successful build, copy the next files to some other directory where you want to run Xash3D:

    cp engine/libxash.so game_launch/xash3d mainui/libxashmenu.so $HOME/Games/Xash3D

If you built it with XASH_VGUI, also copy vgui.so:

    cp ../hlsdk/linux/vgui.so vgui_support/libvgui_support.so $HOME/Games/Xash3D

Copy script:

    cp ../xash3d.sh $HOME/Games/Xash3D

Run:

    cd $HOME/Games/Xash3D
    ./xash3d.sh

## Manual running

Put xash3d binaries and vgui(optionally) to you game data directory and run:

    LD_LIBRARY_PATH=. ./xash -dev 5

## Running under GDB

    LD_LIBRARY_PATH=. gdb --args ./xash -dev 5

## Using DLL Loader

Put vgui_support.dll from windows build to your game data folder and run:

    LD_LIBRARY_PATH=. ./xash -dev 5 -vguilib vgui.dll -clientlib valve/cl_dlls/client.dll -dll dlls/hl.dll

## Running MinGW builds

Put exe file to your game data directory

    cd (game)\
    xash_bin -dev 5

### Running MinGW builds under GDB

    gdb --args ./xash_bin.exe -dev 5

# Useful GDB commands

Start or restart process:

    (gdb) r

Show backtrace:

    (gdb) bt

Full backtrace:

    (gdb) bt full

Continue execution:

    (gdb) c

Recover from segmentation fault:

Skipping function:

    (gdb) handle SIGSEGV nopass
    (gdb) ret
    (gdb) c

Restart frame for beginning:

    (gdb) handle SIGSEGV nopass
    (gdb) jump *Host_AbortCurrentFrame
Anti-Crash script (useful for scripting servers):
```
cd /path/to/rootdir
file xash
set args xash -dev 5 -nowcon
handle SIGSEGV nopass
catch signal SIGSEGV
set $crashes=0
commands
print $crashes++
if $crashes > 500
set $crashes=0
run
else
jump *Host_AbortCurrentFrame
end
end
run
```
