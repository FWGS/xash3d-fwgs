# GameCube Memory Budget

This document describes the memory budget and allocation strategy for Xash3D on GameCube.

## Overview

The GameCube has **24 MB of main RAM** and **32 MB of VRAM**. This document details how memory is allocated and managed.

## Memory Architecture

### GameCube Memory Map

```
GameCube Memory Map (24 MB Main RAM)
├─ Exception Vectors (256 bytes)
├─ Application Memory (20 MB)
│  ├─ Text Section (code)
│  ├─ Data Section (initialized data)
│  └─ BSS Section (uninitialized data)
├─ Stack (2 MB)
├─ Heap (4 MB)
└─ Texture Cache (8 MB)

GameCube Memory Map (32 MB VRAM)
├─ Framebuffer (2 MB)
├─ Texture Memory (24 MB)
└─ Vertex Buffer Memory (6 MB)
```

### Memory Constraints

- **Main RAM**: 24 MB total
- **VRAM**: 32 MB total
- **Texture Cache**: 8 MB
- **Stack**: 1-2 MB
- **Heap**: 4 MB

## Memory Budget Allocation

### Main RAM Budget

```
Main RAM Budget (24 MB)
├─ Engine (8 MB)
│  ├─ Core Engine (4 MB)
│  ├─ Rendering (2 MB)
│  └─ Audio (2 MB)
├─ Game Module (4 MB)
│  ├─ Game Logic (2 MB)
│  └─ Entity Data (2 MB)
├─ Client Module (4 MB)
│  ├─ Rendering (2 MB)
│  └─ HUD (2 MB)
├─ Server Module (4 MB)
│  ├─ Network (2 MB)
│  └─ Entity Management (2 MB)
├─ Texture Cache (4 MB)
└─ Stack/Heap (2 MB)
```

### VRAM Budget

```
VRAM Budget (32 MB)
├─ Framebuffer (2 MB)
│  ├─ Color Buffer (1 MB)
│  └─ Depth Buffer (1 MB)
├─ Texture Memory (24 MB)
│  ├─ Main Textures (16 MB)
│  ├─ Lightmaps (4 MB)
│  └─ UI Textures (4 MB)
└─ Vertex Buffer Memory (6 MB)
```

## Memory Allocation Strategy

### Memory Pools

#### 1. Main Pool (16 MB)
- Game data
- Entity data
- World data
- Model data

#### 2. Texture Pool (4 MB)
- Texture data
- Mipmaps
- Texture cache

#### 3. Audio Pool (2 MB)
- Sound data
- Audio buffers
- Music data

#### 4. Stack Pool (1 MB)
- Call stacks
- Local variables
- Function parameters

#### 5. Heap Pool (1 MB)
- Dynamic allocation
- Temporary data
- Temporary buffers

### Memory Pool Implementation

```c
// Memory pool structure
typedef struct {
    void* pool;           // Memory pool
    int size;             // Pool size
    int used;             // Used memory
    int max_used;         // Maximum used
    int alignment;        // Alignment
    int flags;            // Flags
} MemoryPool;

// Memory pool functions
void* Mem_Alloc(MemoryPool* pool, int size);
void Mem_Free(MemoryPool* pool, void* ptr);
void Mem_Init(MemoryPool* pool, void* data, int size);
void Mem_Shutdown(MemoryPool* pool);
```

### Memory Pool Configuration

```c
// Memory pool configuration
#define POOL_MAIN_SIZE      (16 * 1024 * 1024)  // 16 MB
#define POOL_TEXTURE_SIZE   (4 * 1024 * 1024)   // 4 MB
#define POOL_AUDIO_SIZE     (2 * 1024 * 1024)   // 2 MB
#define POOL_STACK_SIZE     (1 * 1024 * 1024)   // 1 MB
#define POOL_HEAP_SIZE      (1 * 1024 * 1024)   // 1 MB

// Memory pool allocation
MemoryPool main_pool;
MemoryPool texture_pool;
MemoryPool audio_pool;
MemoryPool stack_pool;
MemoryPool heap_pool;
```

## Memory Usage Analysis

### Engine Memory Usage

#### Core Engine (4 MB)
- Entity system: 1 MB
- World system: 1 MB
- File system: 0.5 MB
- Network system: 0.5 MB
- Utility functions: 1 MB

#### Rendering (2 MB)
- Vertex buffers: 0.5 MB
- Texture cache: 1 MB
- Render targets: 0.5 MB

#### Audio (2 MB)
- Sound buffers: 1 MB
- Music buffers: 0.5 MB
- Audio system: 0.5 MB

### Game Module Memory Usage

#### Game Logic (2 MB)
- Entity definitions: 1 MB
- Game rules: 0.5 MB
- Game state: 0.5 MB

#### Entity Data (2 MB)
- Entity instances: 1 MB
- Entity data: 0.5 MB
- Entity cache: 0.5 MB

### Client Module Memory Usage

#### Rendering (2 MB)
- Scene graph: 0.5 MB
- View model: 0.5 MB
- HUD: 1 MB

#### HUD (2 MB)
- HUD elements: 1 MB
- HUD data: 0.5 MB
- HUD cache: 0.5 MB

### Server Module Memory Usage

#### Network (2 MB)
- Network buffers: 1 MB
- Network state: 0.5 MB
- Network cache: 0.5 MB

#### Entity Management (2 MB)
- Entity list: 1 MB
- Entity cache: 0.5 MB
- Entity data: 0.5 MB

## Memory Optimization

### Texture Optimization

#### 1. Texture Compression
- Use DXT1 compression (8:1)
- Use DXT3 compression (4:1)
- Use DXT5 compression (4:1)

#### 2. Mipmapping
- Generate mipmaps for all textures
- Use smaller mipmaps for distant objects

#### 3. Texture Atlasing
- Combine multiple textures into one
- Reduce texture switches

### Model Optimization

#### 1. Vertex Optimization
- Use shared vertices
- Use index buffers
- Use vertex caching

#### 2. Geometry Optimization
- Use LOD (Level of Detail)
- Use culling
- Use occlusion culling

#### 3. Animation Optimization
- Use skinning
- Use animation caching
- Use animation compression

### Audio Optimization

#### 1. Audio Compression
- Use OGG Vorbis compression
- Use 16-bit audio
- Use stereo audio

#### 2. Audio Caching
- Cache audio data
- Use streaming for large files
- Use preloading for small files

### Network Optimization

#### 1. Packet Optimization
- Use compression
- Use delta compression
- Use prediction

#### 2. Buffer Optimization
- Use fixed-size buffers
- Use buffer pooling
- Use buffer reuse

## Memory Monitoring

### Memory Statistics

```c
// Memory statistics
typedef struct {
    int total_memory;     // Total memory
    int used_memory;      // Used memory
    int free_memory;      // Free memory
    int max_used;         // Maximum used
    int allocations;      // Number of allocations
    int deallocations;    // Number of deallocations
} MemoryStats;

// Memory statistics functions
void Mem_GetStats(MemoryStats* stats);
void Mem_PrintStats(void);
```

### Memory Profiling

```c
// Memory profiling
typedef struct {
    void* address;        // Memory address
    int size;             // Size
    const char* file;     // File
    int line;             // Line
    const char* function; // Function
} MemoryProfile;

// Memory profiling functions
void Mem_Profile(void);
void Mem_PrintProfile(void);
```

## Memory Issues

### Common Issues

#### 1. Memory Leaks
- Use memory tracking
- Use memory pools
- Use smart pointers

#### 2. Memory Fragmentation
- Use memory pools
- Use fixed-size allocations
- Use memory compaction

#### 3. Memory Overflows
- Use bounds checking
- Use memory guards
- Use memory validation

### Debugging

#### 1. Memory Debugging
- Enable memory debugging
- Use memory guards
- Use memory validation

#### 2. Memory Profiling
- Profile memory usage
- Identify memory hotspots
- Optimize memory usage

## Memory Future Enhancements

### Planned Features

1. **Dynamic Memory**: Dynamic memory allocation
2. **Memory Compression**: Memory compression
3. **Memory Pooling**: Memory pooling
4. **Memory Tracking**: Memory tracking

### Roadmap

- **Phase 1**: Static memory allocation (completed)
- **Phase 2**: Dynamic memory allocation
- **Phase 3**: Memory compression
- **Phase 4**: Memory pooling
- **Phase 5**: Memory tracking