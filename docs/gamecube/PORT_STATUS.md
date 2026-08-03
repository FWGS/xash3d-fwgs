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

1. **Dolphin filesystem initialization timeout**
   - Repro: `scripts/dolphin-boot-probe.sh`
   - Current route: bounded smoke-map boot
   - Symptom: read-only boot reaches `gcdisc:/xash3d`, then stalls during the
     missing-`vfs.cfg` addon fallback before engine readiness
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

### Latest diagnostic probe — 2026-08-02

- Diagnostic build: passed; fresh `OUT/bin/boot.dol` and smoke ISO generated.
- ISO staging: `xash3d/valve/delta.lst` present, 12,567 bytes.
- Probe bundle: `.ai/logs/dolphin-probe-20260802-191640/`.
- New first failure boundary: the guest reached DVD mount, filesystem module
  registration, and `gcdisc:/xash3d`, then timed out before engine readiness.
- Last guest marker: `FS_LoadVFSConfig: vfs.cfg not found on disc, mounting addon fallback`.
- The G201 `FS_FileExists`/`FS_LoadFile` diagnostic did not execute in this
  run, so the delta contract remains unclassified. Do not apply a delta fix
  from this probe.

### Follow-up filesystem experiment — 2026-08-02

- Hypothesis: the missing-`vfs.cfg` read-only fallback hangs in
  `FS_Rescan_f()`, before engine/server startup.
- Evidence: `.ai/logs/dolphin-probe-20260802-192020/` reached
  `vfs fallback rescan begin` but never reached `vfs fallback rescan done`.
- One scoped change: skip that rescan because GameCube `FS_MountFlags()` already
  enables the addon path on the read-only route.
- Result: `.ai/logs/dolphin-probe-20260802-192224/` passed the fallback and
  reached controller readiness, then stalled inside `SV_InitGame` before the
  server-library path and G201 diagnostics.
- Decision: keep the filesystem fallback change as an experiment, do not accept
  it as a release fix yet, and do not invoke re_agent without a function/address

### Server initialization follow-up — 2026-08-02

- Probe bundle: `.ai/logs/dolphin-probe-20260802-194638/`.
- The fresh DOL contains `SV_InitGame entry`, `SV_InitGame reset error begin`,
  and server-library path markers, but the guest emitted none of them after
  `server game init begin`.
- The observed call path therefore does not reach the first statement of the
  expected `SV_InitGame` body, or guest state is already invalid at entry.
- This is not sufficient evidence for re_agent: no repeated guest address or
  proven function mismatch exists yet. Capture a call-site/ABI or guest trace
  target before using decompilation.

### SV_InitGame argument boundary — 2026-08-02

- Probe bundle: `.ai/logs/dolphin-probe-20260802-195057/`.
- The guest emitted `server game init begin` and
  `server game init argument begin`, but not `server game init argument ready`.
- The argument is `GI->gamemode != GAME_SINGLEPLAYER_ONLY`; therefore the
  failure occurs while reading `GI`, before the direct call to `SV_InitGame`.
- Static ELF inspection confirms `SV_Init` branches directly from `0x800b3d18`
  to `SV_InitGame` at `0x800afec8`, whose first report call is at `0x800afeec`.
- Current blocker: unusable or invalid `FI.GameInfo`/`GI` state at the
  `gamemode` read, or a guest memory/ABI fault at that access. `delta.lst` and
  server-module loading have not been reached in this run.
- Do not invoke re_agent yet: there is still no concrete repeated guest fault
  address or proven function/ABI mismatch.

### G201 reached — 2026-08-02

- Probe bundle: `.ai/logs/dolphin-probe-20260802-195808/`.
- Root cause of the previous null `GI`: the engine-side `FS_LoadGameInfo`
  wrapper processed `vfs.cfg` but never called the filesystem API's
  `LoadGameInfo` entry point. Adding that handoff produced
  `FI->GameInfo=0x80ff6218` and allowed `SV_InitGame` to enter.
- Server module boundary passed: server path resolved to `server`, and G201
  executed.
- G201 result: `delta.lst` has `FS_FileExists=0`, `FS_LoadFile=0`; the explicit
  `valve/delta.lst` query has `FS_FileExists=1` but `FS_LoadFile=0`. Both disk
  paths report `(none)`, despite the ISO containing
  `xash3d/valve/delta.lst`.
- The next fatal is independent: `maps/c0a0e.bsp` cannot be loaded from disk.
  Do not skip Delta initialization or accept the game-info handoff as a
  release fix until the filesystem load contract and map staging pass.

## Next Task

**Isolate the FI.GameInfo/GI handoff before SV_InitGame**

1. Identify where `FI.GameInfo` is assigned before `SV_Init`.
2. Add one bounded diagnostic for its pointer and safe scalar state.
3. Validate the pointer against the generated ELF/DOL memory sections and
   rerun the reduced probe.
4. Only after this boundary passes, return to the independent `delta.lst`
   `FS_FileExists`/`FS_LoadFile` diagnostic.
5. Keep physical-hardware handoff docs intact, but do not treat them as the
   next blocking milestone while Dolphin runtime is still failing.

## External Blockers

None for the current autonomous milestone.

Physical-hardware sign-off remains a later manual checkpoint under G38/G66, but
it is not the reason the automation should stop today.

## Last Updated

2026-07-29
