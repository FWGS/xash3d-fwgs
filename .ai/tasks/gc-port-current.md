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
G410: Asset root discovery

**COMPLETED (G410)**:
- Added XASH3D_GC_ASSET_ROOT environment variable support
- Added XASH3D_GC_VALVE_DIR environment variable support
- Environment variables take precedence over default paths
- Enables flexible asset deployment for SD card and disc
- Build verified: boot.dol (3.8M), xash (20M)
- No stub references in build output

**NEXT STEPS**:
- G411: Asset staging to SD - Create asset staging tools and scripts
- G412: Asset root discovery and SD staging - Combine asset root discovery and SD staging
- G420: Telemetry - Implement telemetry system for GameCube
- G421: BSP loading - Implement BSP file loading for GameCube

Rules:
- Make one bounded implementation or validation patch.
- Preserve unmodified Half-Life asset compatibility.
- Run relevant builds and probes.
- Update durable evidence.
- Do not touch generated build output unless generation is the explicit task.
- Do not commit third-party submodule divergence accidentally.