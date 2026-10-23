# PlayStation 4 port

Experimental port for jailbroken PS4 consoles, built with [OpenOrbis PS4 Toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain).

## Building

Requirements: OpenOrbis toolchain (LLVM 18 build), `clang-18`, `lld-18`, `python3`, `curl`.
`PkgTool.Core` from the toolchain needs OpenSSL 1.1 (`libssl1.1`) on modern distributions.

```
export OO_PS4_TOOLCHAIN=/path/to/OpenOrbis/PS4Toolchain
scripts/gha/build_ps4.sh                    # artifacts/IV0000-XASH00001_00-XASH3DFWGSPS4000.pkg
PS4_IP=192.168.0.10 scripts/gha/build_ps4.sh # same, and upload to the console FTP (GoldHEN, port 2121)
```

`scripts/gha/deps_ps4.sh` is what CI does to get the toolchain and the game libraries.
opengnm is a git submodule (`3rdparty/opengnm/opengnm`), run
`git submodule update --init --recursive` after cloning.

[opengnm](https://github.com/PS4-OpenGNM/opengnm) is built into the engine for the GPU renderer.

Shaders of the GPU renderer are compiled ahead of time and kept in
`ref/gl/ps4/gnm_shaders.c`. To rebuild them after changing `ref/gl/ps4/shaders`, give
the host tools to configure, waf compiles the shaders with `scripts/ps4/gnm/compile_shaders.py`
instead of using the file from the repository:

```
scripts/ps4/psbc/build.sh          # host glslang and opengnm-psbc, prints their paths
./waf configure --ps4 --ps4-glslang=... --ps4-psbc=...
```

To update `gnm_shaders.c` in the repository, copy the file from `build/ref/gl/ps4/`.

## Running

* Game data goes to `/data/xash/valve` (or `/mnt/usbN/xash/valve` on USB storage).
* Game libraries built from [hlsdk-portable](https://github.com/FWGS/hlsdk-portable)
  (its `scripts/ps4/build.sh`) are put into the package by `build_ps4.sh`, it looks for them in
  `../hlsdk-portable/build/ps4/valve` (or `HLSDK_PS4_BUILD`), so build hlsdk-portable first.
  Libraries in `valve/cl_dlls` and `valve/dlls` on the console override packaged ones.
* `extras.pk3` is copied from the package to `/data/xash/valve` on start.
* Extra command line arguments are read from `/data/xash/xash3d.cmdline`. The file is created
  on the first start with `-log -console`; edit it to change arguments, for example
  `-language russian` mounts `valve_russian/` (put a localization pack there).
  `-log -dev 2` gives a verbose log, but developer messages are also shown in the top left
  corner of the screen.
* Output is written to `/data/xash/stdout.txt` and `/data/xash/engine.log`.

## Status

Half-Life runs: menu, gameplay, sound, DualShock 4, level changes, save/load, exit.

* GPU renderer by default: `gles3compat` (ref_gl with gl2_shim) on top of a small
  OpenGL ES 2 subset written over GNM, see below. 1920x1080, 60 FPS with vsync,
  a frame takes about 1-3 ms of CPU and 1-2 ms of GPU time.
* Software renderer with `-ref soft`: rendered at configured resolution
  (1280x720 by default) and scaled to the screen.
* DualShock 4 is read through libScePad directly, default layout is in
  `Key_SetDefaultGamepadBindings` (applied once over older configs, `resetkeys` restores it).
* USB keyboard and mouse are read through libSceKeyboard and libSceMouse (`engine/platform/ps4/kbm_ps4.c`),
  the keyboard layout is US. While a mouse is plugged in `m_ignore` is switched to 0 by itself. The system
  keyboard opens for the gamepad only, Cross opens it again if the USB keyboard was used.
* Sound goes through libSceAudioOut (`engine/platform/ps4/s_ps4.c`): the engine mixes at 44.1 kHz, a thread
  resamples it to 48 kHz, the only rate of the main audio port.
* Function names for savegames come from `<library>.prx.sym` files made by the build scripts of
  the game libraries (`llvm-nm` of the ELF image), the loader of the console has no symbol names.

## GPU renderer

PS4 has no OpenGL driver usable by homebrew: Piglet (system GLES2) can't compile shaders
at runtime. The GPU device lives in the engine and the renderer only draws, like with the
GL context or the buffers of the software renderer on other platforms:

* `engine/platform/ps4/gnm/gnm_dev.c` (engine): GPU memory, command buffers, submits, display
  and flips. Submits are synchronous, the GPU is far from being the bottleneck.
  `engine/platform/ps4/vid_gnm.c` opens it and hands it to the renderer as a table of functions.
* `ref/gl/ps4/gnm_gl.c` (part of ref_gles3compat): what gl2_shim needs from OpenGL ES 2
  over the device with opengnm. GL state, textures (linear, with mipmaps), buffers, draws, `glReadPixels`.
  gl2_shim programs are mapped to precompiled shader variants by their defines.
* Shaders are GLSL compiled with glslang and opengnm-psbc.

Things that were checked on the console (firmware 13.02) and are easy to break:

* Firmware embedded shaders of opengnm refer to old firmware addresses, own shaders are used.
* psbc passes the descriptor table address in one 32-bit user SGPR, tables are kept below 4 GB.
* psbc has no vertex fetch, vertex shaders read attributes from raw buffers by vertex index.
* Indexed draws use our own `DRAW_INDEX_2` packet, opengnm's goes through a firmware helper.
* Linear textures without mipmaps need rows aligned to 64 texels, mipmapped textures use
  opengnm's layout.

Command line options (`xash3d.cmdline`):

* `-gnmdebug [1|2|3]`: log submits and texture uploads, every draw, submit after every draw.
* `-gnmstats`: frame timing in the log every 300 frames.
* `-gnmmips 0|1|2`: no mipmaps, power of two textures only, all textures (default).
* `-gnmnoflip`: don't swap front face winding (only for debugging culling).

## Implementation notes

OpenOrbis toolchain needed several workarounds, all in `3rdparty/ps4compat` (MIT)
and linked into every image by `scripts/waifulib/ps4.py`:

* `ps4_crtlib.c` replaces toolchain `crtlib.o`, which never runs global constructors of modules.
* `ps4_malloc.c` replaces per-image musl heaps with one locked heap owned by `eboot.bin`,
  modules find it by a signature in its data segment.
* `ps4_cwd.c` emulates working directory (PS4 apps can't `chdir`, relative paths are rejected)
  and converts FreeBSD 9 `struct stat` returned by the kernel.
* Images must be linked with `ld.lld` of the same version as clang (18), newer lld picks
  musl's `lite_malloc`.

## Debugging

* `stdout.txt` survives crashes. Crashes print registers and stack as `module+offset`,
  resolve them with `scripts/ps4/symbolize.sh <console IP> [hlsdk build dir]`.
* `-ps4watchdog` in `xash3d.cmdline` dumps engine thread state if nothing is logged for 10 seconds.
* GPU hangs are detected (no submit finishes in 3 seconds): `GPU HANG` in the log names
  the last draw (with `-gnmdebug`), rendering stops but the game keeps running.
