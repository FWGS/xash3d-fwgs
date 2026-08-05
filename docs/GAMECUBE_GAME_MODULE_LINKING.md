# GameCube Game Module Linking

This document describes the game module linking system for Xash3D on GameCube.

## Overview

The GameCube port uses a modular linking system to combine the Xash3D engine with game-specific code (e.g., Half-Life, Opposing Force, Day of Defeat).

## Linking Architecture

### Module Types

#### 1. Engine Module (`engine/`)
- Core engine functionality
- Platform-independent code
- Common utilities

#### 2. Game Module (`game/`)
- Game-specific logic
- Entity definitions
- Game rules

#### 3. Client Module (`client/`)
- Client-side rendering
- HUD system
- Input handling

#### 4. Server Module (`server/`)
- Server-side logic
- Entity management
- Network code

### Linking Strategy

The GameCube port uses **static linking** for all modules to create a single executable.

```
Linking Process:
├─ Compile modules (engine, game, client, server)
├─ Link with engine libraries
├─ Generate ELF binary
└─ Convert to DOL format
```

## Linker Script

### GameCube Linker Script

```ld
/* GameCube linker script */
OUTPUT_FORMAT(elf32-powerpc, elf32-powerpc, elf32-powerpc)
OUTPUT_ARCH(powerpc:common)

/* Memory map */
MEMORY {
    /* Main application memory */
    APP (rwx) : ORIGIN = 0x80001000, LENGTH = 20M
    
    /* Stack memory */
    STACK (rwx) : ORIGIN = 0x80200000, LENGTH = 2M
    
    /* Heap memory */
    HEAP (rwx) : ORIGIN = 0x80400000, LENGTH = 4M
    
    /* Texture cache */
    TEXCACHE (rwx) : ORIGIN = 0x80800000, LENGTH = 8M
}

/* Section definitions */
SECTIONS {
    /* Text section (code) */
    .text : {
        _stext = .;
        *(.vector)
        *(.text)
        *(.text.*)
        *(.rodata)
        _etext = .;
    } > APP

    /* Data section */
    .data : {
        _sdata = .;
        *(.data)
        *(.data.*)
        _edata = .;
    } > APP

    /* BSS section */
    .bss : {
        _sbss = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        _ebss = .;
    } > APP

    /* Stack section */
    .stack : {
        _stack_start = .;
        . += 0x100000;  /* 1MB stack */
        _stack_end = .;
    } > STACK

    /* Heap section */
    .heap : {
        _heap_start = .;
        . += 0x400000;  /* 4MB heap */
        _heap_end = .;
    } > HEAP
}
```

## Module Linking Process

### 1. Module Compilation

Each module is compiled separately:

```bash
# Compile engine module
powerpc-gekko-gcc -c engine/*.c -o engine.o

# Compile game module
powerpc-gekko-gcc -c game/*.c -o game.o

# Compile client module
powerpc-gekko-gcc -c client/*.c -o client.o

# Compile server module
powerpc-gekko-gcc -c server/*.c -o server.o
```

### 2. Module Linking

All modules are linked together:

```bash
# Link all modules
powerpc-gekko-gcc -o xash.elf \
    engine.o \
    game.o \
    client.o \
    server.o \
    -T linker_script.ld
```

### 3. DOL Generation

Convert ELF to DOL:

```bash
# Generate DOL
powerpc-gekko-elf2dol xash.elf boot.dol
```

## Module Dependencies

### Engine Dependencies

- **Math Library**: libm
- **Standard Library**: libc
- **Graphics Library**: libogc2 GX (Swiss) / classic libogc GX fallback
- **Audio Library**: libogc ASND (`-lasnd`; libansnd optional later)
- **File System**: libdvm FAT provider (`-lfat` API) preferred; classic libfat fallback

### Game Dependencies

- **Engine**: Required for all game functionality
- **Client**: Required for client-side game logic
- **Server**: Required for server-side game logic

### Client Dependencies

- **Engine**: Required for rendering
- **Game**: Required for game-specific rendering
- **Graphics**: Required for GX rendering

### Server Dependencies

- **Engine**: Required for server logic
- **Game**: Required for game-specific logic
- **Network**: Required for network code

## Module Integration

### Engine Integration

The engine provides core functionality to all modules:

```c
// Engine API
typedef struct {
    // Initialization
    void (*Init)(void);
    void (*Shutdown)(void);
    
    // Frame processing
    void (*Frame)(float time);
    
    // Entity management
    void (*SpawnEntity)(edict_t* ent);
    void (*RemoveEntity)(edict_t* ent);
    
    // Rendering
    void (*RenderScene)(void);
    void (*RenderHUD)(void);
    
    // Audio
    void (*PlaySound)(const char* sample, vec3_t origin);
    
    // Network
    void (*SendPacket)(packet_t* packet);
    void (*ReceivePacket)(packet_t* packet);
} engine_api_t;
```

### Game Integration

The game module integrates with the engine:

```c
// Game API
typedef struct {
    // Entity management
    void (*Spawn)(edict_t* ent);
    void (*Think)(edict_t* ent);
    void (*Touch)(edict_t* ent, edict_t* other);
    void (*Blocked)(edict_t* ent, edict_t* other);
    
    // Combat
    void (*Damage)(edict_t* target, edict_t* inflictor, edict_t* attacker, float damage, int means_of_death);
    void (*Killed)(edict_t* target, edict_t* inflictor, edict_t* attacker);
    
    // Game rules
    void (*SpawnServer)(void);
    void (*SpawnClient)(void);
    void (*ClientConnect)(edict_t* ent);
    void (*ClientDisconnect)(edict_t* ent);
} game_api_t;
```

### Client Integration

The client module integrates with the engine and game:

```c
// Client API
typedef struct {
    // Rendering
    void (*RenderScene)(void);
    void (*RenderHUD)(void);
    void (*RenderView)(void);
    
    // Input
    void (*HandleInput)(void);
    void (*ProcessCommand)(const char* cmd);
    
    // Audio
    void (*PlayLocalSound)(const char* sample);
    void (*UpdateAudio)(void);
} client_api_t;
```

### Server Integration

The server module integrates with the engine and game:

```c
// Server API
typedef struct {
    // Network
    void (*SendPacket)(packet_t* packet);
    void (*ReceivePacket)(packet_t* packet);
    
    // Entity management
    void (*SpawnEntity)(edict_t* ent);
    void (*RemoveEntity)(edict_t* ent);
    
    // Game rules
    void (*SpawnServer)(void);
    void (*SpawnClient)(edict_t* ent);
    void (*ClientConnect)(edict_t* ent);
    void (*ClientDisconnect)(edict_t* ent);
} server_api_t;
```

## Module Configuration

### Build Configuration

```c
// Module configuration
#define ENGINE_MODULE
#define GAME_MODULE
#define CLIENT_MODULE
#define SERVER_MODULE

// Module paths
#define ENGINE_PATH "engine/"
#define GAME_PATH "game/"
#define CLIENT_PATH "client/"
#define SERVER_PATH "server/"
```

### Module Selection

```c
// Select game module
#ifdef HL1
    #define GAME_MODULE_PATH "game/hl1/"
#elif defined(HL2)
    #define GAME_MODULE_PATH "game/hl2/"
#elif defined(HLX)
    #define GAME_MODULE_PATH "game/hlx/"
#endif
```

## Memory Management

### Module Memory Layout

```
Memory Layout:
├─ Engine Module (8MB)
│  ├─ Core Engine (4MB)
│  ├─ Rendering (2MB)
│  └─ Audio (2MB)
├─ Game Module (4MB)
│  ├─ Game Logic (2MB)
│  └─ Entity Data (2MB)
├─ Client Module (4MB)
│  ├─ Rendering (2MB)
│  └─ HUD (2MB)
└─ Server Module (4MB)
   ├─ Network (2MB)
   └─ Entity Management (2MB)
```

### Memory Pool Allocation

```c
// Memory pool configuration
typedef struct {
    void* pool;           // Memory pool
    int size;             // Pool size
    int used;             // Used memory
    int max_used;         // Maximum used
} MemoryPool;

// Pool allocation
void* Mem_Alloc(MemoryPool* pool, int size);
void Mem_Free(MemoryPool* pool, void* ptr);
```

## Debugging

### Module Debugging

1. **Symbol Files**: Generate symbol files for each module
2. **Link Map**: Generate link map for memory layout
3. **Debug Build**: Enable debug symbols

### Common Issues

#### Linker Errors
- Check module dependencies
- Verify symbol definitions
- Ensure proper module order

#### Memory Issues
- Check memory pool sizes
- Verify module memory usage
- Optimize memory allocation

#### Runtime Issues
- Check module initialization order
- Verify module dependencies
- Debug module-specific issues

## Build Scripts

### Build Script

```bash
#!/bin/bash
# Build GameCube module

# Clean
rm -rf build
mkdir build
cd build

# Configure
cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchains/powerpc-gekko.cmake \
      -DGAMECUBE=ON \
      -DCMAKE_BUILD_TYPE=Release \
      ..

# Build
make -j$(nproc)

# Generate DOL
powerpc-gekko-elf2dol xash.elf boot.dol

# Copy to output
cp boot.dol ../output/
```

### Module Build Script

```bash
#!/bin/bash
# Build specific module

# Compile module
powerpc-gekko-gcc -c $1/*.c -o build/$1.o

# Link module
powerpc-gekko-gcc -o build/$1.elf \
    build/engine.o \
    build/$1.o \
    -T linker_script.ld

# Generate DOL
powerpc-gekko-elf2dol build/$1.elf build/$1.dol
```

## Testing

### Module Testing

1. **Unit Tests**: Test individual modules
2. **Integration Tests**: Test module interactions
3. **Performance Tests**: Test module performance
4. **Memory Tests**: Test memory usage

### Test Checklist

- [ ] Engine module loads correctly
- [ ] Game module loads correctly
- [ ] Client module loads correctly
- [ ] Server module loads correctly
- [ ] All modules interact correctly
- [ ] Memory usage is acceptable
- [ ] Performance is acceptable
- [ ] No memory leaks detected

## Future Enhancements

### Planned Features

1. **Dynamic Loading**: Support dynamic module loading
2. **Module Updates**: Support module updates
3. **Module Dependencies**: Support module dependencies
4. **Module Configuration**: Support module configuration

### Roadmap

- **Phase 1**: Static linking (completed)
- **Phase 2**: Dynamic module loading
- **Phase 3**: Module updates
- **Phase 4**: Module dependencies
- **Phase 5**: Module configuration