# Building for GameCube

This document describes the complete build process for creating GameCube-compatible binaries.

## Prerequisites

### Required Tools
- **GCC for PowerPC**: Cross-compiler targeting PowerPC 750 (GC/Wii CPU)
- **DevKitPro**: Development kit for Nintendo GameCube/Wii
- **CMake**: Build system generator
- **Python 3**: Build scripts and tools
- **Wii Toolchain**: For DOL file generation

### System Requirements
- Linux or macOS (Windows with WSL2 recommended)
- 8GB+ RAM (build memory intensive)
- 20GB+ free disk space
- Internet connection for dependencies

## Build Environment Setup

### 1. Install DevKitPPC

```bash
# Clone DevKitPPC
git clone https://github.com/devkitPro/installer.git
cd installer
./install.sh devkitPPC

# Add to PATH
export DEVKITPPC=$DEVKITPRO/devkitPPC
export PATH=$DEVKITPPC/bin:$PATH
```

### 2. Install Required Libraries

```bash
# libogc (GameCube OS)
# libfat (FAT filesystem)
# libsd (SD card support)
# libwiisocket (Network)
# libmikmod (Music)
# libmodplug (Music)
# freetype (Font rendering)
# zlib (Compression)
# png (PNG support)
# jpeg (JPEG support)
```

### 3. Configure Build Environment

```bash
# Set environment variables
export DEVKITPPC=/opt/devkitPro/devkitPPC
export DEVKITPRO=/opt/devkitPro
export PATH=$DEVKITPPC/bin:$PATH

# Verify toolchain
powerpc-gekko-gcc --version
powerpc-gekko-ld --version
```

## Building Xash3D for GameCube

### Method 1: Using CMake (Recommended)

```bash
# Clone repository
git clone https://github.com/salvadelfia/xash3d.git
cd xash3d

# Configure for GameCube
mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchains/powerpc-gekko.cmake \
      -DGAMECUBE=ON \
      -DCMAKE_BUILD_TYPE=Release \
      ..

# Build
make -j$(nproc)
```

### Method 2: Using Waf (Legacy)

```bash
# Configure
./waf configure --toolchain=powerpc-gekko --gamecube

# Build
./waf build
```

## Build Configuration Options

### Core Options
- `GAMECUBE=ON`: Enable GameCube target
- `GC_DEBUG=ON`: Enable debug symbols
- `GC_OPTIMIZE=ON`: Enable optimizations (default)
- `GC_SSE=OFF`: Disable SSE (not available on GC)

### Audio Options
- `GC_AUDIO=ON`: Enable audio system
- `GC_OGG=ON`: Enable OGG Vorbis support
- `GC_MP3=OFF`: Disable MP3 (patent issues)

### Graphics Options
- `GC_GX=ON`: Enable GX renderer (default)
- `GC_GX_DEBUG=OFF`: Disable GX debug output
- `GC_VSYNC=ON`: Enable vertical sync

### Memory Options
- `GC_MEM_POOL=ON`: Enable memory pool allocator
- `GC_MEM_DEBUG=OFF`: Disable memory debugging
- `GC_MEM_SIZE=24`: Main RAM pool size (MB)

## Output Files

### Primary Outputs
- **xash**: ELF binary (debug symbols)
- **xash.elf**: Raw ELF for loading via loader
- **boot.dol**: GameCube executable (DOL format)

### Secondary Outputs
- **xash.map**: Linker map file
- **xash.sym**: Symbol table
- **xash.dbg**: Debug information

## DOL File Generation

### Using elf2dol

```bash
# Convert ELF to DOL
powerpc-gekko-elf2dol xash.elf boot.dol

# Verify DOL
powerpc-gekko-objdump -h boot.dol
```

### DOL Structure
```
DOL Header (0x100 bytes)
- Text section offsets
- Data section offsets
- BSS section info
- Entry point
- Section sizes
```

## Memory Layout

### GameCube Memory Map
```
0x80000000 - 0x800000FF:  Exception vectors (256 bytes)
0x80000100 - 0x8000013F:  Reset handler
0x80000140 - 0x8000016F:  Machine check handler
0x80000170 - 0x8000019F:  DSI handler
0x800001A0 - 0x800001CF:  ISI handler
0x800001D0 - 0x800001FF:  EAI handler

0x80001000 - 0x801FFFFF:  Main application (20MB)
0x80200000 - 0x803FFFFF:  Stack (2MB)
0x80400000 - 0x807FFFFF:  Heap (4MB)
0x80800000 - 0x80FFFFFF:  Texture cache (8MB)
```

### Xash3D Memory Allocation
```
Main Pool:     16MB  - Game data, entities, world
Texture Cache: 4MB   - Textures, sprites
Audio Buffer:  2MB   - Sound data
Stack:         1MB   - Call stacks
Heap:          1MB   - Dynamic allocation
```

## Debugging

### Using Dolphin Emulator

```bash
# Launch with debug
dolphin-emu -b -e boot.dol --gdb

# Or with GDB
powerpc-gekko-gdb xash.elf
(gdb) target remote tcp:127.0.0.1:4444
```

### Using Swiss DOL Loader

1. Copy `boot.dol` to SD card root
2. Insert SD card into GameCube
3. Launch Swiss
4. Select `boot.dol` from SD card

### Debug Output
- **Console**: UART via GameCube debug port
- **Network**: GDB stub over Ethernet
- **Memory**: Debug memory viewer

## Common Issues

### Build Errors

#### "undefined reference to _start"
- Ensure proper linker script
- Check entry point definition

#### "section '...' will not fit in region '...'"
- Reduce memory usage
- Optimize data structures
- Increase memory pool sizes

#### "relocation truncated to fit"
- Use -mrelocatable for large code
- Split into multiple sections

### Runtime Issues

#### Crashes on startup
- Check memory alignment
- Verify stack size
- Validate DOL header

#### Audio issues
- Check audio buffer sizes
- Verify sample rate
- Check DMA configuration

#### Graphics issues
- Verify texture dimensions (power of 2)
- Check texture cache alignment
- Validate GX setup

## Optimization Tips

### Code Optimization
```bash
# Compiler flags
-O3 -ffast-math -fomit-frame-pointer
-mcpu=750 -mhard-float -maltivec
```

### Memory Optimization
- Use fixed-size data structures
- Pre-allocate memory pools
- Avoid dynamic allocation in hot paths

### Texture Optimization
- Use DXT compression
- Mipmapping for distance
- Power-of-2 dimensions

## Testing Checklist

- [ ] DOL loads without errors
- [ ] Main menu displays
- [ ] World renders correctly
- [ ] Player movement works
- [ ] Audio plays without artifacts
- [ ] No memory leaks detected
- [ ] Frame rate stable at 60 FPS
- [ ] Save/load works correctly

## Distribution

### SD Card Structure
```
/ games/
    xash3d/
        boot.dol
        config.cfg
        /models/
        /sound/
        /materials/
```

### ISO Distribution
```
ISO Root
- boot.dol (at correct offset)
- system menu
- GameCube files
```

## Next Steps

After successful build:
1. Test in Dolphin emulator
2. Deploy to GameCube via Swiss
3. Verify all features work
4. Optimize performance
5. Create release build