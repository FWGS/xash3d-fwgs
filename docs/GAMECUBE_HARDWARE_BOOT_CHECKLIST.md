# GameCube Hardware Boot Preparation Checklist

Use this checklist before running the Xash3D GameCube port on real hardware.

## Pre-flight

- [ ] Clean, high-speed SD card (SD2SP2/SD Gecko) or GameCube Memory Card.
- [ ] Supported loader installed (e.g., Swiss).
- [ ] Official or compatible GameCube controller in Port 1.
- [ ] Video cable connected (Component for 480i/480p, Composite/S-Video for 480i).
- [ ] Console power cycled (cold boot recommended for first test).

## Artifact Preparation

Generate artifacts using:
```sh
scripts/build-gamecube.sh
scripts/build-gamecube-disc.py --smoke-map c0a0e
```

Verify artifacts:
```sh
ls -l OUT/bin/boot.dol OUT/xash3d-gc.iso
sha256sum OUT/bin/boot.dol OUT/xash3d-gc.iso
```

## Loader Routes and Layout

### Route A: SD2SP2 / SD Gecko (DOL Boot)
**Loader:** Swiss or equivalent.
**SD Card Layout (FAT32):**
```
/
├── boot.dol          <-- Copy from OUT/bin/boot.dol
├── xash3d/
│   └── valve/        <-- Game assets (valve.rc, liblist.gam, etc.)
└── ...
```
**Expected First Screen:**
Black screen for 1-2s, then OSReport text: `Xash3D GameCube: bootstrap`.

### Route B: GameCube Disc (ISO Boot)
**Loader:** Console IPL / DVD drive.
**Disc Image:** `OUT/xash3d-gc.iso`
**Note:** The ISO contains `boot.dol`, an FST partition, and staged smoke assets.
**Expected First Screen:**
Black screen for 1-2s, then OSReport text: `Xash3D GameCube: bootstrap`.

### Route C: Memory Card (DOL Boot)
**Loader:** Swiss (Memory Card loader).
**Memory Card Layout:**
```
/
├── boot.dol          <-- Copy from OUT/bin/boot.dol
└── ...
```
**Note:** Game assets must be accessed via SD Card or Disc in parallel, as Memory Cards are too small for full `valve/` content. Usually used for loader/boot only with assets on SD/Disc.

## Verification Commands

Print layout instructions for a specific route:
```sh
scripts/gamecube-hardware-layout-info.sh --route sd
scripts/gamecube-hardware-layout-info.sh --route disc
```

## Failure Triage Table

| Symptom | Possible Cause | Verification / Action |
| :--- | :--- | :--- |
| **Black Screen (No Text)** | No `boot.dol` found, wrong path, or corrupt DOL. | Verify `boot.dol` is at root of SD or in ISO. Check loader settings. |
| **Black Screen (After Bootstrap)** | Video init failure or renderer crash. | Check if diagnostic checker (top-left corner) appears. If not, debug `vid_gamecube.c`. |
| **No Input** | Controller not detected in Port 1. | Verify controller is plugged into Port 1. Try official controller. |
| **No Audio** | ASND init failed, falling back to null backend. | Expected for initial smoke tests if ASND isn't fully stable. Check `-gcnullaudio` cvar. |
| **Missing Assets / Map Fail** | Wrong search path, disc not mounted, or SD not mounted. | Check OSReport for `SD card init failed` or `FS_LoadProgs` errors. |
| **Read-Only Storage Errors** | Trying to write to Disc or unmounted SD. | Ensure SD is present for writes. Disc is read-only. |
| **Memory Exhaustion (Hang/Crash)** | MEM1 (24MB) limit exceeded. | Check `mem stage=` telemetry in OSReport. Use `-gcmap` for minimal load. |
| **Disc Read Errors** | Bad burn, corrupted ISO, or dirty lens. | Verify ISO checksum. Clean lens. Try different disc. |

## Next Steps

If boot succeeds:
1. Observe `Xash3D GameCube: engine subsystems ready`.
2. Watch for `Xash3D GameCube: map loaded c0a0e`.
3. Use controller to verify input polling (`Xash3D GameCube: input polling active`).
4. Record video/audio for compliance evidence (G38/G53).
