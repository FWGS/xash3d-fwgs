# Cross-compiling for Windows with Wine

This can be useful to test engine in Wine without using virtual machines or dual-booting to Windows.

0. Clone and install https://github.com/mstorsjo/msvc-wine (you can skip CMake part)
1. Set environment variable MSVC_WINE_PATH to the path to installed MSVC toolchain
2. Pre-load wine: `wineserver -k; wineserver -p; wine64 wineboot`
3. Run `PKGCONFIG=/bin/false ./waf configure -T <build-type> --enable-wine-msvc --sdl2=../SDL2_VC`. Configuration step will take more time than usual.
4. .. other typical steps to build from console ...

> [!NOTE]
> Notice the usage of PKGCONFIG=/bin/false here. We're disabling pkg-config so we don't accidentally pull
> system-wide dependencies and force building them from source. In future builds we might set custom
> directory to pull dependencies from, like ffmpeg...
