# Xash3D GameCube Port Status

## Current Milestone

**Dolphin runtime gate** - the current autonomous milestone is no longer
physical-hardware-only G38. The next required proof is that the current
GameCube build boots in Dolphin, reaches engine readiness, and loads the smoke
map through the standard bounded probe route.

## Current Automated State

- Early DOL boot regression was fixed on **July 29, 2026** by correcting
  `scripts/elf-to-dol.py` so generated DOL headers carry the proper entry
  point, section tables, and BSS metadata.
- The latest bounded Dolphin probe now reaches **engine readiness** and
  reports `BOOT_PHASE: sw_fb`, which moves the failure boundary beyond raw boot
  and into runtime/map-load behavior.
- The current blocking runtime failure is:
  `MAP_TIMEOUT: Engine readiness was observed, but c0a0e did not load within 180s.`

## Build Status

| Artifact | Size | Format |
|----------|------|--------|
| `OUT/bin/boot.dol` | about 4.0 MiB | PowerPC DOL |
| `OUT/bin/xash` | about 20.8 MiB | PowerPC ELF |

**Build Notes**:
- Engine builds successfully.
- DOL generation is now structurally valid for Dolphin boot.
- No current build error blocks runtime work.

## Dolphin Status

**Status**: Active autonomous blocker

Latest useful runtime evidence on **July 29, 2026**:
- Probe log: `.ai/logs/dolphin-probe-20260728-225124/`
- Result: `MAP_TIMEOUT`
- Engine readiness: observed
- Time to first frame: 1.884 seconds
- Boot phase: `sw_fb`
- G45 synthetic input readiness: PASS
- Smoke map load (`c0a0e`): FAIL within 180 seconds

Interpretation:
- The disc boots.
- The guest reaches early engine/runtime initialization.
- The current autonomous blocker is smoke-map load or runtime progression after
  early video/input readiness, not raw emulator launch and not DOL entry.

## Real-Hardware Status

**Status**: Deferred sign-off, not the current autonomous blocker

- Physical GameCube / Swiss validation still matters for release confidence.
- G38 remains a manual checkpoint for native hardware confirmation.
- It is no longer the stopping condition for autonomous work while Dolphin
  runtime validation is still failing.

## Known Active Failure

### Evidence-gated re_agent use

The function-level re_agent path is opt-in. It is eligible only when fresh
evidence names a concrete function result, static call path, ABI/structure
layout, or repeated guest-trace address. A `delta.lst`, search-path, or asset
staging failure is a filesystem contract and must not be routed to decompilation.

When eligible, acceptance requires the re_agent report fields `address`,
`decompile`, `source_match`, and `confidence`, followed by the normal clean
build, valid DOL/disc, reduced map-load ladder, and no-guest-fatal gates.

Immediate delta.lst sequence: pause automation; instrument independent
`FS_FileExists` and `FS_LoadFile` calls; verify `delta.lst` in the generated
smoke ISO; run the reduced ladder; make one filesystem/path fix; rerun the
probe; then update this status from fresh evidence.

1. **Dolphin smoke-map runtime timeout**
   - Repro: `scripts/dolphin-boot-probe.sh`
   - Current route: bounded smoke-map boot
   - Symptom: engine readiness observed, `c0a0e` never reaches `map loaded`
     within probe timeout
   - Priority: highest autonomous blocker

### Latest reproducible evidence — 2026-08-02

- Build: `scripts/build-gamecube.sh` completes with devkitPPC/libogc and emits
  a valid DOL (`OUT/bin/boot.dol`, about 4.6 MiB).
- Static HLSDK registration: fixed/confirmed in the rebuilt artifact;
  Dolphin reports `COM_LoadLibrary server (registered)`.
- Probe: `DOLPHIN_TIMEOUT=60 scripts/dolphin-boot-probe.sh`.
- Evidence: `.ai/logs/dolphin-probe-20260802-022115/` and later retries.
- Failure boundary: after `find found 'maps/c0a0e.bsp'`, the bounded route
  enters `G201 delta reinit` and fails on `Delta_InitFields: couldn't load
  file delta.lst`; the probe then reports `GUEST_RUNTIME_ERROR`.
- Important non-fix: skipping G201 reinitialization for `-gcmap` was tested but
  caused the subsequent BSP open to report `exists=0`; that experiment was
  reverted. Do not treat it as a passing workaround.

## Next Task

**Make the smoke-map Dolphin probe pass again**

1. Inspect the latest probe bundle under `.ai/logs/dolphin-probe-20260728-225124/`
2. Trace the runtime path from engine readiness to `Xash3D GameCube: map loaded c0a0e`
3. Fix the narrowest source-side blocker preventing bounded map load
4. Re-run `scripts/dolphin-boot-probe.sh`
5. Keep physical-hardware handoff docs intact, but do not treat them as the
   next blocking milestone while Dolphin runtime is still failing

## External Blockers

None for the current autonomous milestone.

Physical-hardware sign-off remains a later manual checkpoint under G38/G66, but
it is not the reason the automation should stop today.

## Last Updated

2026-07-29
