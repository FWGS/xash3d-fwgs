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

1. **Dolphin smoke-map runtime timeout**
   - Repro: `scripts/dolphin-boot-probe.sh`
   - Current route: bounded smoke-map boot
   - Symptom: engine readiness observed, `c0a0e` never reaches `map loaded`
     within probe timeout
   - Priority: highest autonomous blocker

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
