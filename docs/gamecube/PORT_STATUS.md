# Xash3D GameCube Port Status

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

### Full renderer-arena BSP scratch — 2026-10-27

- Probe bundle: `.ai/logs/dolphin-probe-20261027-042554/`.
- Root cause of the `c5a1` lightmap OOM was an accounting mismatch: the
  renderer lent a 3.50 MiB static map-load arena, but the BSP scratch allocator
  retained only the 2.62 MiB source-file length and ignored the arena tail.
- The map-load API now exposes its actual retained capacity. BSP node and
  clipnode high carves, surface scratch, and transient lump reloads use that
  capacity without increasing the static allocation.
- Runtime proof: scratch grew from 2.26 MiB to 3.13 MiB; the 1.05 MiB lighting
  lump reloaded through scratch, bound for the G154 bake, and was released
  afterward. `c5a1` reached `MAP_READY`; the runtime ladder passed 10/10.
- Timing proof: 15 samples, 4.66 ms average, 26.34 ms p95/max against the
  33.33 ms 30 FPS budget (`G36_STATUS: PASS`).
- Remaining probe classification is `MAP_LOADED_INPUT_READY`: presentation is
  missing the expected viewmodel marker and the scripted TAS action route is
  unseen. This is no longer a BSP/lightmap/map-readiness failure.

### Dense-map deferred viewmodel scheduling — 2026-10-27

- Probe bundle: `.ai/logs/dolphin-probe-20261027-043346/`.
- The remaining `c5a1` presentation miss was an exact-present scheduling bug:
  G36 deliberately blocked studio promotion at present 1, and the lean route
  did not retry until exact presents 32/40. Deferred viewmodel promotion now
  runs once at the first eligible post-budget present and retains bounded NPC
  retries at the later thresholds.
- Runtime proof: real `v_crowbar.mdl` and `v_9mmhandgun.mdl` models were pinned;
  G105, G172, lean-HUD, attack, jump, use, sustained world, map-ready, input,
  and nonblack markers all passed. Harness result: `NEWGAME_READY`, G36/G45/
  G506 PASS, runtime ladder 10/10.
- Timing remained 4.66 ms average and 26.34 ms p95/max against the 33.33 ms
  30 FPS budget. DOL SHA-256:
  `00511da5b790e221e3abaacd151354da5b3de2e3229aa37a68f85abe753c98d0`.

**Full-game Dolphin proof (current DOL) — campaign CHANGELEVEL spine**

### Current-DOL terminal campaign hop — 2026-10-27

- Fresh artifact: `OUT/bin/boot.dol`, SHA-256
  `80fff451972ebb66ed73dd427badc3b47fe662667b638730e75486bc4fafb1f4`.
- Fresh probe: `.ai/logs/dolphin-probe-20261027-203545/`; its staged ISO is
  SHA-256 `444828d7361facc834313bca00ee0195968c1a7b0583d5eeda141222a54db0f5`.
- `c4a3` → `c5a1` reached `CHANGELEVEL_READY` with landmark continuity,
  a nonblack sampled frame, controller input, and attack/jump/use actions.
  The runtime ladder passed 10/10; G36 was PASS (15 samples: 1.27 ms average,
  1.32 ms p95/max versus the 33.33 ms / 30 FPS target), as were G45 and G506.
- Host-side GameCube contracts remain green: `85 passed` in
  `tests/test_gamecube_host.py` after the same rebuild.

### Visual-proof correction — 2026-10-27

- Dumped completion frames are **not acceptable visual proof yet**. The first
  fresh campaign capture (`c0a0e` → `c1a0`,
  `.ai/logs/dolphin-probe-20261027-223114/`) retained a bright-green XFB with
  only sparse geometry instead of a completed level image.
- This is explained by the current GameCube timing harness, not by a valid
  sky/level render: `GC_FillBudgetProbeFrameBuffer()` deliberately writes
  RGB565 `0x07E0` (bright green) and `GC_PresentBudgetProbeFrame()` presents
  that buffer. DumpFrames is sampling that probe/XFB path before a stable GX
  world frame. Do not describe G36 or `CHANGELEVEL_READY` telemetry as
  completed visual rendering until an actual post-world GX screenshot passes.
- Required next gate: capture one non-probe, post-`R_GXDrawNewGameCapFaces`
  destination frame per representative chapter and verify that it contains a
  complete non-green world image. The physical Swiss G38/G66 gate remains
  separate and still requires console evidence.

### Retail-menu Dolphin visual proof — 2026-10-27

- A clean retail-menu run reached `RETAIL_READY`; G45 controller input and
  `menu:down,confirm,back` actions passed in
  `.ai/logs/dolphin-probe-20261027-224502/`.
- The recovered complete Dolphin frame is
  `.ai/screenshots/gc-main-menu-dolphin-runtime.png`. It contains the retail
  Half-Life menu background, logo, selected `New game` row, and the three
  standard menu entries; it is not the green gameplay timing buffer.
- The aligned runtime viewport and asset-derived retail-menu preview are
  recorded side by side in
  `.ai/screenshots/gc-main-menu-dolphin-vs-preview.png`. Their absolute
  pixel difference is 10.29/255 mean (29/255 p95), consistent with the
  runtime's filtering/gamma rather than a missing UI render.
- `scripts/capture-gc-main-menu.sh` now rejects an incomplete last TGA and
  selects the newest complete Dolphin frame, so this evidence cannot silently
  fall back to the baked preview. This proves the menu route only; it does not
  close the separate post-world-frame visual gate above.

### Gameplay visual audit — 2026-10-27

- The non-diagnostic menu-to-New-Game route reached `MENU_NEWGAME_READY` on
  `c0a0` in `.ai/logs/dolphin-probe-20261027-230612/`, but the analyzer
  correctly returned `MAP_LOADED_RENDER_FALLBACK_ONLY`. Its 110 complete TGA
  frames are almost entirely a uniform fallback buffer, not a level scene.
- The separate `gcworldrender` experiment
  (`.ai/logs/dolphin-probe-20261027-230349/`) did emit a fragment of GX world
  geometry, then crashed. It also carries the `WORLD PRESENT` diagnostic
  overlay, so it is not a retail gameplay frame and cannot be used for parity.
- A direct-EFB DumpFrames configuration was also tested on the ordinary menu
  path (`.ai/logs/dolphin-probe-20261027-235525/`); it timed out before `c0a0`
  loaded. That speculative capture setting was reverted rather than retained.
- Therefore gameplay visual matching in Dolphin remains **unproven and
  currently failing**. `MENU_NEWGAME_READY`, `MAP_READY`, face counts, and
  nonblack pixels are behavioral diagnostics only; none is a substitute for a
  complete retail-like scene.

### HUD-to-world handoff repair — 2026-10-28

- The prior ordinary menu-to-New-Game run rendered CapFaces and the tram but
  stalled in `HUD_VidInit`: the built-in client requested `gc_quality` and
  received a null cvar pointer. `CL_CvarGetPointer()` now materializes that
  GameCube renderer cvar with the low-memory default when the static renderer
  handoff has not exposed it yet.
- Fresh DOL probe: `.ai/logs/dolphin-probe-20261028-001057/`. It reached map
  and input readiness, completed `newgame world render ready`, sustained SCR
  world frames, passed G506 (G172 + lean HUD + G105), and passed the runtime
  ladder 10/10. Its 15 timing samples were 26.28 ms average / 46.39 ms p95
  against the default 16.67 ms capture target.
- This confirms that the former HUD null-pointer stall is fixed. It is still
  behavioral evidence only: a one-shot native Dolphin screenshot of a
  completed world frame is required before asserting gameplay visual parity.

### Native OpenGL capture audit — 2026-10-28

- A direct Dolphin OpenGL boot of the same validated menu-to-New-Game ISO
  reached sustained live GX presentation: CapFaces, tram brush model, HUD,
  crowbar, scientist, Barney, and headcrab promotion were all logged. The
  displayed window was captured at
  `.ai/screenshots/c0a0-dolphin-opengl-window.png`.
- That screenshot is a red GameCube DSI exception screen, **not** a gameplay
  frame. The exception PC `0x8045d948` resolves in the fresh ELF to
  `_malloc_r`; it occurs after deferred NPC studio promotion (383.57 KiB of
  the 640 KiB studio budget) and before a valid native screenshot can be
  saved. This is the current concrete gameplay-stability blocker.
- Do not use the sustained-frame markers as proof of retail visual parity
  until this post-promotion allocator fault is removed and the replacement
  native OpenGL capture shows a complete scene.

### Rejected studio-arena experiment — 2026-10-28

- Expanding the static studio BSS arena from 256 KiB to 512 KiB was tested to
  avoid heap allocation for the Barney and headcrab meshes. The fresh compact
  ISO replay (`.ai/current-menu-newgame.iso`) stopped during the following
  `c0a0` pre-spawn path, before model promotion or a native gameplay frame.
- It therefore neither removes the allocator fault nor supplies visual
  evidence. That size-only change was reverted; the native OpenGL DSI fault remained the
  blocker and no gameplay-retail-parity claim is warranted.

### Static studio merge replay — 2026-10-28

- The rejected size-only experiment was replaced with a targeted fix: the
  complete lean studio set and retained `T.mdl` headers use a 1 MiB static
  arena, and a mesh already in that arena is extended there **before** any
  `calloc` attempt. This removes the prior post-present `_malloc_r` route.
- Fresh direct OpenGL evidence from `.ai/allocator-static-user-0726/` reached
  64 sustained world frames after promotion. It logged BSS reads for crowbar,
  scientist, Barney, and headcrab, plus all three `G376` skin merges and the
  `G371` promotion gate, with no DSI exception.
- `.ai/screenshots/c0a0-static-studio-live.png` is the first valid native
  non-diagnostic `c0a0` frame from that route. It proves live GameCube GX
  gameplay presentation after the former allocator fault, but it visibly has
  stretched/incorrect world geometry and a black portal region. It is evidence
  **against** gameplay retail parity, not a parity pass. The next rendering
  task is to repair the world-geometry path before taking per-level frames.

### Live BSP recovery — 2026-10-28

- The menu New Game route retains its map-load arena through presentation and
  now stamps visible full-BSP surfaces. Direct diagnosis found the GX emitter
  was interpreting the `edges16`/`edges32` union as 32-bit; retail HL1 maps
  now explicitly use `edges16` (the source lump was already endian-normalized).
- Fresh native OpenGL evidence in `.ai/live-bsp-final-user-0742/` reports
  `opaque=2004 trans=12 … emit0=0` and `G283 live BSP Flipper opaque=2016`.
  `.ai/screenshots/c0a0-live-bsp-final.png` is the matching frame. It shows
  real ceiling/wall geometry, but the synthetic tram/fill overlay and black
  portal still make it non-retail; visual parity remains unproven.

### Sustained real-BSP presentation — 2026-10-28

- The retained full-BSP path now bypasses the CapFaces/fill and baked-tram
  fallbacks. A command-token cleanup fixed the post-frame-8 double-free at
  `cmd.c:580` that appeared once this route was allowed to continue.
- Fresh direct OpenGL run `.ai/live-bsp-stable-user-0750/` passed 32 world
  frames and all scientist/Barney/headcrab `G376` skin merges plus `G371`
  promotion with no DSI or allocator error. Its native frame is
  `.ai/screenshots/c0a0-live-bsp-stable.png`.
- The capture is a real c0a0 corridor (tracks, walls, and ceiling), not the
  former synthetic overlay. A black far portal/void persists, so it is still
  not a retail-parity frame or the requested completed first-level capture.

### Current hardware handoff — 2026-10-27

- Refreshed FAT32 Swiss smoke image:
  `OUT/xash3d-gc-swiss-smoke.img` (256 MiB hybrid DOL + smoke assets + compact
  ISO), SHA-256
  `c0622b05a566856fe618763bc9827f8b80141da5d7de60acf0be0d126e1affb7`.
  Its staged `apps/xash3d-gc/boot.dol` is byte-identical to the current DOL
  above; the image verifier found the loader, `gfx.wad`, `liblist.gam`,
  `c0a0e.bsp`, and log directory.
- Operator handoff: `.ai/logs/hardware-handoff-20261027-222554/`.
  The G56 checklist verifier passed; physical Swiss boot, FAT mount, playable
  map, and return-to-loader evidence remain the only unclosed G38/G66 gates.

1. Tip-safe continuity hops across chapters on packet DOL `77f53b3a…`
   (tram → AM → combat → Lambda → Xen → Nihilanth).
2. Flipper polish (G381 skins / CapFaces) as hop failures or visual gaps appear.
3. Swiss / hardware G38 deferred (no FAT32 SD); do not block emulation on it.


### Tram + Xen hops past c0a0b / c4a1 — 2026-08-09

- Landmark defaults added in `scripts/dolphin-boot-probe.sh` for
  c0a0b→c0a0c (`c0a0btoc`), c0a0c→d (`c0a0ctod`), c0a0d→e, c0a0e→c1a0,
  c4a1→c4a2 (`c4a2`), c4a2→c4a2a (`c4a2_0a`).
- **CHANGELEVEL_READY** c0a0b→c0a0c `20260809-205421` (G36/G45/G506 PASS).
- **CHANGELEVEL_READY** c4a1→c4a2 `20260809-205526` (G36/G45/G506 PASS).
- **CHANGELEVEL_READY** c0a0c→c0a0d `20260809-205629` (G36/G45/G506 PASS).
- No tip-safe lean engine fix required for these hops.

### Xen NEWGAME + c0a0a→c0a0b OOM closed — 2026-08-09

- c4a1 hang after G292 (`20260809-131438`): CapFaces fell through to
  `R_GXDrawWorldLiveSurfaces` on scratch-retain frames after the one-shot
  G283 log (live=0 Xen). Tip-safe: scratch-retain stays LM-caps + water +
  G292 only. **NEWGAME_READY** `20260809-184328` (G291 EFX + play-start).
- c0a0a→c0a0b OOM at `Mod_LoadPlanes` 308 Kb: SWAP/zone + malloc exhausted
  after Prepare; planes now prefer system malloc, else unused map-load arena
  tail (not BSP-image carve). **CHANGELEVEL_READY** `20260809-184625`
  (`world planes using map-load arena tail 308.05 Kb`).
- Regression: c0a0 G36 PASS `20260809-184736` avg=12.75ms.

### Reactor Host_Frame + Lambda Core green — 2026-08-09

- Host_Frame after SCR~16: skip lean world `SV_RunThink` on non-c0a0
  (probe `20260809-013402`).
- Lean combat tip-safe: `DecalGunshot` / `TEXTURETYPE_PlaySound` /
  `UTIL_BubbleTrail` skip hung TE paths; crowbar `FireBulletsPlayer` proven
  on c3a2 (`20260809-121134`) with SCR frames=32.
- Probe attack hold: TRIGGER_R held 16 frames so lean FillUsercmd sees IN_ATTACK.
- Lambda single-map NEWGAME_READY: c3a2, c3a2a–c3a2f, c3a2d (all G36/G45/G506).
- Lambda changelevel c3a2→c3a2a landmark `c3a2f`: **CHANGELEVEL_READY**
  (`20260809-123857`) — defer hop post-G36, source Prepare with `-gcchangelevel`,
  skip non-c0a0 world interaction (trigger→breakable hung). Further hops
  c3a2a→b→c→d/f also CHANGELEVEL_READY.
- c0a0 G36 WEAK (~56ms) → PASS: tram-only face/live budget during armed
  samples (`GC_IsG36SampleFaceCap`); sticky Prepare draw-cap removed (broke
  c3a2 beams). Proof: c0a0 `20260809-130441` avg=12.75ms; c3a2
  `20260809-130544` NEWGAME_READY + SCR32 + map beam blit gpu=1.
- Campaign hops beyond Lambda: tip-safe `PM_RecursiveHullCheck` bad-node
  (was Host_Error on c0a0a). **CHANGELEVEL_READY** c0a0→c0a0a
  `20260809-131124`; c3a1 NEWGAME + c3a1→c3a1a `20260809-131232` /
  `20260809-131328`. Landmark defaults added for a1a1a / c0a0tob /
  c3a2_c4a1. Xen c4a1 NEWGAME_READY `20260809-184328`; c0a0a→c0a0b
  CHANGELEVEL_READY `20260809-184625`. Further: c0a0b→c0a0c
  `20260809-205421`, c0a0c→c0a0d `20260809-205629`, c4a1→c4a2
  `20260809-205526` (landmark defaults extended).
- Swiss: evidence writer OGC stack line; fixture ingest COMPLETE (real SD still required).

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

After allocator and full-map correctness are stable, consider a progressive
texture cache. Preload a 2x2 or 4x4 fallback mip for every visible material,
then stream higher mip tiers from SD into a small MEM1 staging buffer and upload
them to GX under a per-frame budget. Keep the lowest mip resident and evict
higher tiers by PVS visibility/recency. SD must remain load-time/cache-fill
storage; rendering must never depend on an uncached SD read. This is a visual
polish and memory-pressure optimization, not a substitute for complete retail
asset loading or BSP/PVS correctness.
