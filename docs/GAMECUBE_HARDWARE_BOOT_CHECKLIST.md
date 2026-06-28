# GameCube Hardware Boot Preparation Checklist

Use this checklist before running the Xash3D GameCube port on real hardware or
on a Wii in GameCube mode. Dolphin evidence is useful diagnostics, but it is not
real hardware sign-off.

## Pre-Flight

- [ ] Build and verify the current checkout:

```sh
scripts/ai-verify.sh
scripts/gamecube-reproducibility-check.py
```

- [ ] Record the commit hash and artifact hashes from the reproducibility log.
- [ ] Use a FAT32 SD card for SD Gecko or SD2SP2 routes.
- [ ] Place an official or compatible GameCube controller in Port 1.
- [ ] Prefer composite or S-Video for first boot; test component/progressive
  modes only after basic video is proven.
- [ ] Keep local Half-Life assets outside Git. Copy legally owned assets only to
  test media.

## Route A: SD Gecko or SD2SP2

Loader: Swiss or equivalent homebrew loader.

Required media layout:

```text
/boot.dol
/xash3d/valve/liblist.gam
/xash3d/valve/gfx.wad
/xash3d/valve/maps/c0a0e.bsp
/xash3d/valve/models/
/xash3d/valve/sprites/
/xash3d/valve/sound/
```

Notes:

- `boot.dol` comes from `OUT/bin/boot.dol`.
- `xash3d/valve/` contains legally owned Half-Life assets staged for testing.
- SD is the preferred writable route for configs and saves.
- `gcdisc:/xash3d` is read-only and should not be used for generated state.

## Route B: Disc Image

Loader: Swiss, compatible modchip route, or compatible Wii/GameCube-mode loader.

Build the smoke ISO:

```sh
scripts/build-gamecube-disc.py --smoke-map c0a0e --output OUT/xash3d-gc.iso
```

Expected image content:

```text
/boot.dol
/xash3d/valve/
/xash3d/valve/extras.pk3
```

Notes:

- Disc media is read-only.
- Use SD in parallel when testing save/config writes.
- Do not add local Half-Life assets to Git or public release archives.

## Route C: Memory Card Assisted Boot

Memory Cards are not a full asset route. Use this only for loader/bootstrap
experiments when assets remain on SD or disc.

Required expectation:

```text
Memory Card: loader/bootstrap state only
SD or Disc: /xash3d/valve/ assets
```

## Expected First-Screen Evidence

Record as much of this sequence as the loader/log setup allows:

- Artifact hash for `OUT/bin/boot.dol`.
- Loader route, storage route, console model, region, video cable, and
  controller type.
- `Xash3D GameCube: bootstrap`.
- `Xash3D GameCube: engine subsystems ready`.
- `Xash3D GameCube: map loaded c0a0e` or the selected test map.
- `Xash3D GameCube: input polling active`.
- Any `mem stage=` high-water telemetry.
- Any fatal breadcrumb block if boot fails.

## Failure Triage

| Symptom | Likely cause | Action |
| --- | --- | --- |
| Black screen, no OSReport | Loader path, corrupt DOL, or video init failure | Verify `boot.dol` hash, try composite video, and test the same DOL in Dolphin for logs. |
| Bootstrap then black screen | GX/XFB or renderer path failed | Check for diagnostic marker output and `vid_gamecube.c` logs. |
| No input | Controller missing or unsupported state | Use Port 1, try an official wired controller, and check G45 controller logs. |
| No audio | ASND init failed or null-audio route active | Check G48 audio compliance logs; audio failure should not block boot diagnostics. |
| Missing assets | Wrong `xash3d/valve` layout or case mismatch | Verify `liblist.gam`, `gfx.wad`, and the smoke map path with exact lowercase names. |
| Read-only storage errors | Disc-only boot without writable SD | Insert SD for saves/configs or treat write skips as expected for disc-only boot. |
| Memory exhaustion | MEM1 pressure during map/client load | Check `mem stage=` telemetry and use the G57 memory threshold gate. |
| Fatal breadcrumb screen | Engine, filesystem, renderer, or allocation failure | Record subsystem, message, build hash, map, route, memory, and frame values. |

## Helper Command

Print route-specific file placement instructions:

```sh
scripts/gamecube-hardware-layout-info.sh --route all
scripts/gamecube-hardware-layout-info.sh --route sd
scripts/gamecube-hardware-layout-info.sh --route disc
scripts/gamecube-hardware-layout-info.sh --route memcard
```

Run the automated checklist preflight:

```sh
scripts/gamecube-hardware-boot-check.py
```
