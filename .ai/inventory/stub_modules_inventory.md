# Xash3D GameCube Stub Modules Inventory

**Date:** 2026-07-25  
**Branch:** `agent/gamecube-port`  
**Build Status:** ✅ Successful (32MB ELF, 5.6MB DOL)

---

## Overview

The GameCube port uses **stub implementations** for critical modules that are not yet fully implemented. These stubs provide minimal placeholder functionality to allow the engine to build and run with reduced capabilities.

---

## Current Stub Modules

### 1. Client Module (`stub/client/`)

**Files:**
- `client_stub.c` - Stub implementation
- `client_export.c` - DLL registration

**Exported Functions:**
- `GetClientAPI(cldll_func_t *funcs)` - Main API entry point

**Stub Implementation Details:**
- All client functions return default/empty values
- Player movement uses `PM_Move` and `PM_Init` from pm_shared
- Input handling returns zeroed usercmd_t structures
- Rendering functions are empty stubs
- No actual game logic implemented

**Key Functions:**
```c
- pfnInitialize: Returns 1 (success)
- pfnVidInit: Returns 1 (success)
- pfnRedraw: Returns 1 (success)
- pfnUpdateClientData: Returns 1 (success)
- pfnPlayerMove: Calls PM_Move (physics only)
- pfnCreateMove: Zeroes usercmd_t structure
- pfnKey_Event: Returns 0 (no event processed)
- pfnAddEntity: Returns 0 (no entities added)
- pfnFrame: Empty stub
```

**Status:** ⚠️ **FUNCTIONALITY LIMITED**
- Physics movement works (via pm_shared)
- No actual rendering, input, or game logic
- Suitable only for testing module infrastructure

---

### 2. Server Module (`stub/server/`)

**Files:**
- `server_stub.c` - Stub implementation
- `server_export.c` - DLL registration

**Exported Functions:**
- `GiveFnptrsToDll(enginefuncs_t *engfuncs, globalvars_t *pGlobals)`
- `GetEntityAPI(DLL_FUNCTIONS *pFunctionTable, int interfaceVersion)`
- `GetEntityAPI2(DLL_FUNCTIONS *pFunctionTable, int *interfaceVersion)`
- `custom(entvars_t *pev)` - Entity initialization

**Stub Implementation Details:**
- All server functions return default values
- Entity spawning uses basic defaults
- No actual game logic or world simulation
- Client connection returns success without actual connection

**Key Functions:**
```c
- pfnSpawn: Returns 0 (success)
- pfnKeyValue: Handles basic keyvalues (classname, origin, angles, model)
- pfnClientConnect: Returns true (always accepts)
- pfnClientPutInServer: Sets default player state
- pfnAddToFullPack: Returns 1 (always adds entity)
- pfnCreateBaseline: Creates default entity baseline
- pfnServerActivate: Empty stub
- pfnServerDeactivate: Empty stub
```

**Status:** ⚠️ **FUNCTIONALITY LIMITED**
- Basic entity system works
- No actual gameplay, AI, or world simulation
- Suitable only for testing module infrastructure

---

### 3. Menu Module (`stub/menu/`)

**Files:**
- `menu_stub.c` - Stub implementation
- `menu_export.c` - DLL registration

**Exported Functions:**
- `GetMenuAPI(UI_FUNCTIONS *pFunctionTable, ui_enginefuncs_t *engfuncs, ui_globalvars_t *pGlobals)`

**Stub Implementation Details:**
- Simple menu system with 5 items
- Keyboard/controller navigation
- Basic drawing with console string functions
- No actual game state management

**Menu Items:**
1. New Game - Attempts to start new game
2. Load Game - Attempts to load game
3. Options - Attempts to open options
4. Controller - Attempts controller config
5. System - Attempts system menu

**Key Functions:**
```c
- pfnVidInit: Returns 1 (success)
- pfnInit: Sets selection to 0
- pfnShutdown: Deactivates menu
- pfnRedraw: Draws menu items with highlighting
- pfnKeyEvent: Handles UP/DOWN/ENTER/ESCAPE
- pfnSetActiveMenu: Activates/deactivates menu
```

**Status:** ⚠️ **FUNCTIONALITY LIMITED**
- Menu display and navigation works
- No actual game state changes
- No save/load functionality
- Suitable only for testing module infrastructure

---

### 4. Renderer Module (`ref/gx/`)

**Files:**
- `r_dll_export.c` - DLL registration

**Exported Functions:**
- `GetRefAPI(int version, ref_interface_t *funcs, ref_api_t *engfuncs, ref_globals_t *globals)`

**Status:** ⚠️ **IMPLEMENTED ELSEWHERE**
- Renderer is in `ref/gx/` directory (not stub)
- Uses GX (Graphics Synthesizer) for rendering
- Full implementation (not stub)

---

### 5. Filesystem Module (`filesystem_stdio`)

**Exported Functions:**
- `GetFSAPI(int version, fs_api_t *api, fs_globals_t **globals, fs_interface_t *engfuncs)`

**Status:** ✅ **FULLY IMPLEMENTED**
- Uses libiso9660 for DVD filesystem
- Uses fatfs for SD card filesystem
- Full implementation in engine

---

### 6. Audio Module

**Status:** ⚠️ **NOT YET IMPLEMENTED**
- No stub implementation present
- Audio system not available

---

### 7. Input Module

**Status:** ⚠️ **NOT YET IMPLEMENTED**
- No stub implementation present
- Input system not available (except through client stub)

---

## Module Infrastructure

### Module System (`engine/modules/`)

**Files:**
- `module.h` - Module interface definitions
- `module.c` - Module infrastructure implementation
- `stub_inventory.c` - Stub module inventory
- `stub_inventory.h` - Stub inventory header

**Module Types:**
```c
MODULE_TYPE_UNKNOWN = 0
MODULE_TYPE_CLIENT
MODULE_TYPE_SERVER
MODULE_TYPE_MENU
MODULE_TYPE_REF
MODULE_TYPE_FS
MODULE_TYPE_AUDIO
MODULE_TYPE_INPUT
MODULE_TYPE_MAX
```

**Module State:**
```c
MODULE_STATE_UNLOADED = 0
MODULE_STATE_LOADING
MODULE_STATE_LOADED
MODULE_STATE_ERROR
MODULE_STATE_MAX
```

**Key Functions:**
- `Module_Init()` - Initialize module inventory
- `Module_Shutdown()` - Shutdown module inventory
- `Module_GetInventory()` - Get module inventory
- `Module_Find(const char *name)` - Find module by name
- `Module_Load(const char *name, module_type_t type)` - Load module
- `Module_Unload(const char *name)` - Unload module
- `Module_RegisterStub(...)` - Register stub module
- `Module_CreateStub(...)` - Create stub module with exports
- `Module_Report()` - Report module status

**Stub Inventory Functions:**
- `Stub_Inventory_Init()` - Initialize all stub modules
- `Stub_Inventory_Shutdown()` - Shutdown stub inventory
- `Stub_Inventory_Report()` - Report stub inventory status

---

## DLL Registration System (`engine/platform/gamecube/dll_gamecube.c`)

**Registration Functions:**
```c
int setup_gamecube_filesystem_exports()
int setup_gamecube_ref_exports()
int setup_gamecube_client_exports()
int setup_gamecube_server_exports()
int setup_gamecube_menu_exports()
int setup_gamecube_dll_functions()  // Calls all above
```

**Registered DLLs:**
- `filesystem_stdio` - Filesystem (full implementation)
- `libref_gx.so` - Renderer (full implementation)
- `cl_dlls/client_gamecube_ppc.so` - Client (stub)
- `dlls/hl_gamecube_ppc.so` - Server (stub)
- `menu` - Menu (stub)

---

## Build Configuration

**Build Command:**
```bash
DEVKITPRO=/opt/devkitpro python3 waf configure --gamecube
DEVKITPRO=/opt/devkitpro python3 waf build
```

**Output:**
- `build/engine/xash` - 32MB ELF (PowerPC)
- `OUT/bin/boot.dol` - 5.6MB DOL (GameCube executable)

**Compiler Flags:**
- Target: PowerPC (devkitPPC)
- Architecture: G4 (750)
- Endian: Big-endian
- ABI: EABI

---

## Limitations

### Current Limitations:

1. **No Actual Gameplay**
   - Client stub provides no rendering or input
   - Server stub provides no game logic
   - Menu stub provides no actual game state changes

2. **No Audio**
   - Audio module not implemented
   - No sound or music

3. **No Input System**
   - Input module not implemented
   - Only basic controller input through client stub

4. **No Network**
   - Network functionality disabled
   - Local loopback only

5. **Reduced Functionality**
   - All modules return default values
   - No actual game state management
   - No save/load functionality

### What Works:

1. **Module Infrastructure**
   - Module system fully functional
   - Stub registration works
   - DLL registration works

2. **Basic Filesystem**
   - DVD filesystem (libiso9660)
   - SD card filesystem (fatfs)

3. **Basic Rendering**
   - GX renderer (full implementation)
   - Video output to TV

4. **Basic Physics**
   - pm_shared functions work
   - Player movement physics

---

## Next Steps

### G400-G433 Priority Queue:

1. **G400-G405: Module Implementation**
   - Replace stub implementations with real implementations
   - Implement actual client rendering
   - Implement actual server game logic
   - Implement actual menu functionality

2. **G406-G410: Feature Implementation**
   - Implement audio system
   - Implement input system
   - Implement network functionality

3. **G411-G415: Testing & Validation**
   - Test with actual game content
   - Validate module loading
   - Validate gameplay functionality

---

## Conclusion

The GameCube port currently uses **stub implementations** for client, server, and menu modules. These stubs provide minimal functionality to allow the engine to build and run, but they do not provide actual gameplay functionality.

The module infrastructure is fully implemented and functional. The next priority is to replace stub implementations with real implementations to provide actual gameplay functionality.

**Build Status:** ✅ **BUILDING SUCCESSFULLY**  
**Functionality Status:** ⚠️ **STUB IMPLEMENTATIONS ONLY**  
**Next Priority:** G400-G405 (Module Implementation)