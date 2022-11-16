# Cross-compiling for Windows with Wine

This can be useful to test engine in Wine without using virtual machines or dual-booting to Windows.

0. Clone and install https://github.com/mstorsjo/msvc-wine (you can skip CMake part)
1. Set environment variable WINE_MSVC_PATH to the path to installed MSVC toolchain
2. Pre-load wine: `wineserver -k; wineserver -p; wine64 wineboot`
3. Run `./waf configure -T <build-type> --enable-wine-msvc --sdl2=../SDL2_VC`. Configuration step will take more time than usual.
4. Follow other typical steps to build from console, except keep in mind you're in decent Operating System now.
