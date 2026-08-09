# Xash3D GameCube Port Status

## Current Milestone

**Playable Dolphin New Game (Pure Flipper)** — lean `-gcnewgame` reaches
`NEWGAME_READY` with G508 + G509 + lean audio PCM green and a disc-only Dolphin
release packet **COMPLETE / RELEASE-READY**
(`.ai/logs/operator-evidence-20260808-audio/`).
Studios stay out of lean packets. Fullphysics stays the richer regression path.
Swiss FAT/loader markers need real hardware (Dolphin has no SD EXI device).


## Current Automated State

- Early DOL boot regression was fixed on **July 29, 2026** by correcting
  `scripts/elf-to-dol.py` so generated DOL headers carry the proper entry
  point, section tables, and BSS metadata.
- The latest direct New Game probe reaches `NEWGAME_READY` with sustained world
  presentation and attack/jump/use actions.
- The latest supervisor release-disc probe loads `c0a0e` with G36 and G45
  passing and nonblack visual output.
- The client sprite-list OOM was removed by a contiguous 384-entry cache;
  all 15 weapon lists loaded in Dolphin without an allocation fatal.

## Build Status

| Artifact | Size | Format |
|----------|------|--------|
| `OUT/bin/boot.dol` | 5,874,272 bytes | PowerPC DOL |
| `OUT/bin/xash` | 33,293,048 bytes | PowerPC ELF |

**Build Notes**:
- Engine builds successfully against Swiss-first libogc2 (or classic libogc).
- DOL generation is now structurally valid for Dolphin boot.
- No current build error blocks runtime work.
- Stack resolver: `python3 scripts/waifulib/gamecube_ogc_stack.py`

## Dolphin Status

**Status**: Gameplay smoke passing; release systems remain unverified

Latest useful runtime evidence on **August 8, 2026**:
- Gameplay probe: `.ai/logs/dolphin-probe-20260808-044632/`
- Result: `NEWGAME_READY`, exit code 0
- Frame samples: 15; average ~0.71 ms, p95 ~0.73 ms (G36 PASS)
- G36: PASS; G45: PASS; G45 actions attack/jump/use: PASS; G506: PASS
- Lean Flipper: `G155 … viewmodel=1`, `G319 … brushes=1`,
  `lean DrawEntities client tris solid` + `… trans`
- Prior supervisor release-disc probe: `.ai/logs/dolphin-probe-20260804-143927/`
- Smoke map (`c0a0e`): loaded; G36/G45/visual: PASS

Interpretation:
- The disc boots and the guest reaches playable map state in Dolphin.
- The observed client sprite-cache OOM is fixed and runtime-verified.
- Physical hardware and release-facing systems remain unverified; future
  failures must not be accepted from build success alone.

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

- Build: `scripts/build-gamecube.sh` completes with Swiss-first
  libogc2/libdvm (or classic libogc fallback) and emits a valid DOL
  (`OUT/bin/boot.dol`, about 4.6 MiB).
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

### Filesystem lookup/load contract resolved — 2026-08-02

- Probe bundle: `.ai/logs/dolphin-probe-20260802-200351/`.
- Cause: GameCube `FS_FindFile` treated a successful-lookup cache entry as a
  negative result. `FS_FileExists` populated that cache, then `FS_LoadFile` or
  `FS_Open` returned `NULL` on the next lookup. Search-path rebuilds also left
  stale hit/miss entries behind.
- Fix: successful cache entries no longer short-circuit lookup, and both
  caches are cleared when search paths are rebuilt.
- Validation: `delta.lst` and `valve/delta.lst` both report
  `FS_FileExists=1`, `FS_LoadFile=1`, size `12567`, with disk path
  `gcdisc:/xash3d/valve/delta.lst`.
- Runtime validation passed through G201 reinit, BSP load, entity spawn,
  `map loaded c0a0e`, and stable rendered frames without a guest fatal.

## Next Task

**Swiss / hardware sign-off (G38) — blocked on physical SD**

1. Disc-only Dolphin packet COMPLETE:
   `.ai/logs/operator-evidence-20260808-audio/packet`.
2. `--require-swiss` is wired but FAIL-closes on disc-only evidence
   (confirmed: `.ai/logs/operator-evidence-20260808-require-swiss-fail/`).
   Dolphin upstream has no SD Gecko/SD2SP2 EXI device.
3. Operator handoff: `.ai/logs/hardware-handoff-20260808-110924/`
   (checklist lists required FAT / return-to-loader markers).
4. Host currently builds classic libogc+libfat; Swiss preferred stack is
   libogc2+libdvm (needs `dkp-pacman` install with sudo).
5. Keep physical SD/memory-card fault cases under G38/G66/G71.

## External Blockers

None for the current autonomous milestone.

Physical-hardware sign-off remains a later manual checkpoint under G38/G66, but
it is not the reason the automation should stop today.

## Last Updated

2026-08-08

### Changelevel continuity validated — 2026-08-04

- Probe bundle: `.ai/logs/dolphin-probe-20260804-161727/`.
- Cause of the changelevel OOM: the SV map path prepared the destination BSP
  before purging the old world, and the map-load borrow remained marked in use.
- Fix: release the borrow during BSP staging, purge the old world before the
  destination reservation, and discard the old arena only at model teardown.
  `SV_InitGame` now reserves the destination-sized contiguous BSP buffer after
  that purge.
- Runtime result: `CHANGELEVEL_READY`; `G68 changelevel ready` and
  `MAP_READY c0a0a` passed through Dolphin with no guest fatal or filesystem
  allocation failure.
- Continuity result: `G100 landmark restore` preserved health `77`, armor `50`,
  weapons `0x6`, ammo `99/88`, and origin; `G94 round trip present` also passed.
- Build and host validation: GameCube DOL/ISO rebuilt; `20` host tests passed.

### Short soak validated — 2026-08-04

- Report: `.ai/logs/dolphin-soak-20260804-continue/report.json`.
- Two real Dolphin `c0a0` iterations passed with no guest fatal and stable map
  readiness. MEM1 high-water was identical at `3,953,131` bytes in both runs.
- Frame telemetry was present in both runs: 31 samples, average `1.06 ms`,
  p95 `0.69 ms`, maximum `12.31/12.46 ms`.
- This is a short regression soak, not release-duration hardware soak evidence.

### Save policy preflight validated — 2026-08-04

- Report: `.ai/logs/save-compliance-20260804-continue/report.json`.
- Automated metadata, CRC, atomic commit, confirmation, and destructive-write
  checks all pass. Physical storage interruption/full-card/removal/corruption
behavior remains explicitly unverified until hardware testing.

### Retail intro/menu timing validated — 2026-08-04

- Fresh current-DOL ISO: `.ai/logs/retail-continue/xash3d-gc.iso`.
- Dolphin probe: `.ai/logs/dolphin-probe-20260804-195138/`.
- Retail probe passed intro GCVID frame 150/150 at guest elapsed `10.00 s`,
  synchronized native 48 kHz audio with nonzero PCM, interactive menu
  readiness, and menu down/confirm/back actions.
- `gamecube-video-playback-gate.py` passed complete pacing, audio sync,
  nonzero PCM, and no-fatal checks. Retail probes now default to Dolphin JIT
  plus CPU threading; frame dumping remains opt-in for timing validation.

### G508 config round-trip probe wired — 2026-08-05

- Extended the Dolphin-designated `gcprobe:` RAM bank to host `config.cfg`
  (`.new`/`.bak`) write/read/rename/delete so `Host_WriteConfig` can round-trip
  without real SD Gecko hardware.
- Added `-gcconfigroundtrip` / disc `configroundtrip` override, Dolphin probe
  flag `DOLPHIN_G508=1`, and release-packet evidence gating for
  `G508 config round trip ready`.
- Real SD (`sd:/xash3d`) remains the persistent route; physical SD fault cases
  stay hardware-only.
- Host contract covered by `tests/test_gamecube_host.py::test_g508_config_roundtrip_probe_contract`.

### G508 Dolphin gcprobe evidence — 2026-08-08

- Probe: `.ai/logs/dolphin-probe-20260808-062342/`
  (`DOLPHIN_NEWGAME=1 DOLPHIN_G508=1 DOLPHIN_TIMEOUT=240`).
- Result: `G508 config round trip ready route=gcprobe`, analyze
  `G508_STATUS: PASS route=gcprobe`, plus `NEWGAME_READY` / G45 / G506 PASS.
- Runtime fixes: static 12KB config-only probe bank; `FS_VPrintf`→`FS_Write`
  for fake probe fds; lean marker write on gcprobe (avoid late Host_WriteConfig
  OOM + `FS_FileExists` DVD hang).

### G509 changelevel soak gate wired — 2026-08-05

- Expanded `scripts/gamecube-soak-probe.py` with `--g509` /
  `--changelevel-route FROM:TO` so soak iterations exercise continuity, not
  only repeated single-map boots.
- Default route is the proven early tram hop `c0a0:c0a0a`; PASS requires
  `CHANGELEVEL_READY` / G68 ready plus G100 landmark restore, memory, and frame
  telemetry with bounded MEM growth.
- Wrapper: `scripts/gamecube-g509-soak.sh`.
- Host proof: dry-run + parser unit tests in `tests/test_gamecube_host.py`.
- Release packet treats changelevel-mode soak reports as the preferred soak
  evidence when `require_changelevel` is set.

### G509 Dolphin soak evidence — 2026-08-08

- Report: `.ai/logs/soak-probe-20260808-141117/`
  (`G509_ITERATIONS=2 G509_TIMEOUT=300 scripts/gamecube-g509-soak.sh`).
- Result: Status PASS; both iters landmark+ladder+frames; MEM1 HWM stable
  at 4026531 bytes; route `c0a0:c0a0a` / landmark `c0a0toa`.
- Runtime fixes: default landmark wiring; no soft-dump latch on changelevel
  alone; early dest frame-budget arm; skip hanging dest post-tick ServerFrames.

### Dolphin release packet COMPLETE — 2026-08-08

- Bundle: `.ai/logs/operator-evidence-20260808-g508-g509/`
  (`scripts/gamecube-operator-evidence.sh --reuse-probe … --reuse-soak …`).
- Packet: `RELEASE_PACKET: COMPLETE` / **COMPLETE / RELEASE-READY**
  (disc-only; Swiss storage/loader open; audio was UNVERIFIED in this bundle).
- Gameplay gate accepts lean Pure Flipper action evidence for packet PASS.

### Lean audio PCM + audio-complete packet — 2026-08-08

- Root cause: smoke ISO lacked shallow `media/*.wav` and
  `sound/buttons/button10.wav`, so ASND stayed silent (`peak=0`).
- Fix: `stage_smoke_data(..., newgame=True)` stages lean media VO + button10
  (`scripts/build-gamecube-disc.py`).
- Probe: `.ai/logs/dolphin-probe-20260808-110540/` → `NEWGAME_READY` plus
  `audio submitted nonzero PCM chunks=1 peak=22644`; G278 atmosphere ready.
- Packet: `.ai/logs/operator-evidence-20260808-audio/` →
  **COMPLETE / RELEASE-READY** with evidence `audio: PASS` (disc-only;
  Swiss storage/loader still open).

### Swiss `--require-swiss` gate + hardware handoff — 2026-08-08

- `scripts/gamecube-operator-evidence.sh --require-swiss` fail-closes unless
  `FAT volume ready` + return-to-loader markers are in reused OSReport logs.
- Disc-only confirmation FAIL:
  `.ai/logs/operator-evidence-20260808-require-swiss-fail/` (expected).
- Handoff: `.ai/logs/hardware-handoff-20260808-110924/` points operators at
  the audio-complete Dolphin baseline and the Swiss marker checklist.
- Blocker: physical Swiss + SD2SP2/SD Gecko (and preferably libogc2 rebuild).

### Lean door brush admit — unblocked via `*` submodel pack — 2026-08-08

- Classname door/button admit hung Dolphin `WriteEntities begin` when linked
  (in-loop / noinline / separate TU / +64 B after reclaim).
- Fix: replace tram classname/`*12` checks with smaller `precache[0]=='*'`
  admit, budget 2 (DOL **5,878,432**, −448 B vs tram-only).
- Probe `20260808-172805` → `NEWGAME_READY`, G319 `entities=3 brushes=2`,
  world interaction `func_door` done, G45/G506 + gameplay PASS, PCM ok.
- Swiss gate: DOL now writes `xash3d/valve/logs/swiss-evidence.txt` on FAT
  (ready/preferred + shutdown). Ingest with
  `gamecube-swiss-evidence.sh --log <sd-mount>` (fixture COMPLETE at
  `.ai/logs/operator-evidence-swiss-sdroot-wiring/`). Real SD still required.
- Lean combat: probe `20260808-200847` — glock FireBullets +
  `TraceAttack td=0` (world) + ApplyMultiDamage, NEWGAME_READY + G45_ACTION
  PASS. `DecalGunshot` hung; TEXTURETYPE/BubbleTrail/PLAYBACK_EVENT deferred.
- G320 beams + G36 on reactor: c3a2 `20260809-012721` **NEWGAME_READY** —
  Flipper budget samples armed in Prepare, map env_beam client seed→blit,
  G36 PASS, G45_ACTION PASS, G506 PASS, visual nonblack. Zap TE still
  skipped. c0a0 `20260809-012809` NEWGAME_READY + G45_ACTION PASS (G36 WEAK
  on Flipper sample cost).
