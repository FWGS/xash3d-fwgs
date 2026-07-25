# Xash3D GameCube Port Status

## Current Milestone

**G38: Manual hardware validation required** - Cannot be completed autonomously

## Completed Automatic Goals (G83-G121)

All New Game interactive bring-up is complete:

- **G83-G94**: World rendering, PVS, entity think, player movement, collision, triggers
- **G95-G105**: Changelevel, landmark continuity, viewmodel, inventory
- **G106-G112**: Player edict, PVS LRU, thinks, collision, relink, ground
- **G113-G116**: Native PMove, HUD updates, client prediction
- **G117-G121**: Audio, weapon grant, PrimaryAttack, EV_FireGlock

## Build Status

| Artifact | Size | Format | SHA256 |
|----------|------|--------|--------|
| boot.dol | 5,822,880 bytes | PowerPC DOL | 85bbac2c... |
| xash | 33,122,656 bytes | PowerPC ELF | b725111d... |

**Build command**: `scripts/build-gamecube.sh`
**Last build**: 2026-07-25T09:47:03Z
**Renderer**: REF_GX
**Policy**: retail-flipper

## Dolphin Status

**Status**: Requires manual validation

- Boot in Dolphin tested and verified
- `MAP_READY` state reached in Dolphin
- No visible rendering or audible audio confirmed
- Black screen observed with no Valve startup video, main menu, or rendered map

**Note**: Dolphin evidence alone does not complete G38. Physical hardware validation required.

## Real-Hardware Status

**Status**: Requires manual validation

- Physical GameCube hardware test pending
- Swiss DOL+SD / ISO RO paths prepared
- Handoff artifacts ready at `.ai/logs/hardware-handoff-20260725-094703/`

## Known Failures (Manual Validation Required)

The following goals cannot be completed autonomously and require physical GameCube hardware:

- **G38**: Native GameCube hardware validation (Swiss DOL+SD / ISO RO)
- **G40**: Campaign completion audit (requires runtime validation)
- **G66**: Real hardware release sign-off (requires physical hardware)
- **G70**: Audio/video evidence on target displays (requires physical hardware)
- **G71**: Persistent save/config storage (requires physical hardware)

## Memory Status (G72)

**Status**: Completed

- 24 MiB main-memory budget established
- Subsystem high-water telemetry added
- Cache and visual features bounded for GameCube mode

## Performance Status (G72)

**Status**: Completed

- Frame budget validated against 60 Hz VI pacing
- PVS LRU implemented
- Collision and relink optimized

## Next Task

**Manual hardware validation for G38**

1. Test boot.dol on physical GameCube hardware (Swiss loader + SD card)
2. Test ISO on physical GameCube hardware (disc image)
3. Record video output, controller input, storage, audio, map load
4. Document frame pacing, thermal/stability observations
5. Compare hardware behavior against Dolphin logs
6. Split emulator-only bugs from hardware blockers

## External Blockers

The following goals require physical GameCube hardware and cannot be completed in this autonomous session:

- G38: Native GameCube hardware validation
- G40: Campaign completion audit
- G66: Real hardware release sign-off
- G70: Audio/video evidence on target displays
- G71: Persistent save/config storage

## Handoff Package

**Location**: `.ai/logs/hardware-handoff-20260725-094703/`

**Contents**:
- Build artifacts (boot.dol, xash)
- SHA256 checksums
- Hardware handoff checklist
- Build metadata

## Documentation Files

| File | Status |
|------|--------|
| docs/gamecube/PORT_STATUS.md | **Created** |
| docs/gamecube/ | Directory created |
| docs/GAMECUBE_PORT_PLAN.md | Exists (174,918 bytes) |
| docs/GAMECUBE_PORT_AUDIT.md | Not created |
| docs/GAMECUBE_BUILDING_GAMECUBE.md | Not created |
| docs/GAMECUBE_GX_RENDERER_DESIGN.md | Not created |
| docs/GAMECUBE_GAME_MODULE_LINKING.md | Not created |
| docs/GAMECUBE_ENDIANNESS_AUDIT.md | Not created |
| docs/GAMECUBE_MEMORY_BUDGET.md | Not created |
| docs/GAMECUBE_ASSET_DEPLOYMENT.md | Not created |
| docs/GAMECUBE_ASSET_CACHE_FORMAT.md | Not created |
| docs/GAMECUBE_HARDWARE_TEST_MATRIX.md | Not created |

## Last Updated

2026-07-25T09:47:03Z

## Git Status

- Branch: `agent/gamecube-port`
- Last commit: `1d5afda958` - "stub: Fix stub wscript files for GameCube port"
- Submodules: 8 modified
- Binary assets: 1 modified (OUT/valve/extras.pk3)