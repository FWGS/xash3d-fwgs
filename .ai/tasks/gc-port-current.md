Auto-port task for Xash3D GameCube
=================================

Manual checkpoint:
G38 physical GameCube validation is MANUAL_VALIDATION_PENDING.

Selection policy:
- Skip manual-only goals when choosing autonomous implementation work.
- Do not mark manual goals complete without operator evidence.
- Prepare repeatable hardware artifacts and testing instructions.
- Continue with the first incomplete automatable goal in the goal ledger.

Current goal:
G400: Inventory stub/placeholder/dummy/fallback/reduced modules

**INVENTORY COMPLETE**

Verified stub modules:
1. stub/client/ (11,657 bytes)
   - client_stub.c (4,449 bytes) - 17 stub functions
   - client_export.c (7,208 bytes) - export wrapper

2. stub/server/ (12,576 bytes)
   - server_stub.c (10,806 bytes) - 21 stub functions
   - server_export.c (1,770 bytes) - export wrapper

3. stub/pm_shared/ (105,048 bytes)
   - Full PM implementation (not stub)
   - 11 files including pm_shared.c (83,158 bytes)

4. engine/modules/ (4,752 bytes)
   - stub_inventory.c/h - Module inventory system

Total stub modules: 15 files, 129,281 bytes

**BUILD STATE VERIFICATION**
- HLSDK archives ARE present at OUT/hlsdk-gamecube/valve/
- Build correctly uses HLSDK when available (default behavior)
- Stub modules are fallback when HLSDK not found
- module_linkage.csv reports hardcoded stub status (needs verification)

Next goal after G400:
G401: Produce verified final-ELF module linkage matrix

**VERIFICATION IN PROGRESS**
- Analyzing ELF symbol table to verify actual module linkage
- Filtering out gamecube_hlsdk symbols to identify real linkage

Rules:
- Make one bounded implementation or validation patch.
- Preserve unmodified Half-Life asset compatibility.
- Run relevant builds and probes.
- Update durable evidence.
- Do not touch generated build output unless generation is the explicit task.
- Do not commit third-party submodule divergence accidentally.
