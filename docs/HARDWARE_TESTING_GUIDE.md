# GameCube Hardware Testing Guide

This guide provides step-by-step instructions for manual hardware validation of the xash3d-gamecube port.

## Overview

The GameCube port requires physical hardware testing to validate:
- Boot behavior on real GameCube hardware
- Controller input handling
- Storage write/read operations
- Audio output behavior
- Frame pacing and stability
- Memory pressure under real hardware constraints

## Prerequisites

### Required Hardware
- **GameCube** (or Wii in GameCube mode)
- **SD card** (2GB-32GB recommended, FAT32 formatted)
- **SD card reader** (for preparing the SD card)
- **GameCube controller** (recommended, compatible third-party pads may work)
- **Video capture setup** (optional but recommended for documentation)

### Required Software
- **Swiss loader** (homebrew GameCube loader)
- **Legal Half-Life assets** (you must own these legally)

## Setup Instructions

### 1. Prepare the SD Card

Create the following directory structure on your SD card:

```
sd:/apps/xash3d-gc/boot.dol
sd:/xash3d/valve/           (Half-Life assets)
sd:/xash3d/valve/save/      (for save games)
sd:/xash3d/valve/logs/      (for logs)
sd:/xash3d/valve/screenshots/ (for screenshots)
```

### 2. Copy the Build Artifacts

1. Copy `OUT/bin/boot.dol` to `sd:/apps/xash3d-gc/boot.dol`
2. Copy your Half-Life `valve` folder to `sd:/xash3d/valve/`

**Important**: Do not include Nintendo SDK files, BIOS/IPL dumps, or copyrighted assets in your distribution.

### 3. Verify Your Setup

Before testing, verify:
- [ ] SD card is properly formatted (FAT32)
- [ ] boot.dol is in the correct location
- [ ] Half-Life assets are in the correct location
- [ ] Controller is connected to port 0

## Testing Procedures

### Test 1: Boot Test (HW-BOOT-001)

**Objective**: Verify the port boots on physical hardware

**Steps**:
1. Insert SD card into GameCube
2. Boot through Swiss loader
3. Select xash3d-gc application
4. Observe video output

**Expected Results**:
- GameCube displays video output
- Boot process begins
- No loader errors

**Document**:
- Hardware model (e.g., RVL-101, RVL-001)
- Video cable type (AV, RGB, HDMI via converter)
- Swiss loader version
- Boot behavior notes

### Test 2: Engine Readiness (HW-BOOT-002)

**Objective**: Verify the engine initializes correctly

**Steps**:
1. Wait for engine initialization
2. Look for OSReport markers
3. Check for "REF_GX static GetRefAPI" message
4. Verify "retail Flipper policy" markers

**Expected Results**:
- Engine reaches readiness marker
- No unbounded hangs during initialization
- OSReport markers visible

**Document**:
- Time to reach readiness
- Any error messages
- OSReport output

### Test 3: Map Load Test (HW-MAP-001)

**Objective**: Verify map loading works correctly

**Steps**:
1. Ensure Half-Life assets are accessible
2. Load map `c0a0e` (smoke map)
3. Verify map loads successfully
4. Observe any errors

**Expected Results**:
- Map loads without errors
- BSP geometry renders
- Entities spawn correctly

**Document**:
- Map load time
- Any error messages
- Visual artifacts

### Test 4: Player Spawn Test (HW-PLAYER-001)

**Objective**: Verify player entity spawns correctly

**Steps**:
1. Verify player spawns after map load
2. Check camera/view initialization
3. Observe player model
4. Test initial movement

**Expected Results**:
- Player model appears
- Camera initializes correctly
- Player can move

**Document**:
- Spawn location
- Player model appearance
- Initial movement behavior

### Test 5: Controller Test (HW-INPUT-001)

**Objective**: Verify controller input handling

**Steps**:
1. Test movement (WASD or D-pad)
2. Test look (mouse or right stick)
3. Test use/fire buttons
4. Test jump button
5. Test pause/menu
6. Test disconnect/reconnect behavior

**Expected Results**:
- All inputs respond correctly
- No stuck inputs
- Disconnect/reconnect handled gracefully

**Document**:
- Controller type
- Input latency
- Any input issues

### Test 6: Audio Test (HW-AUDIO-001)

**Objective**: Verify audio output behavior

**Steps**:
1. Listen for audio output
2. Test weapon sounds
3. Test ambient sounds
4. Test menu sounds
5. Note any silence or hangs

**Expected Results**:
- Audio plays correctly (or null fallback)
- No unbounded hangs
- No audio distortion

**Document**:
- Audio backend used (null or ASND)
- Audio quality
- Any issues

### Test 7: Storage Test (HW-FS-001, HW-FS-002)

**Objective**: Verify storage operations

**Steps**:
1. Verify SD card is detected
2. Check for config file creation
3. Test save game creation
4. Test save game loading
5. Verify directory structure

**Expected Results**:
- SD card detected
- Config files created
- Save games work
- Directory structure correct

**Document**:
- Storage type (SD card)
- Config file locations
- Save game behavior

### Test 8: Stability Test (HW-STABILITY-001)

**Objective**: Verify long-term stability

**Steps**:
1. Run for at least 5 minutes
2. Monitor for crashes
3. Check for thermal issues
4. Observe frame pacing
5. Test extended gameplay

**Expected Results**:
- No crashes
- Stable frame rate
- No thermal issues
- Consistent performance

**Document**:
- Test duration
- Frame rate observations
- Any issues encountered

## Failure Classification

If any test fails, use one of these labels:

| Label | Description |
|-------|-------------|
| `loader_failure` | Loader rejects artifact |
| `no_video` | No video output |
| `bootstrap_failure` | Boot fails before engine |
| `filesystem_mount_failure` | Storage mount issues |
| `asset_lookup_failure` | Map/asset loading issues |
| `config_write_failure` | Config save issues |
| `save_load_failure` | Save game issues |
| `bsp_load_failure` | BSP parsing issues |
| `entity_spawn_failure` | Entity spawn issues |
| `renderer_failure` | Rendering issues |
| `controller_failure` | Controller input issues |
| `audio_failure` | Audio output issues |
| `memory_pressure` | Memory issues |
| `performance_stall` | Frame pacing issues |
| `bounded_hang` | Timeout with recovery |
| `unbounded_hang` | Infinite hang |
| `crash` | Unexpected crash |
| `thermal_or_power_issue` | Hardware issues |
| `unknown` | Unknown cause |

## Evidence Template

Use this template to document test results:

```markdown
### Hardware validation — YYYY-MM-DD — TEST-ID

- Tester:
- Commit: (commit hash)
- Build command: scripts/gamecube-hardware-handoff.sh --build
- Artifact: OUT/bin/boot.dol
- Hardware: (GameCube model, region)
- Loader: (Swiss version, loader name)
- Video route: (AV cable, HDMI converter, etc.)
- Storage route: (SD card, size, format)
- Asset route: (SD card location)
- Controller: (GameCube controller, third-party)
- Result: (pass/partial/fail)
- Furthest reached: (boot/map/gameplay/etc.)
- Evidence: (photos, videos, logs)
- Failure label: (if applicable)
- Notes: (additional observations)
- Next blocker: (what needs to be fixed)
```

## Troubleshooting

### No Video Output
- Check video cable connections
- Verify TV/monitor is on correct input
- Try different video mode
- Check for corrupted boot.dol

### Loader Failure
- Verify boot.dol is in correct location
- Check SD card formatting (FAT32)
- Try different SD card
- Update Swiss loader

### Map Load Failure
- Verify Half-Life assets are present
- Check asset path structure
- Verify assets are not corrupted
- Check SD card free space

### Audio Issues
- Verify audio backend configuration
- Check for null fallback (expected on some setups)
- Test with different audio settings
- Check for hardware audio limitations

### Controller Issues
- Verify controller is connected
- Try different controller
- Check for controller compatibility
- Test disconnect/reconnect behavior

## Next Steps

After completing hardware testing:

1. **Document Results**: Fill in the evidence template with your test results
2. **Update Port Status**: Add results to `docs/GAMECUBE_PORT_PLAN.md`
3. **Report Issues**: If failures occurred, document the next blockers
4. **Continue Development**: Address any identified issues

## Additional Resources

- **Hardware Validation Protocol**: `docs/GAMECUBE_HARDWARE_VALIDATION.md`
- **Port Status**: `docs/GAMECUBE_PORT_STATUS.md`
- **Port Plan**: `docs/GAMECUBE_PORT_PLAN.md`
- **Hardware Matrix**: `docs/GAMECUBE_HARDWARE_MATRIX.md`

## Support

For questions or issues:
1. Check the troubleshooting section above
2. Review the detailed validation protocol
3. Document your issue with full context
4. Provide hardware specifications and test results

## Legal Notice

This guide is for testing purposes only. You must legally own Half-Life assets to use this software. Do not distribute copyrighted assets.