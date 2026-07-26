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
G420: Telemetry - Implement telemetry configuration options

**COMPLETED (G410)**:
- Added XASH3D_GC_ASSET_ROOT environment variable support
- Added XASH3D_GC_VALVE_DIR environment variable support
- Environment variables take precedence over default paths
- Enables flexible asset deployment for SD card and disc
- Build verified: boot.dol (3.8M), xash (20M)
- No stub references in build output

**COMPLETED (G411)**:
- Created stage-sd-assets.sh script for asset staging to SD card
- Created stage_sd_assets.py Python script for asset staging
- Both scripts create PK3 archives using makepak.py
- Scripts validate asset structure and report issues
- Scripts support all required asset directories (models, sound, materials, maps, scripts, cfg, resource, fonts, particles)
- Scripts create proper SD card directory structure (xash3d/valve/, save/, logs/, screenshots/)
- Scripts provide deployment instructions for GameCube
- Scripts are executable and tested

**COMPLETED (G412)**:
- Created asset-manager.py unified asset management script
- Combines asset root discovery and SD staging in single tool
- Supports 'discover', 'stage', and 'validate' commands
- Automatically discovers asset roots using environment variables and default paths
- Creates PK3 archives for efficient loading
- Validates asset structure and reports issues
- Provides deployment instructions for GameCube
- Script is executable and tested

**IN PROGRESS (G420)**:
- Added telemetry configuration options (gc_telemetry, gc_telemetry_format cvars)
- Telemetry infrastructure already exists in mem_gamecube.c/h
- BSP loading already implemented in GC_PrepareMapLoadBuffer functions

**NEXT STEPS**:
- G421: BSP loading - Test and document BSP loading optimization
- G422: Server - Test server functionality and performance
- G423: Client - Test client functionality and performance
- G424: Playable frame - Integrate all components and test gameplay loop

Rules:
- Make one bounded implementation or validation patch.
- Preserve unmodified Half-Life asset compatibility.
- Run relevant builds and probes.
- Update durable evidence.
- Do not touch generated build output unless generation is the explicit task.
- Do not commit third-party submodule divergence accidentally.