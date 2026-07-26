# GameCube BSP Loading Optimization

This document describes the BSP (Binary Space Partitioning) loading optimization for Xash3D GameCube.

## Overview

BSP loading is a critical component of the GameCube port, as it determines how map data is loaded from storage into memory. The GameCube has limited main memory (MEM1) and requires careful memory management to ensure smooth map loading.

## Memory Architecture

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

## BSP Loading Process

### 1. Memory Preparation

Before loading a BSP map, the system prepares a contiguous memory buffer for the map data:

```c
void GC_PrepareMapLoadBuffer( size_t size );
void GC_PrepareMapLoadBufferForMap( const char *mapname );
```

This function:
- Allocates a contiguous buffer of the specified size (or default 3MB)
- Uses the GameCube's memory management system
- Ensures the buffer is aligned for optimal performance

### 2. Map Loading

The BSP map is loaded from storage into the prepared buffer:

```c
void *GC_BorrowMapLoadBuffer( size_t size );
qboolean GC_ReleaseMapLoadBuffer( void *ptr );
```

This function:
- Borrows the contiguous buffer from the memory pool
- Reads the BSP file from storage (SD card or ISO)
- Returns a pointer to the loaded data

### 3. Map Processing

The loaded BSP data is processed by the engine:

```c
void Mod_LoadBrushModel( model_t *mod, void *buffer, size_t buffersize, qboolean *loaded );
```

This function:
- Parses the BSP header and lumps
- Loads textures, models, and other assets
- Builds collision hulls and PVS (Potentially Visible Set)
- Creates the world model

### 4. Memory Cleanup

After the map is loaded and processed, the buffer is released:

```c
void GC_DiscardMapLoadBuffer( void );
```

This function:
- Releases the contiguous buffer back to the memory pool
- Frees any temporary data structures
- Optimizes memory for gameplay

## Optimization Techniques

### 1. Contiguous Buffer Allocation

The BSP loading system uses a contiguous buffer to avoid fragmentation and ensure efficient memory access. This is critical for the GameCube's memory architecture, where MEM1 is limited and must be managed carefully.

### 2. Memory Telemetry

The system includes telemetry to track memory usage during map loading:

```
Xash3D GameCube: mem stage=bsp load total=6.44 Mb delta=2.32 Mb hwm=6.44 Mb map=c0a0e
```

This telemetry helps developers:
- Monitor memory usage during map loading
- Identify memory leaks or inefficiencies
- Optimize memory allocation for different map sizes

### 3. Deferred Studio Loading

To optimize memory usage, studio models (characters, weapons) are loaded lazily:

```c
void Mod_GCLoadNewGameStudios( void );
void Mod_GCTryDeferredStudios( void );
```

This approach:
- Loads only essential models during map load
- Defers non-essential model loading until needed
- Reduces memory footprint during map transitions

### 4. Map Load Memory Optimization

The system includes a memory optimization mode for map loading:

```c
void GC_BeginMapLoadMemoryOpt( void );
void GC_EndMapLoadMemoryOpt( void );
qboolean GC_MapLoadMemoryOpt( void );
```

This mode:
- Trims client subsystems before map load
- Frees unused resources
- Optimizes memory for map loading

## BSP Loading Flow

```
┌─────────────────────────────────────────────────────────────┐
│ 1. GC_PrepareMapLoadBufferForMap(mapname)                   │
│    - Determine map file size                                │
│    - Allocate contiguous buffer (default 3MB)               │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ 2. GC_BorrowMapLoadBuffer(size)                             │
│    - Borrow buffer from memory pool                         │
│    - Return pointer to buffer                               │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ 3. FS_Open(mapfile) + FS_Read(buffer)                       │
│    - Open BSP file from storage                             │
│    - Read BSP data into buffer                              │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ 4. Mod_LoadBrushModel(mod, buffer, size, &loaded)           │
│    - Parse BSP header and lumps                             │
│    - Load textures, models, assets                          │
│    - Build collision hulls and PVS                          │
│    - Create world model                                     │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ 5. GC_DiscardMapLoadBuffer()                                │
│    - Release buffer to memory pool                          │
│    - Free temporary data structures                         │
│    - Optimize memory for gameplay                           │
└─────────────────────────────────────────────────────────────┘
```

## Testing BSP Loading

### Manual Testing

1. **Load a map using the console:**
   ```
   map c0a0
   map c0a0e
   map c1a1
   ```

2. **Monitor memory usage:**
   - Check console output for `mem stage=bsp load` messages
   - Verify memory usage is within budget
   - Check for any errors or warnings

3. **Test map transitions:**
   - Load multiple maps in sequence
   - Verify memory is properly released between maps
   - Check for memory leaks or fragmentation

### Automated Testing

The engine includes test infrastructure for BSP loading:

```c
#if XASH_ENGINE_TESTS
void Test_RunCommon( void );
#endif
```

To run tests:
```bash
./xash -runtests
```

## Performance Considerations

### Map Size Limits

The default BSP buffer size is 3MB, which covers most campaign maps:
- c0a0, c0a0e: ~1-2MB
- c1a1, c1a2: ~2-3MB
- c2a1, c2a2: ~3-4MB

For larger maps, the buffer size can be increased:
```c
#define GC_MAPLOAD_BUFFER_DEFAULT (3072u * 1024u)  // 3MB
```

### Memory Optimization

To optimize memory usage during map loading:

1. **Trim client subsystems:**
   ```c
   GC_TrimClientSubsystemsForMapLoad();
   ```

2. **Free unused resources:**
   ```c
   Mod_FreeUnused();
   ```

3. **Release video memory:**
   ```c
   R_GcmapTrimForMapLoad();
   GC_TrimVideoMemoryForMapLoad();
   ```

### Telemetry

Enable telemetry to monitor BSP loading:
```c
Cvar_Set( "gc_telemetry", "1" );
Cvar_Set( "gc_telemetry_format", "1" );  // Detailed format
```

## Troubleshooting

### Common Issues

1. **Map load fails with "out of memory" error:**
   - Increase buffer size in `GC_MAPLOAD_BUFFER_DEFAULT`
   - Trim client subsystems before map load
   - Check for memory leaks in other subsystems

2. **Map load is slow:**
   - Verify SD card is properly formatted (FAT32)
   - Check for file system fragmentation
   - Consider using ISO9660 for faster access

3. **Memory fragmentation:**
   - Use contiguous buffer allocation
   - Free resources in proper order
   - Monitor memory usage with telemetry

### Debugging

Enable debug output:
```c
Cvar_Set( "developer", "1" );
Cvar_Set( "gc_telemetry", "1" );
```

Check console output for:
- `Xash3D GameCube: map-load buffer ready <size>`
- `Xash3D GameCube: mem stage=bsp load ...`
- `Xash3D GameCube: map loaded <mapname>`

## References

- `engine/platform/gamecube/mem_gamecube.c` - BSP loading implementation
- `engine/common/model.c` - Model loading and BSP integration
- `engine/server/sv_cmds.c` - Server map loading commands
- `docs/GAMECUBE_MEMORY_BUDGET.md` - Memory budget planning
- `docs/GAMECUBE_PORT_PLAN.md` - Port development plan

## History

- **G421 (2026-07-26):** BSP loading optimization implemented
  - Contiguous buffer allocation for BSP maps
  - Memory telemetry for map loading
  - Deferred studio model loading
  - Map load memory optimization mode

- **G67 (earlier):** Native GoldSrc BSP format compatibility
  - BSP, WAD, PAK format support
  - Map loading from ISO9660 and SD card