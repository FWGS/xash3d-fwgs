## PlayStation Vita port

### Prerequisites
1. Make sure your PSVita is [set up to run homebrew applications](https://vita.hacks.guide/).
2. Install [kubridge](https://github.com/TheOfficialFloW/kubridge/releases/). It is recommended to use kubridge version `0.1`, because other versions aren't tested, we don't know are they suitable or not.

   Worth to notice, we got reports that automatic plugins management app EasyPlugin have issues with installing kubridge plugin, so it's better to install it manually: by copying `kubridge.suprx` to your taiHEN plugins folder (usually `ux0:/tai`, but could be `ur0:/tai`) and add it to your `config.txt`, for example:
   ```
   *KERNEL
   ux0:tai/kubridge.skprx
   ```

3. Install `libshacccg.suprx` by following [this guide](https://cimmerian.gitbook.io/vita-troubleshooting-guide/shader-compiler/extract-libshacccg.suprx).

### Installation
1. If you have an old vitaXash3D install, remove it.
2. Get `xash3d-fwgs-psvita.7z` from the latest [automatic build](https://github.com/FWGS/xash3d-fwgs/releases/tag/continuous).
3. Install `xash.vpk` from the 7z archive onto your PSVita.
4. Copy the `data` directory from the 7z archive to the root of your PSVita's SD card.
5. Copy the valve folder and any other mod folders from your Half-Life install to `ux0:/data/xash3d/` (you can use other mountpoints instead of `ux0`). **Do not overwrite anything.**

### Build instructions
1. Install [VitaSDK](https://vitasdk.org/).
2. Build and install [vitaGL](https://github.com/Rinnegatamante/vitaGL):
    ```
    git clone https://github.com/Rinnegatamante/vitaGL.git
    make -C vitaGL NO_TEX_COMBINER=1 HAVE_UNFLIPPED_FBOS=1 HAVE_PTHREAD=1 SINGLE_THREADED_GC=1 MATH_SPEEDHACK=1 DRAW_SPEEDHACK=1 HAVE_CUSTOM_HEAP=1 -j2 install
    ```
3. Build and install [vita-rtld](https://github.com/fgsfdsfgs/vita-rtld):
    ```
    git clone https://github.com/fgsfdsfgs/vita-rtld.git && cd vita-rtld
    mkdir build && cd build
    cmake -DCMAKE_BUILD_TYPE=Release ..
    make -j2 install
    ```
4. Build and install [this SDL2 fork](https://github.com/Northfear/SDL) with vitaGL integration:
    ```
    git clone https://github.com/Northfear/SDL.git && cd SDL
    mkdir build && cd build
    cmake -DCMAKE_TOOLCHAIN_FILE=${VITASDK}/share/vita.toolchain.cmake -DCMAKE_BUILD_TYPE=Release -DVIDEO_VITA_VGL=ON ..
    make -j2 install
    ```
5. Use `waf`:
    ```
    ./waf configure -T release --psvita
    ./waf build
    ```
6. Copy all the resulting `.so` files into a single folder:
    ```
    ./waf install --destdir=xash3d
    ```
7. `xash.vpk` is located in `build/engine/`.
