# Xash3D GameCube Stub Modules Inventory

## Overview

This document provides a comprehensive inventory of stub/placeholder modules used in the Xash3D GameCube port to enable successful builds while maintaining compatibility with the original Half-Life engine architecture.

## Current Stub Modules

### 1. Server Stub Module

**Location:** `stub/server/`

**Files:**
- `server_stub.c` - Main server stub implementation (4,248 lines)
- `server_export.c` - Export registration for GameCube server DLLs
- `wscript` - Build configuration (minimal, no actual build)

**Purpose:**
- Provides minimal server DLL implementation for GameCube embedding
- Implements core enginefuncs_t callbacks with stub behavior
- Handles entity spawning, key-value parsing, and client management
- Uses `GiveFnptrsToDll`, `GetEntityAPI`, `GetEntityAPI2` exports

**Key Functions:**
- `Stub_PM_Init()` / `Stub_PM_Move()` - Player movement initialization
- `Stub_Spawn()` - Entity spawning with class-based defaults
- `Stub_KeyValue()` - Key-value pair parsing for entity setup
- `Stub_ClientConnect()` / `Stub_ClientPutInServer()` - Client management
- `Stub_AddToFullPack()` / `Stub_CreateBaseline()` - Network state management

**Build Status:**
- No actual build output (embedded into engine binary)
- WAF build script present but empty build() function

---

### 2. Client Stub Module

**Location:** `stub/client/`

**Files:**
- `client_stub.c` - Main client stub implementation (141 lines)
- `client_export.c` - Export registration for GameCube client DLLs
- `wscript` - Build configuration for client stub library

**Purpose:**
- Provides minimal client DLL implementation for GameCube
- Implements cldll_func_t callback structure
- Handles client-side rendering, input, and prediction

**Key Functions:**
- `Stub_Initialize()` - Client initialization
- `Stub_CreateMove()` - Input command generation
- `Stub_UpdateClientData()` - Client data updates
- `Stub_AddEntity()` / `Stub_CreateEntities()` - Entity rendering
- `Stub_Frame()` - Frame processing
- `Stub_Key_Event()` - Input event handling

**Build Status:**
- Builds as static library `libclient_stub.a`
- Target: `libogc` (devkitPPC)
- Install path: `bld.env.LIBDIR`

---

### 3. PM Shared Module

**Location:** `stub/pm_shared/`

**Files:**
- `pm_shared.c` - Player movement shared code (3,447 lines, 83KB)
- `pm_math.c` - Math primitives (388 lines)
- `pm_debug.c` - Debug visualization (327 lines)
- `mathlib.h` - Math library headers
- `pm_debug.h` - Debug headers
- `pm_materials.h` - Texture materials
- `pm_model.h` - Model definitions
- `pm_movevars.h` - Movement variables
- `pm_shared.h` - Main headers
- `pmtrace.h` - Trace definitions
- `usercmd.h` - User command definitions

**Purpose:**
- Provides player movement logic shared between client and server
- Implements physics calculations, collision detection, and movement prediction
- Contains core Half-Life player movement algorithm

**Key Functions:**
- `PM_Init()` - Movement system initialization
- `PM_Move()` - Main movement calculation
- `PM_FindTextureType()` - Surface texture type detection
- Physics primitives (vector math, angle calculations)
- Collision detection and resolution

**Build Status:**
- No wscript (not built as separate module)
- Compiled into engine as part of pm_shared implementation
- No stub-specific build configuration

---

## Module Dependencies

```
stub/server/
├── server_stub.c (depends on: common.h, server.h, pm_shared.h)
└── server_export.c (depends on: common.h, gamecube/dll_gamecube.h)

stub/client/
├── client_stub.c (depends on: common.h, client.h, build.h, pm_shared.h)
└── client_export.c (depends on: common.h, gamecube/dll_gamecube.h)

stub/pm_shared/
├── pm_shared.c (depends on: mathlib.h, const.h, pm_defs.h, pm_movevars.h, pm_debug.h)
├── pm_math.c (depends on: mathlib.h, const.h)
└── pm_debug.c (depends on: mathlib.h, const.h, pmtrace.h, pm_model.h)
```

## Build Integration

### Server Stub
- Embedded directly into engine binary
- No separate library build
- Linked via `setup_gamecube_server_exports()` in `server_export.c`

### Client Stub
- Built as static library: `libclient_stub.a`
- Uses WAF build system
- Target: `libogc` (devkitPPC)
- Includes client bridge functions for GameCube-specific initialization

### PM Shared
- Compiled as part of engine source
- No separate stub module
- Provides shared player movement logic

## Verification

### Build Status
- **boot.dol**: 5,823,404 bytes (SHA256: bdf3f7c5...)
- **xash ELF**: 33,122,656 bytes
- Build completes successfully with all stub modules

### Functionality
- Server stub provides minimal entity management
- Client stub provides basic rendering and input
- PM shared provides player movement physics
- All modules compile without errors or warnings

## Next Steps

1. **G401**: Generate verified final-ELF module linkage matrix
2. **G402**: Build real menu implementation (replace stub)
3. **G403**: Build real server implementation (replace stub)
4. **G404**: Build real client implementation (replace stub)
5. **G405**: Build real combined implementation

## References

- Original Half-Life SDK source code
- Xash3D engine architecture
- GameCube libogc documentation
- devkitPPC toolchain specifications