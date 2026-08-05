# GameCube Port Audit

## Current Status

**Last Audit**: 2026-08-05 (host gate blockers)
**Port Status**: Dolphin gameplay smoke proven; Swiss/libogc2 stack wired;
host verify/gates fail-closed on G504/G506/G509; hardware release still
manual (G38 / G510).

## Audit Results

### Completed Automatic Goals (G83-G121+)
- World rendering with PVS / Flipper GX path
- Entity think, player movement, triggers, changelevel, landmarks
- Viewmodel / inventory / HUD / client prediction
- ASND audio path (nonzero PCM proven on Dolphin probes; audible HW pending)
- Weapon grant / PrimaryAttack / EV_FireGlock paths

### Host-side Swiss stack (2026-08-05)
- libogc2 + libdvm preferred; classic libogc fallback
- Multi-volume FAT: `sd:` / `carda:` / `cardb:`
- G504 ladder wired into analyze / release / G509 soak
- G506 HUD/viewmodel markers required by gameplay gate
- G508 config round-trip probe + G509 changelevel soak gates wired (Dolphin evidence pending)
- `dolphin-boot-probe.sh` under ai-verify size guard via shared classify helper

### Manual Validation Required (G38, G40, G66, G70, G71, G510)
- Physical GameCube / Swiss hardware test
- Campaign completion audit
- Real hardware release sign-off
- Audio/video evidence on target displays
- Persistent save/config on physical media

## Known Limitations

1. **Hardware**: No physical GameCube release sign-off yet
2. **Audio**: ASND path exists; target-display audible evidence still open
3. **Storage**: Writable FAT volumes implemented (SD2SP2 / SD Gecko); Dolphin disc-only boots use `gcprobe:` for G508
4. **Save/Load**: Engine paths exist; physical SD fault cases unverified
5. **Campaign**: Full Half-Life 1 campaign not tested on hardware

## Next Steps

1. Rebuild with libogc2/libdvm on an operator machine
2. Swiss boot + Dolphin G508/G509 evidence (full ladder + presentation markers)
3. Physical hardware checklist (G510)
4. Campaign / soak archives from one build

## Supported build

```sh
scripts/build-gamecube.sh
# or
python3 scripts/waifulib/gamecube_ogc_stack.py
SKIP_GAMECUBE_BUILD=1 scripts/ai-verify.sh
```
