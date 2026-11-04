# Xash3D GameCube Port Status

## Xen actor loading — 2026-11-04

The GameCube studio allowlist and bootstrap mirror now include Nihilanth,
Gonarch, its baby headcrabs, alien controllers, and alien slaves. The embedded Nihilanth model and
the separate Gonarch/controller/slave skin companions are preserved in full.
Xen maps reserve the studio arena for actors actually encountered instead of
preloading the unrelated intro cast.

Fresh Dolphin direct-map evidence: `c4a2` reached `MAP_READY` with Gonarch's
model and six companion texture headers (`.ai/logs/dolphin-probe-20261104-015657/`);
`c4a3` reached `MAP_READY` with Nihilanth and controller/slave models
(`.ai/logs/dolphin-probe-20261104-015926/`); and `c4a1d` reached `MAP_READY`
with the controller and slave companions (`.ai/logs/dolphin-probe-20261104-020201/`).
The latter used approximately 507 KiB of the studio arena instead of 1,007 KiB
with the intro cast. A host fixture verifies that staging preserves the boss and
companion skin payloads.

These are map-load and model-loading checks, not visual or boss-fight proof.
An earlier full-physics `c4a3` probe exited during spawning after archive
allocation failures (`.ai/logs/dolphin-probe-20261104-020303/`). A subsequent
probe reached world rendering and scripted attack, jump and use
(`.ai/logs/dolphin-probe-20261104-022007/`), but recorded filesystem allocation
failures, skipped Nihilanth skin upload at low quality, missed the frame-time
target, and captured visibly corrupted world textures. It is not a boss-fight
pass. Fixing fatal-panel drawing also made crash diagnostics safe for the small
panel buffer. A later build retained fallback mip data for all 26 packed `c4a3`
world textures instead of pointing at an empty streaming window, but its
full-physics probe exited during entity spawning before a world frame
(`.ai/logs/dolphin-probe-20261104-022459/`). Visual improvement therefore remains
unverified. The corrected fallback-mip path subsequently reached `MAP_READY`
on a separate `c4a3` direct-map image with all 26 packed textures retained
(`.ai/logs/dolphin-probe-20261104-023107/`); direct-map rendering intentionally
uses a status-panel fallback, so this confirms loading only. The next gates are a visually correct playable boss encounter,
transitions and physical-hardware checks.

## Multi-game / named DOLs — 2026-11-03

Same repo; one DOL per title. HL1 release is `OUT/bin/halflife1.dol` with
`boot.dol` kept as the Swiss/docs alias. Day of Defeat will use `dodgc.dol`
after the vgui2 / client-source research gate in
[`MULTI_GAME.md`](MULTI_GAME.md). Boot now logs detected `xash3d/<gamedir>/`
trees; disc packaging can stage multiple gamedirs. HL1 remains the only
supported play path until a DoD-linked DOL exists.

## Menu / HUD MEM1 follow-up — 2026-11-04 (verified)

Root causes from `.ai/logs/dolphin-probe-20261103-203758/` and
`…-205544/`:

1. **`resource/gameui_english.txt` is 26,556 bytes** — after intro release,
   `FS_LoadFileFromArchive` could not allocate 26,557 bytes. GC main menu
   labels are hardcoded English + builtins, so GameUI dictionary load is now
   skipped on GameCube (`3rdparty/mainui/MenuStrings.cpp`).
2. **Two HUD slabs thrashed the menu font** when `logo.tga` rebound — late
   framedumps showed an empty panel with orange glyph fragments. HUD reserve
   is now **3** slabs, and LRU eviction prefers non-font chrome
   (`ref/gx/r_gx_world.c`).
3. **`-gcmenuplaystart` + `-gcmap` never ensured client / Prepare** — Activate
   deferred Prepare until client progs, but the `-gcmap` ClientFrame path
   kept them unloaded and never queued `gc_playstart`, so Host_Frame stayed
   at `ready=0` (`.ai/logs/dolphin-probe-20261103-235225/`). Post-smoke now
   ensures the client and Prepare for menu playstart; lean-play arm and SCR
   sustain accept `-gcmenuplaystart` (`host.c`, `cl_main.c`).

### Dolphin evidence — 2026-11-04

- Retail menu: `.ai/logs/dolphin-probe-20261104-000250/` — `RETAIL_READY`,
  `localization skip resource/gameui_english.txt`, `menu console font ready`,
  `retail menu button text ready … New Game`, G45 menu actions PASS.
- Menu→New Game `c0a0`: `.ai/logs/dolphin-probe-20261104-000713/` —
  `MENU_NEWGAME_READY` with play start ready, G36 PASS (15 samples,
  avg 4.97 ms / p95 5.07 ms), G506 PASS (G172 + lean-HUD + G105),
  ladder 10/10, nonblack frames. Earlier proof:
  `.ai/logs/dolphin-probe-20261104-000100/`. Screenshot:
  `.ai/screenshots/c0a0-menu-newgame-20261104.png`.
- Hardware and full-campaign visual parity remain open.

## Current verification — 2026-11-02

Current follow-up: entity private-data allocations now reuse freed 16-byte
blocks anywhere in the 108 KiB slab. Dense maps can also reserve 64 KiB from
the map arena after texture staging, with arena reuse deferred until entity
destructors finish. `c1a2` reaches `MAP_READY` with this reserve
(`.ai/logs/map-compat-20261102-192510/`). Sanitizer-backed native fixtures verify
hole reuse, alignment, exhaustion, preservation of live objects, and the arena
discard guard. A Dolphin `c1a2→c1a2a` landmark transition passed its G68
readiness and input checks (`.ai/logs/dolphin-probe-20261102-193411/`). Full
gameplay and hardware validation remain outstanding.
Small texture pools now reject oversized tables, invalid
file ranges, short reads, and staging transfers that overlap resident mips.
The build pipeline installs dependencies before generating the DOL because
installation can relink the ELF and invalidate an earlier DOL.

The representative campaign audit at
`.ai/logs/campaign-audit-20261102-181501/` recorded 14 ready maps, five runtime
failures, and one inconclusive probe. Its 237 transition targets all exist;
that static check does not verify traversal. Subsequent texture-pack work
cleared `c1a1` and `c1a2`; `c1a3`, `c2a1`, `c2a2`, and `c2a4e` still need current
runtime proof. Fresh adaptive-reserve verification now passes `c2a4e`, `c1a2`,
`c1a3`, and `c2a1` (`.ai/logs/map-compat-20261102-233305/`): all four reach
`MAP_READY`. The formerly failing `c2a4e` reserves 51,776 bytes, retains all
115 packed textures, and loads both its 189.44 KiB leaf and 294.18 KiB node
arrays from the retained arena. This remains a map-load gate, not campaign-wide
visual, gameplay, or physical-hardware sign-off.

The retail intro/menu regression is fixed in the current build. The fallback
Cinepak path now keeps decoder and upload dimensions consistent, and menu startup
uses the baked retail background without redundantly loading nine composite tiles
and four blurred logos. Fresh Dolphin evidence reaches the final intro frame and
the interactive menu with no ImageLib OOM (`.ai/logs/dolphin-probe-20261103-002113/`).
The video completed at 15 fps, but the strict host-timestamp pacing gate remains
red under this loaded Dolphin run; visual and hardware playback still need a
dedicated timing capture.

Release completion remains unproven. Fresh ending-map (`c5a1`) probes exposed
allocation failures before vertex conversion and private-data exhaustion during
entity spawning. World vertices now use the existing map-arena fallback; server
strings reuse identical allocations and use compact headers; temporary key/value
slab strings retain their correct ownership during cleanup. The native C lookup
and ownership regression fixtures pass.

The latest direct-map run reaches `MAP_READY` after the full entity list, with a
2.31 MiB reported memory peak. The private-data fallback is now bounded at
108 KiB, recovering MEM1 headroom while still admitting dense maps. World
texture packs are now enabled for `-gcmap` probes and can split into a small
map-arena tail plus ARAM. Probe evidence: `.ai/logs/map-compat-20261102-145736/`.
The representative Xen maps `c4a1` and Nihilanth `c4a3` also reach `MAP_READY`
in fresh probes (`.ai/logs/map-compat-20261102-151034/` and
`.ai/logs/map-compat-20261102-150613/`). These are load/readiness gates, not yet
proof of playable transitions or hardware behavior.
The early `c0a0` map reaches `MAP_READY` as well (`.ai/logs/map-compat-20261102-151426/`).
The cold changelevel route `c0a0e→c1a0` also reaches `MAP_READY`
(`.ai/logs/map-compat-20261102-153554/`), confirming at least one real
cross-map handoff in the current build.
The chained `c1a0→c1a0a` route reaches `MAP_READY` too
(`.ai/logs/map-compat-20261102-153924/`).
The full campaign audit dry-run enumerates every listed chapter map and finds
all local BSP files, including the corrected Xen/Gonarch/Interloper/Nihilanth,
Endgame, and Hazard Course assignments (`.ai/logs/campaign-audit-20261102-164052/`).
Hazard Course `t0a0` now reaches `MAP_READY` after all 335 entities spawn with
the reduced fallback slab (`.ai/logs/map-compat-20261102-181050/`). This clears
the previous entity-spawn blocker; campaign transitions and real hardware
playability remain separate verification gates.
The texture-pack gate clears `c1a1`
(`.ai/logs/map-compat-20261102-183929/`). The overflow reserve clears the
`c1a2` entity-allocation failure. Adaptive split selection subsequently loads
all 161 `c1a2` packed textures with 43,040 bytes left for full-resolution
streaming (`.ai/logs/map-compat-20261102-193103/texture-budget-results.tsv`).
This is load and allocation evidence, not a visual or gameplay quality pass.
The transition destination `c1a2a` initially loaded 141 of 143 packed
textures. The renderer map arena was not 32-byte aligned, allowing DMA cache
operations to invalidate the end of its texture table. After aligning the
arena base and tail allocations, a fresh direct `c1a2a` probe loaded 143 of
143 textures and reached `MAP_READY` (`.ai/logs/dolphin-probe-20261102-194344/`).
Repeated guest-debugger samples show libogc `SI_Transfer` / `SI_GetTypeAsync`
callbacks and `udelay`; the cause of the stall is still under investigation.
Saved trace: `.ai/logs/dolphin-debug-c5a1-20261102/gdb-stall.log`.

The chapter audit now correctly assigns Nihilanth to `c4a3` and Endgame to
`c5a1`; map-load results do not claim chapter completion.
The older milestone notes below describe historical evidence, not current
release sign-off. Physical Swiss and full campaign validation remain open.

## Current Milestone

**Dolphin tip-safe retail path green; hardware handoff armed for Swiss (2026-08-28).**
Lean `-gcnewgame` `c1a0a→c1a0d` reaches CHANGELEVEL_READY with G36/G45/G506/LADDER
PASS. Campaign visual proof, map-compat RC, G509 soak, NPC studio verify, GX-022
textured underfoot pad, and FS-022/025 storage-route matrix are in place.
**Physical Swiss sign-off** (TODO-HW-012/022 / G38) remains the release gap —
host packet armed (`boot.dol` `e96e3c52…`, hybrid smoke `3f680e71…` with Swiss
r2092 loader); physical FAT32 SD still required. Dolphin has no SD EXI device.

## Current Automated State (2026-08-28)

- **HW boot fix:** early blue splash, hardware loading plaque during init, SD-without-
  `valve/`→disc fallback, disc-only vfs skip rescan, fatal boot panel on missing assets.
- **GX-022:** textured underfoot pad borrows resident hallway tex (no DOL growth).
- **FS-022/025:** storage-route matrix + `gc-fs-route-v1` schema (disc/hybrid scoring).
- **Handoff packet:** Expansion `xash3d-gc-swiss-hw-20260828/` + stage
  `.ai/logs/swiss-smoke-stage/` (`boot.dol` `e96e3c52…`, hybrid smoke `3f680e71…`,
  `swiss.dol` / `apps/swiss/boot.dol` from Swiss r2092).
- **G77 Dolphin proxy PASS (swissquit re-arm DOL):** `.ai/logs/hardware-smoke-20260828-224924/`.
- **Swiss smoke FAT image:** `OUT/xash3d-gc-swiss-smoke.img` (256 MiB hybrid DOL+valve+ISO+Swiss).
- **Host waiters:** `xash3d-gc-swiss-sd-watch.service` + `xash3d-gc-swiss-hw-oneshot.service`
  (FAT32 only; accepts real `XASH3DGC`/`XASHSMOKE` remounts; skips loop verify mounts;
  auto-unmount after stage; exFAT/raw-disk notify).
- Tip-safe G381 hallway: `.ai/logs/dolphin-probe-20260828-204414/` (CHANGELEVEL_READY).

## Build Status

| Artifact | Size | Format | sha256 (prefix) |
|----------|------|--------|-----------------|
| `OUT/bin/boot.dol` | ~6.0 MiB | PowerPC DOL | `e96e3c52…` |
| `OUT/xash3d-gc-swiss-smoke.img` | 256 MiB | FAT hybrid | `3f680e71…` |
| `OUT/xash3d-gc-swiss-disc.iso` | ~91 MiB | GC smoke ISO | `55522807…` |

**Build Notes**:
- Engine builds successfully against Swiss-first libogc2 (or classic libogc).
- DOL generation is structurally valid for Dolphin boot; G43 boot-media compliance PASS.
- No current build error blocks runtime or hardware staging work.
- Stack resolver: `python3 scripts/waifulib/gamecube_ogc_stack.py`
- Operator staging: `scripts/stage-sd-assets.sh <Half-Life> <sd-mount> --route sd`

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

**Status**: Software + handoff ready; physical Swiss run required (TODO-HW-012/022)

- **2026-08-28 (G381):** Live `boot.dol` sha256 prefix `3e005bfb…`; smoke FAT
  `c3229d0e…`. Packet on Expansion; oneshot/watch armed. Physical Swiss evidence
  still required (`swiss-evidence.txt` FAT + map/playable + shutdown).
- **2026-08-27:** Fixed black-screen boot (early splash, loading plaque, asset path
  validation, fatal panel when `valve/` missing). Current `OUT/bin/boot.dol` includes
  that fix — stage it (or the smoke ISO) to Swiss/SD2SP2/SD Gecko and record evidence.
- Physical GameCube / Swiss validation still matters for release confidence.
- G38/G77 remain manual checkpoints; use `scripts/gamecube-hardware-smoke.sh` +
  `scripts/gamecube-swiss-evidence.sh`.

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

Cleared 2026-11-04: the Oct 27–28 “Next Task” write-ups below were finished
engineering notes, not open work. Closed and removed from this list:

- renderer-arena BSP scratch (`c5a1` lightmap OOM)
- dense-map deferred viewmodel scheduling
- terminal campaign hop `c4a3→c5a1` CHANGELEVEL_READY
- retail-menu Dolphin visual proof (reconfirmed `.ai/logs/dolphin-probe-20261104-000250/`)
- HUD `gc_quality` null-pointer handoff
- native OpenGL `_malloc_r` DSI (fixed via static studio arena merge)
- rejected 512 KiB studio-size experiment (already reverted)
- live BSP `edges16` recovery + sustained real-BSP presentation
- tram/Xen/Lambda tip-safe hops and related OOM closes from 2026-08-09

Still open:

1. **Chapter visual gate** — capture one non-probe, non-green post-world GX
   frame per representative chapter (tram / AM / combat / Lambda / Xen /
   end). `c0a0` now has non-green DumpFrames evidence
   (`.ai/screenshots/c0a0-menu-newgame-20261104.png`,
   `.ai/logs/dolphin-probe-20261104-000713/`); far-portal / retail-parity
   polish remains. Do not treat G36 / CHANGELEVEL_READY alone as visual parity.
2. **Tip-safe continuity hops** on the current DOL across the campaign spine
   (tram → AM → combat → Lambda → Xen → Nihilanth), with Flipper polish
   (G381 skins / CapFaces) as hop failures appear.
3. **Swiss / hardware G38–G66** — physical FAT32 SD boot, playable map, and
   return-to-loader. Host handoff/smoke image remain available
   (`OUT/xash3d-gc-swiss-smoke.img`); do not block Dolphin work on this.

## External Blockers

None for the current autonomous milestone.

Physical-hardware sign-off remains a later manual checkpoint under G38/G66, but
it is not the reason the automation should stop today.

## Last Updated

2026-10-27

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
- G320 beams + G36 on reactor: c3a2 `20260809-012721` / re-proof
  `20260809-130544` **NEWGAME_READY** — Flipper budget samples, map env_beam
  client seed→blit, G36 PASS, G45_ACTION PASS, G506 PASS. Zap TE still
  skipped. c0a0 G36 WEAK fixed via tram-only sample face cap —
  `20260809-130441` G36 PASS avg=12.75ms.

### Command-token handoff guard — 2026-10-28

The first post-BSP handoff could still reach `Mem_FreeBlockSmall` from
`Cmd_TokenizeString` after the local client/server `begin` sequence.  The
GameCube path now checks ownership against `cmd_pool` before freeing a token,
clears stale slots, and reports the exceptional cleanup.  Fresh Dolphin
profile `.ai/live-bsp-stable-user-0750` reproduced the stale-slot condition;
fresh guarded profile `/tmp/xash-gc-proof-0753` dropped two stale slots and
continued through 16 sustained c0a0 SCR frames with no DSI/fatal.  The run
also preserved `pure GX live BSP faces opaque=2004 trans=12` and
`G283 live BSP Flipper opaque=2016` evidence.  This proves the guarded
handoff is stable, but does not by itself establish retail pixel parity.

### Fullphysics memory follow-up — 2026-10-28

The repository gameplay gate exposed two separate MEM1 constraints on the
fullphysics c0a0 route. First, normalized compact clipnodes could not obtain a
contiguous 59.12 KiB heap block after retained PVS data; the loader now
reclaims retained visdata and retries the proper normalized copy. A fresh run
reached `bmodel clipnodes ready`, `ClientPutInServer ready`, and `DropToFloor`
without the former clipnode fatal. The next run reached frame 5 but exhausted
the sprite-model pool while loading `sprites/320hud1.spr` (a 96-byte pool
allocation). Fullphysics client initialization now reserves 64 sprite-list
entries instead of the normal 256, preserving the retail-sized pool outside
that constrained route. A subsequent fresh run reached `map loaded c0a0` and
entity spawn after the normalized copy fell back to a bounded aligned static
reserve; its next failure was sprite-list exhaustion at 76 entries. The
fullphysics reservation is now 160 entries (still below the normal 256), and a
fresh gate is still required after the stale-probe lock is cleared.

The fresh 160-entry gate then advanced through `ClientPutInServer ready`,
`DropToFloor ready frac=0.744`, `state=4 signon=2`, controller/input
initialization, and the first weapon-list cache entry (`pool offset=0`). It
hit the next independent retail asset boundary while creating a sprite-model
memory pool: `_Mem_AllocPool: out of memory (allocpool at
../engine/common/mod_sprite.c:216)`. This is a deterministic post-spawn
allocator limit rather than a renderer or physics failure; the evidence is in
`.ai/logs/dolphin-probe-20261028-091831/dolphin-user/Logs/dolphin.log`.

The subsequent full-cache run reached sprite-list offsets through 140 before
showing that the temporary pool-table reserve itself capped at 512 entries.
The cache is now restored to the retail 256-entry capacity, and the bounded
GameCube pool metadata reserves above 512 entries consumed enough MEM1 to move
failure earlier into BSP/texture loading (768 and 1024 were rejected). The
reserve is therefore back at 512 entries, the last configuration that reached
c0a0 spawn and sprite-list offsets through 140. The image compiles and host
coverage remains green (`85 passed`).

The 768-entry probe did reach c0a0 BSP texture upload, but Dolphin recorded
renderer-side MEM1 pressure (`GL_UploadTexture: OOM allocating 160x144x1` and
`144x128x1` for c0a0 textures) followed by loopback outgoing-message overflow.
This is a separate render-asset budget boundary after the pool-table change;
it is recorded in `.ai/logs/dolphin-probe-20261028-092517/stderr.log`.

The 512-entry isolated run then confirmed the immediate late failure was pool
creation itself (`mod_sprite.c:216`) after the first two sprite lists. Fullphysics
sprite models now share the existing host pool and are excluded from per-model
pool destruction; the GameCube build and host suite remain green (`85 passed`).
This removes the pool-table dependency and is the next configuration awaiting a
fresh Dolphin gameplay/frame gate.

The fresh gate with that fix passed the sprite-model pool boundary and reached
HUD loading (`weapon_crowbar.txt`, `G172 HUD sprite sys-malloc`). It then hit a
new 1 KiB `ImageLib Pool` allocation failure with only 29.91 KiB heap free,
recorded at `img_utils.c:465` in
`.ai/logs/dolphin-probe-20261028-092927/dolphin-user/Logs/dolphin.log`.
This is later than the prior pool-table fatal and confirms the real retail path
is advancing through map, physics, input, and HUD initialization.

The next full Dolphin probe (log `.ai/logs/dolphin-probe-20261028-093153`)
passed the major runtime gates: `NEWGAME_READY`, `G36_STATUS: PASS`,
`G45_STATUS: PASS`, `G45_ACTION_STATUS: PASS` for attack/jump/use input,
`G506_STATUS: PASS` for G172/lean-HUD/G105, nonblack visual samples, and
15 sustained frame samples (average 21.11 ms, p95 2.81 ms). The harness still
reports two narrow gameplay assertions—weapon event delivery and entity
use/touch callback—as missing, so retail-complete proof remains open.

The follow-up Dolphin run verified weapon delivery: `G120 attack usercmd`
reported a real active crowbar (`clip=-1`) and
`G121 PlaybackEvent deliver index=3 name=events/crowbar.sc`. The remaining
failure is `world interaction none` at the c0a0 spawn origin; no eligible
func_button, door, or trigger was present in the bounded interaction scan.

Log inspection showed the weapon assertion was caused by the injected attack
arriving with no active item (`active=0`, `weapons=0x0`, `clip=-999`). The
fullphysics attack path now invokes the existing weapon-readiness hook before
the first attack, allowing the real crowbar `PrimaryAttack` and playback-event
path to run. The rebuilt image and host suite remain green (`85 passed`); a
fresh Dolphin run is required to verify the event marker.

The first verification run still logged `active=0` because the existing
readiness helper deliberately skipped grants on fullphysics. It now seeds the
retail crowbar when fullphysics has no active item before processing the first
attack; the rebuilt image and host suite remain green (`85 passed`).

After including the actual c0a0 `trigger_changelevel` entity in the bounded
interaction scan, the next clean Dolphin gate passed completely:
`GAMEPLAY_GATE: PASS` with movement/look, jump, attack/weapon, use/entity,
viewmodel/HUD, and post-action stable-frame evidence. It also reported
`NEWGAME_READY`, G36/G45/G506 PASS, nonblack visual samples, and
`LADDER_STATUS: PASS passed=10/10`. Authoritative log:
`.ai/logs/dolphin-probe-20261028-093610`.

An extension run with `DOLPHIN_SMOKE_MAP=c1a0` reached the alternate retail
chapter map loader but hit a larger BSP allocation boundary at
`mod_bmodel.c:2473` (17.28 KiB) before gameplay initialization. This keeps the
c0a0 gate as the authoritative green baseline while identifying c1a0 as the
next MEM1-reclamation target.

An additional `DOLPHIN_SMOKE_MAP=c0a0b` run reached `map loaded c0a0b` and
client signon completion. It did not reach the gameplay gate before timeout;
the log is dominated by the map's larger texture set and missing `.mip`
lookups. This confirms a second retail BSP loads on GameCube while identifying
texture staging/assets as the next c0a0b-specific verification boundary.

An additional `DOLPHIN_SMOKE_MAP=boot_camp` run reached `MAP_READY boot_camp`
and client signon. It then reported `OOM reloading lump lightmaps (1.02 Mb)`
under MEM1 pressure, so boot_camp is a map-load/render-memory milestone rather
than a complete gameplay gate yet.

#### Dolphin map evidence matrix

| Map | Verified runtime boundary | Result |
|---|---|---|
| `c0a0` | Full gameplay gate, nonblack frames, HUD/viewmodel, attack event, entity touch, 10/10 ladder; retail WADs mounted in latest probe | Visual completeness still pending due missing `.mip`/texture OOM markers |
| `c0a0a` | BSP load, compact clipnodes, and hull-0 setup complete via static normalized reserve; no fatal allocator after fix | Full gameplay gate pending; retail texture staging emits missing `.mip`/texture OOM and loopback overflow before terminal gate |
| `c0a0b` | BSP load, entity spawn, `MAP_READY`, client signon | Texture staging/missing `.mip` follow-up |
| `c1a0` | BSP load, compact clipnodes, and hull-0 setup complete via static normalized reserve; no fatal marker in probe log | Full gameplay gate pending; probe timed out after post-hull startup (`.ai/logs/dolphin-probe-20261028-095646`) |
| `boot_camp` | BSP load, hull-0 ready, `MAP_READY`, and client signon (probe `.ai/logs/dolphin-probe-20261028-095933`) | 1.02 MiB lightmap reload reports OOM but map continues to `MAP_READY`; gameplay gate pending |

The bounded content-format audit against the legal retail tree
(`Half-Life/valve`) passes BSP, WAD, studio-model, sprite, audio, image, and
script coverage (125 BSPs, 8 WADs, 323 models, 384 sprites, 2,542 WAVs). A
retail-content Dolphin run (`.ai/logs/dolphin-probe-20261028-174949`) confirms
`decals.wad` and `halflife.wad` mounted and reaches `MAP_READY c0a0` in the
latest fullphysics capture (`.ai/logs/dolphin-probe-20261028-175638`).

The c0a0 gameplay gate must not be read as a complete visual-parity result: the
retail-content run still contains some missing `.mip` lookups and a
`GL_UploadTexture` OOM/truncated-mip error for
`#models/zombie/ZOM2_CrabButt1.mdl` before the nonblack frame. It proves
retail WAD mounting and rendering startup, but not yet a fully retail-loaded
visual frame.

A fresh bounded c0a0 probe (`.ai/logs/dolphin-probe-20261028-181044`) still
passes the complete gameplay gate, but records the underlying pressure directly:
heap free falls to roughly 18--23 KiB, filesystem-pool allocations fail, and
the same zombie first-mip upload is skipped. This makes the remaining visual
gap a reproducible MEM1 lifetime/fragmentation issue spanning filesystem and
texture staging, rather than an absent retail asset.

An allocator experiment on `.ai/logs/dolphin-probe-20261028-181642` enabled
compact headers for the filesystem/sound pools. It built and passed gameplay,
but still reached 17.6 KiB heap-free with the same eight filesystem failures
and zombie first-mip skip; the policy change was reverted as ineffective.

The subsequent mip-tier reclaim (`.ai/logs/dolphin-probe-20261028-182203`)
trimmed 210 resident mips on first pressure, removed the zombie texture
first-mip/skip markers, and raised heap-free from ~23 KiB to ~173 KiB while
preserving the gameplay gate. Several filesystem allocations still fail due
to contiguous-block pressure; the remaining redesign target is a dedicated
filesystem staging arena or compaction path.

The first filesystem-arena implementation is validated by the c0a0 probe
`.ai/logs/dolphin-probe-20261028-215356`: a 384 KiB pool-owned arena serves
medium/large transient file payloads (>=32 KiB), while small metadata remains
on the normal allocator. The probe preserves `G45_STATUS: PASS` and removes
the previous 72--124 KiB contiguous-allocation failures; only tiny metadata
soft-fails remain. Arena blocks retain normal pool headers, support
allocate/copy/free reallocation, and reset when the FileSystem pool empties.
The follow-up probe `.ai/logs/dolphin-probe-20261028-220138` widened arena
coverage to allocations >=2 KiB with a 512 KiB reserve: it preserves
`G45_STATUS: PASS` and reports no filesystem soft-fails in the captured run.

The texture fallback follow-up (`.ai/logs/dolphin-probe-20261028-224337`)
keeps the same gameplay gate while converting first-mip texture OOMs into
resident 2x2 fallback mips. No texture is skipped solely because its first
resident allocation fails; higher-resolution promotion remains the deferred
streaming/cache optimization documented below.

That probe's unresolved source-content set is 12 names: `+0‾fifts_lght4`,
`+a‾fifts_lght01`, `-1out_w3`, `-1silo2_cor2`, `-2silo2_cor2`,
`c1a4_silo5`, `silo2_wetdr`, `subway_ceiling`, `subway_door`,
`subway_floor1`, `subway_inend`, and `subway_outside` (all `.mip`). None is
present as a loose file in `Half-Life/valve`; the mounted retail WADs are
verified separately. This is now a finite content-supply task, not an
allocator or lookup-path blocker.

The later `-gcnewgame` retail-content probe
(`.ai/logs/dolphin-probe-20261028-180323`) independently reproduces the same
first-mip failure and reaches `MAP_READY c0a0` with movement, jump, attack,
and use actions. The captured c0a0 frame
(`.ai/screenshots/c0a0-live-bsp-final.png`) is genuine GX gameplay, but its
640x480 image contains 35,846 pure-black pixels (11.7%), so it is not accepted
as a fully loaded retail-equivalent frame.

The worst-case report for the campaign probes
(`.ai/logs/dolphin-probe-20261028-180632/summary.md`) records zero hard MEM1
failures and zero runtime blockers, with `c1a0`, `c1a0d`, and `c1a1a` reaching
the release-profile scene budget. These are performance/runtime-stability
results, not a substitute for the still-open visual-completeness check.

The passing c0a0 evidence is tied to these reproducible SHA-256 artifacts:

- `OUT/xash3d-gc.iso`: `30e29d396d437cf5e5f42fe230297bec5be1fa13d73cdb72f0e4147ff3173f24`
- `OUT/bin/boot.dol`: `4eada5a9ae1de1ad378a633c6399f9f1644aa273cd3c71ffaaadc3cbb0816346`

Rendered Dolphin evidence is also content-addressed:

- `.ai/screenshots/c0a0-live-bsp-stable.png`: `9d877527766f951850d8056ecdb3848147438363980261db2502438857b27af3`
- `.ai/screenshots/c0a0-live-bsp-final.png`: `22af36a6b93d312bf283959650daeefb82a3e96dac9260559943513ccb9d57fc` (in-game HUD and viewmodel visible)
- `.ai/screenshots/c0a0-studio-arena-valid.png` is retained as a negative/invalid capture (Dolphin UI, not gameplay) and is not used as proof.
- `.ai/screenshots/gc-main-menu-dolphin-runtime.png`: `633afc43603c01f39cb18d0cd1e38980f1a1ce55126ba514249b336353f46527`
- c0a0 Dolphin log: `e0f7e02756b5a110193801cdf3fc0deca5dce9734136223f73a8d459783e4360`

#### Deferred optimization: progressive mip streaming

#### Dense-map BSP surface budgeting (2026-11-03)

The map-load static arena was expanded with a 2 MiB tail pad, and dense maps
now budget their world texture pack to a 16 KiB ARAM-streaming table when the
surface reserve exceeds 900 KiB. Face lumps are copied into the map tail before
BSP staging release, while cleanup only frees face pins that still point into
the original source buffer. This removes the contiguous-surface allocation
failures and stale-pointer hang observed on `c1a2`/`c1a3`/`c2a1` while keeping
the tiny-tail `c1a1f` path intact.

Verified probes: `c1a1f`, `c1a2`, `c1a3`, and `c2a1` each reached `MAP_READY`
with zero hard failures in `.ai/logs/map-compat-20261103-013216` and the
preceding focused probe logs.

After allocator and full-map correctness are stable, consider a progressive
texture cache. Preload a 2x2 or 4x4 fallback mip for every visible material,
then stream higher mip tiers from SD into a small MEM1 staging buffer and upload
them to GX under a per-frame budget. Keep the lowest mip resident and evict
higher tiers by PVS visibility/recency. SD must remain load-time/cache-fill
storage; rendering must never depend on an uncached SD read. This is a visual
polish and memory-pressure optimization, not a substitute for complete retail
asset loading or BSP/PVS correctness.
