## PlayStation 4 port

Experimental port for jailbroken PS4 consoles, built with [OpenOrbis PS4 Toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain).

### Building

Requirements: OpenOrbis toolchain (LLVM 18 build), `clang-18`, `lld-18`, `python3` with `mako` and `yaml`
modules, C and C++ compilers for the build machine.
`PkgTool.Core` from the toolchain makes the package and needs OpenSSL 1.1 (`libssl1.1`), without it
only the signed images are built.

```
export OO_PS4_TOOLCHAIN=/path/to/OpenOrbis/PS4Toolchain
./waf configure -T release --ps4
./waf build # build/engine/IV0000-XASH00001_00-XASH3DFWGSPS4000.pkg
```

Game libraries built from [hlsdk-portable](https://github.com/FWGS/hlsdk-portable) are put into the
package, configure looks for them in `../hlsdk-portable/build/ps4/valve` (where its
`./waf install --destdir=build/ps4` puts them), `--ps4-gamelibs` selects another directory.

The package can be uploaded to the console with GoldHEN FTP, e.g.
`curl -T build/engine/IV0000-XASH00001_00-XASH3DFWGSPS4000.pkg ftp://<console IP>:2121/data/pkg/`.

`scripts/gha/deps_ps4.sh` is what CI does to get the toolchain and the game libraries.

Submodules used only by the PS4 port aren't checked out by default:

```
git submodule update --init --checkout --recursive 3rdparty/opengnm/opengnm 3rdparty/opengnm-psbc 3rdparty/glslang
```

[opengnm](https://github.com/PS4-OpenGNM/opengnm) is built into the engine for the GPU renderer.

Shaders of the GPU renderer (`ref/gl/gnm_shim/shaders`) are compiled during the build with glslang and
[opengnm-psbc](https://github.com/PS4-OpenGNM/opengnm-psbc), both are built from the submodules for the
build machine. `--host-cc` and `--host-cxx` configure options select its compilers.

### Running

* Game data goes to `/data/xash/valve` (or `/mnt/usbN/xash/valve` on USB storage).
* Libraries in `valve/cl_dlls` and `valve/dlls` on the console override packaged ones.
* `extras.pk3` is opened directly from the package.
* Extra command line arguments are read from `/data/xash/xash3d.cmdline`. The file is created
  on the first start with `-log -console`; edit it to change arguments, for example
  `-language russian` mounts `valve_russian/` (put a localization pack there).
  `-log -dev 2` gives a verbose log, but developer messages are also shown in the top left
  corner of the screen.
* Output is written to `/data/xash/stdout.txt` and `/data/xash/engine.log`.

### Status

Half-Life runs: menu, gameplay, sound, DualShock 4, level changes, save/load, exit.

* Renderer: `gl` (ref_gl with gl2_shim) on top of a small
  OpenGL ES 2 subset written over GNM, see below. 1920x1080, 60 FPS with vsync,
  a frame takes about 1-3 ms of CPU and 1-2 ms of GPU time.
* DualShock 4 is read through libScePad directly, default layout is in
  `Key_SetDefaultGamepadBindings` (applied once over older configs, `resetkeys` restores it).
* USB keyboard and mouse are read through libSceKeyboard and libSceMouse (`engine/platform/ps4/kbm_ps4.c`),
  the keyboard layout is US. Servers are told about the mouse only while one is plugged in. The system
  keyboard opens for the gamepad only, Cross opens it again if the USB keyboard was used.
* Sound goes through libSceAudioOut (`engine/platform/ps4/s_ps4.c`): the engine mixes at 44.1 kHz, a thread
  resamples it to 48 kHz, the only rate of the main audio port.
* Function names for savegames come from `<library>.prx.sym` files made by the build scripts of
  the game libraries (`llvm-nm` of the ELF image), the loader of the console has no symbol names.

### GPU renderer

PS4 has no OpenGL driver usable by homebrew: Piglet (system GLES2) can't compile shaders
at runtime. The GPU device lives in the engine and the renderer only draws, like with the
GL context or the buffers of the software renderer on other platforms:

* `engine/platform/ps4/gnm/gnm_dev.c` (engine): GPU memory, command buffers, submits, display
  and flips. Submits are synchronous, the GPU is far from being the bottleneck.
  `engine/platform/ps4/vid_gnm.c` opens it, the renderer gets it as a table of functions
  with `R_GetWindowHandle( REF_WINDOW_TYPE_GNM )`.
* `ref/gl/gnm_shim/gnm_shim.c` (part of ref_gl, built from `ref/gl/gnm_shim/wscript`): what gl2_shim needs from OpenGL ES 2
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

### Implementation notes

OpenOrbis toolchain needed several workarounds, all in `3rdparty/ps4compat` (MIT)
and linked into every image by `scripts/waifulib/ps4.py`:

* `ps4_crtlib.c` replaces toolchain `crtlib.o`, which never runs global constructors of modules.
* `ps4_malloc.c` replaces per-image musl heaps with one locked heap owned by `eboot.bin`,
  modules find it by a signature in its data segment.
* `ps4_cwd.c` emulates working directory (PS4 apps can't `chdir`, relative paths are rejected)
  and converts FreeBSD 9 `struct stat` returned by the kernel.
* Images must be linked with `ld.lld` of the same version as clang (18), newer lld picks
  musl's `lite_malloc`.

### Debugging

* `stdout.txt` survives crashes. Crashes print registers and stack as `module+offset`,
  resolve them with `scripts/ps4/symbolize.sh <console IP> [hlsdk build dir]`.
* `-ps4watchdog` in `xash3d.cmdline` dumps engine thread state if nothing is logged for 10 seconds.
* GPU hangs are detected (no submit finishes in 3 seconds): `GPU HANG` in the log names
  the last draw (with `-gnmdebug`), rendering stops but the game keeps running.
