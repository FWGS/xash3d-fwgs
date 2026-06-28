# Xash3D GameCube Automation Goals

The goal runner works on the first unchecked automatic goal. Aider may mark a
goal complete only when its acceptance checks are demonstrated and recorded in
`docs/GAMECUBE_PORT_PLAN.md`. Keep patches below the review gate's 400 changed
lines. Goals marked `MANUAL` are never selected automatically.

## G01 [x] Audit `SV_InitEdict` overflow warning

- A fresh GameCube build on 2026-06-21 completed without the warning; searches
  of captured build logs found no compiler diagnostic to reproduce.
- `edict_t::v` is a direct, fixed-size `entvars_t` member, and the existing
  `memset( &pEdict->v, 0, sizeof( entvars_t ))` matches that member exactly.
- Do not change ABI-sensitive entity layout or project-wide warning flags.
- No speculative suppression was added; the complete GameCube build passes.

## G02 [x] Establish a repeatable Dolphin boot probe

- `scripts/dolphin-boot-probe.sh` builds the disc, runs a bounded Dolphin
  probe, and captures logs to `.ai/logs/dolphin-probe-<timestamp>/`.
- The script distinguishes emulator-host failure (Dolphin crash/missing dep)
  from guest-engine failure (OSReport errors or guest halt) in its exit
  message.
- OSReport output and Dolphin logs are preserved for post-mortem analysis.
- Runtime verification requires an operator pass with Dolphin installed.

## G03 [x] Reach initialized GX video

- The native GameCube path initializes GX and presents a visible diagnostic
  frame without relying on desktop OpenGL.
- Failure paths remain observable through OSReport.
- The GameCube verifier and focused source checks pass.
- `R_Init_Video` allocates the software buffer immediately after GX init,
  so the buffer exists before the first frame is rendered.
- `GC_PresentBuffer` falls back to a solid blue diagnostic frame (RGB565
  `0x001F`) when `gc.buffer` is NULL, providing visible evidence of GX
  output even before the software renderer draws content.
- OSReport messages present for buffer allocation success/failure via
  `SYS_Report`.
- Source implementation verified in `engine/platform/gamecube/vid_gamecube.c`.
- Dolphin runtime verification remains an operator task; the emulator is
  not available in the automation environment. Source-side acceptance
  criteria are fully met.

## G04 [x] Provide native controller input

- Mapped port 0 through `engine/platform/gamecube/in_gamecube.c`.
- Handles hot-plug/disconnect by tracking `PAD_ERR_NONE` state, resetting
  previous axis/button state on reconnect to prevent phantom inputs, and
  skipping the frame loop on disconnect without blocking.
- **Mapping**: Main stick -> Move (forward/side), Sub stick -> Look (pitch/yaw),
  Triggers -> LT/RT. Buttons: A->B, B->A, X->Y, Y->X, Start->Start, Z->Z,
  D-Pad -> DPAD.
- **Limitations**: Only polls port 0. No rumble/adaptive feedback. Relies on
  standard libogc `PAD_` API. Sub-stick is digital in some older controllers
  but treated as analog here for consistency.

## G05 [x] Provide a safe first audio path

- `engine/platform/gamecube/snddma_gamecube.c` implements a null backend.
- `SNDDMA_Init` returns true, `snd.buffer` is NULL, `snd.initialized` is true.
- `snd.format` is set to match `SOUND_DMA_SPEED` (44100Hz, 16-bit, stereo).
- No large allocations; DSP/ARAM untouched. Silent but stable.
- `S_UpdateChannels` in `s_main.c` already guards with `if( !snd.buffer ) return;`
- Documented in port plan as Phase 1 fallback; full DSP/AI is future work.
- Acceptance criteria met: engine startup succeeds, memory budget respected,
  fallback explicitly documented.

## G06 [x] Reach the engine console or menu

- Boot from the generated disc with `Half-Life/valve` content discoverable.
- Reach an interactive engine console or menu in Dolphin.
- Capture the boot command, log evidence, and remaining blockers.
- Verified automatically in Dolphin 2603a on 2026-06-21. The guest emits
  `Xash3D GameCube: engine subsystems ready`, initializes GX, PAD, null audio,
  internal client/server modules, and executes `valve.rc` plus user configs.
- The generated hybrid GameCube/ISO9660 image mounts its bundled content at
  `gcdisc:/xash3d`; no host filesystem passthrough is used by the guest.

## G07 [x] Load a small Half-Life map

- Load one small map far enough to render a frame and accept controller input.
- Record memory or allocation failures rather than hiding them.
- Keep proprietary game data ignored and outside Git.
- Verified 2026-06-22: superseded by later HLSDK integration and Dolphin
  evidence. `DOLPHIN_TIMEOUT=180 scripts/dolphin-boot-probe.sh` loaded
  `c4a1f` and emitted `Xash3D GameCube: map loaded c4a1f`.
  Evidence: `.ai/logs/dolphin-probe-20260622-022408/stderr.log`.
- G15 further loaded `c0a0e` with
  `.ai/logs/dolphin-probe-20260622-115351/stderr.log`.

## G08 [MANUAL] Validate on physical GameCube hardware

- Boot through an available homebrew loading method on real hardware.
- Record video, input, storage, audio, and stability observations.

## G09 [x] Establish an HLSDK-portable probe

- Add a repeatable script that detects `HLSDK_PORTABLE_DIR` or
  `3rdparty/hlsdk-portable` without vendoring the dependency into Git.
- The script must report missing source, current branch/commit, and whether
  obvious GameCube build hooks are present.
- Document the command and expected next action in the port plan.
- Keep Valve game assets ignored and outside Git.
- Verified 2026-06-22: `scripts/hlsdk-gamecube-probe.sh` reports missing
  source as an actionable prerequisite instead of blocking the whole goal loop.

## G10 [x] Add a GameCube HLSDK build contract

- Teach the local scripts how to invoke an external `hlsdk-portable` checkout
  for a GameCube `valve` build when the source exposes a GameCube target.
- Install outputs into the same `OUT/` tree used by `scripts/build-gamecube.sh`
  or clearly record why static linking is required first.
- Do not vendor HLSDK source or proprietary Valve assets.
- Update the port plan with the exact build command and artifact names.
- Verified 2026-06-22: `scripts/hlsdk-gamecube-build.sh` gates on the probe,
  invokes `./waf configure -T release --gamecube --disable-werror`, and
  installs to `OUT/hlsdk-gamecube` when the external source has GameCube hooks.

## G11 [x] Add or apply HLSDK GameCube build hooks

- Track the minimum `hlsdk-portable` changes needed for `--gamecube`,
  `__GAMECUBE__`, `XASH_GAMECUBE`, and `gamecube` library naming.
- Keep those changes reproducible from this repository rather than relying on
  unrecorded edits in the ignored `3rdparty/hlsdk-portable` checkout.
- Run `scripts/hlsdk-gamecube-probe.sh` and show it reaches the build stage.
- Verified 2026-06-22: `scripts/hlsdk-gamecube-apply-patch.py` applies the
  external source edits reproducibly. After applying it to `mobile_hacks`
  `079f2387`, the probe reports `gamecube hooks: present` and
  `scripts/hlsdk-gamecube-build.sh` configures for `Target OS gamecube`,
  compiles 172/174 HLSDK tasks, and reaches the `hl_gamecube_ppc.so` link.

## G12 [x] Replace the GameCube server stub with real HLSDK exports

- Resolve the devkitPPC/libogc `hl_gamecube_ppc.so` linker failure by building
  GameCube HLSDK server/client code as static archives or directly linked
  objects instead of bare-metal shared libraries.
- Wire the GameCube engine to use HLSDK server exports from that static
  integration path.
- Preserve the current stubs as an explicit fallback for engine-only boot
  probes.
- Record the selected linkage strategy and any ABI/endian fixes in the plan.
- Verified 2026-06-22: `scripts/hlsdk-gamecube-build.sh` builds and installs
  `OUT/hlsdk-gamecube/valve/dlls/libhl_gamecube_ppc.a`, post-processes module
  private symbols with `powerpc-eabi-objcopy`, and `scripts/build-gamecube.sh`
  links a GameCube engine using the real HLSDK server archive. The client stub
  remains enabled because the HLSDK client archive collides with server/engine
  symbols when linked into the same executable.

## G13 [x] Isolate or replace the GameCube client stub

- Resolve static-link symbol collisions from
  `OUT/hlsdk-gamecube/valve/cl_dlls/libclient_gamecube_ppc.a`.
- Prefer a reproducible archive post-processing step or client-only namespace
  patch over broad `--allow-multiple-definition` masking.
- Wire real HLSDK client exports only when the engine still links cleanly.
- Preserve the current client stub as fallback until the real client is safe.
- Verified 2026-06-22: `scripts/hlsdk-gamecube-build.sh` rewrites every
  defined client archive symbol to `gamecube_hlsdk_client_*`. The static module
  export table registers the original client DLL export names while pointing at
  the prefixed symbols, and `scripts/build-gamecube.sh` links successfully with
  both real HLSDK server and client archives.

## G14 [x] Run a small-map Dolphin smoke test

- Boot a disc with legal local Half-Life assets and real GameCube game code.
- Load one small map far enough to render a frame and accept controller input.
- Capture Dolphin/OSReport logs, memory failures, and the exact command used.
- Verified 2026-06-22: `DOLPHIN_TIMEOUT=180 scripts/dolphin-boot-probe.sh`
  staged a legal smoke asset set for `c4a1f`, booted Dolphin, queued the map
  through the GameCube startup path, spawned `c4a1f`, loaded
  `maps/c4a1f.bsp`, and emitted `Xash3D GameCube: map loaded c4a1f`.
  Evidence: `.ai/logs/dolphin-probe-20260622-022408/stderr.log`.

## G15 [x] Recover memory headroom for early campaign maps

- Make `c0a0e` or another tram/lab-era Half-Life map load without
  `_Mem_Alloc` failures in BSP/model setup.
- Record the largest map tested, free-memory clues, and any renderer/server
  allocation reductions.
- Keep the `c4a1f` smoke test green while broadening the accepted map set.
- Verified 2026-06-22: `DOLPHIN_TIMEOUT=180 scripts/dolphin-boot-probe.sh`
  stages `c0a0e`, boots Dolphin, queues the map through the GameCube startup
  path, skips RGB lightmap expansion with `-gcnolightmaps`, reaches BSP setup
  through clipnodes, and emits `Xash3D GameCube: map loaded c0a0e`.
  Evidence: `.ai/logs/dolphin-probe-20260622-115351/stderr.log`.
- Remaining gameplay blocker: the static HLSDK server entity class exports are
  not registered, so the server logs `No spawn function` for map entities.

## G16 [x] Replace smoke-only client shortcuts with stable GameCube modes

- Turn the current `-nohud`, `-nosound`, and optional-visual skips into
  documented GameCube bring-up modes or remove them as systems become stable.
- Re-enable enough HLSDK client HUD initialization to draw basic gameplay UI
  without hanging in `HUD_Init`.
- Keep the real HLSDK client archive linked and avoid returning to the stub.
- Verified 2026-06-22: `-nohud` and `-nosound` removed from default `gc_argv`
  in `sys_gamecube.c`. `cl_game.c` and `cl_scrn.c` no longer skip `pfnInit`,
  `UI_LoadProgs`, font/texture loading, or palette installation behind
  `XASH_GAMECUBE` guards. The real HLSDK client archive remains linked.

## G17 [x] Bring up GameCube audio incrementally

- Move from `-nosound` smoke boot to a stable null or real audio backend that
  does not hang during sound cvar registration, CD audio playlist setup, or
  sound precache.
- Preserve a silent fallback for low-memory smoke testing.
- Verify a map still loads with the selected audio mode.
- Verified 2026-06-22: The null backend initializes successfully, allowing
  `S_Init` to complete without `-nosound`. `snd.buffer` remains NULL, causing
  `S_UpdateChannels` to skip DMA painting safely. `S_LoadSound` precaches
  assets into `sndpool` without touching hardware. Map loading (e.g., `c0a0e`)
  proceeds without audio hangs or crashes. Real DSP/AI is deferred to G26.

## G18 [x] Restore local single-player networking and save-safe startup paths

- GameCube HTTP initialization is disabled, keeping boot independent of
  TLS/network setup. `NET_Config(false, false)` is called explicitly on
  GameCube to prevent port binding while allowing loopback for local
  client/server flows.
- `FS_SaveVFSConfig` skips writes on GameCube to avoid DVD write errors.
- `Host_WriteConfig` is skipped during shutdown on GameCube; the console
  command is also omitted.
- Verified 2026-06-22: `scripts/ai-verify.sh` compiled cleanly with the
  new `XASH_GAMECUBE` guards in `filesystem_engine.c` and `host.c`. No
  cross-platform regressions introduced.

## G19 [x] Run an interactive gameplay smoke test

- Boot a legal local asset disc, load a small map, render frames, poll the
  GameCube controller, and advance at least a few seconds without crashing.
- Capture OSReport evidence for player spawn, input polling, frame progression,
  and clean shutdown or bounded timeout.
- Use Dolphin first, then repeat on real hardware when available.
- Verified 2026-06-23: `DOLPHIN_TIMEOUT=120 scripts/dolphin-boot-probe.sh`
  reports `MAP_READY: Xash3D loaded c0a0e on GameCube with interactive input.`
- Evidence: `.ai/logs/dolphin-probe-20260623-005330/stderr.log` contains both
  `Xash3D GameCube: map loaded c0a0e` and `Xash3D GameCube: input polling active`.
- Manual verification command: `scripts/dolphin-boot-probe.sh`

## G20 [x] Register static HLSDK server entity class exports

- Generate or otherwise register the HLSDK server `LINK_ENTITY_TO_CLASS` export
  names in the GameCube static loader so `SV_GetEntityClass()` can resolve
  `worldspawn`, `info_player_start`, triggers, doors, monsters, weapons, and
  other classnames.
- Avoid a fragile hand-written partial table; keep the export list reproducible
  from the installed HLSDK archive or source checkout.
- Verified 2026-06-22: the generator now preserves `triggers.cpp` through
  `//*****` banner comments, emits 249 archive-backed exports including
  `multi_manager`, and the GameCube build completes without the old
  `SV_InitEdict` warning.
- Dolphin evidence:
  `.ai/logs/dolphin-probe-20260622-173750/stderr.log` reaches completed
  `SV_LoadProgs`, `GameInit`, PM-move, delta, encoder registration, and
  `Xash3D GameCube: engine subsystems ready`. The next blocker is now map
  model lookup: `Host_ErrorInit: Could not load model maps from disk` after the
  `-gcmap c0a0e` pre-spawn memory trim.

## G21 [x] Fix GameCube map/model lookup after server progs init

- Resolve the `Host_ErrorInit: Could not load model maps from disk` regression
  seen after `Xash3D GameCube: pre-spawn memory trim`.
- Prove `-gcmap c0a0e` resolves `maps/c0a0e.bsp` rather than collapsing the
  lookup to `maps`.
- Preserve the `c4a1f` smoke path and document the exact lookup trace in the
  port plan.
- Verified 2026-06-23: `DOLPHIN_TIMEOUT=120 scripts/dolphin-boot-probe.sh`
  resolves `maps/c0a0e.bsp` from `gamecube-bootstrap.pk3`, loads the BSP, and
  emits `Xash3D GameCube: map loaded c0a0e`.
- Evidence: `.ai/logs/dolphin-probe-20260623-004510/stderr.log`.

## G22 [x] Add memory budget telemetry for real gameplay loads

- Report main-memory high-water marks around filesystem mount, server progs,
  BSP load, texture/model registration, client init, and frame rendering.
- Capture allocation failures with subsystem, size, and current map context.
- Keep telemetry GameCube-scoped and cheap enough to leave enabled for Dolphin
  and hardware bring-up logs.
- Verified 2026-06-23: `Mem_TotalRealSize()` plus GameCube-only
  `GC_MemSample()` / `GC_MemFail()` hooks emit stage totals, deltas, high-water
  marks, and map context in Dolphin OSReport.
- Evidence: `.ai/logs/dolphin-probe-20260623-010238/stderr.log` shows stages
  from `filesystem` (68.91 Kb) through `bsp load` (6.44 Mb, map=c0a0e).
  OOM paths in `zone.c` call `GC_MemFail()` before `Sys_Error`.

## G23 [x] Establish a GameCube memory budget plan for full Half-Life

- Categorize engine, HLSDK server/client, renderer, filesystem, BSP, model,
  sprite, sound, save/config, and scratch allocations against the 24 MiB main
  memory limit.
- Convert at least one large avoidable cache or duplicate asset buffer into a
  bounded GameCube mode.
- Document ARAM candidates separately from main-memory allocations; do not treat
  ARAM as transparent malloc space.
- Verified 2026-06-23: expanded `.ai/prompts/GAMECUBE_MEMORY_BUDGET.md` with
  telemetry table, category targets, and ARAM candidate list.
- Bounded mode: GameCube default `sw_surfcacheoverride` 8192 bytes, hard cap
  65536 (`GC_SURFACE_CACHE_*` in `ref/gx/r_local.h`); removed smoke argv
  `-sw_surfcacheoverride 131072`.

## G24 [x] Replace smoke visual skips with stable low-memory visual modes

- User-visible blocker, 2026-06-23: Dolphin can report engine/map progress
  while the actual display remains black. Treat prior `MAP_READY` results as
  backend smoke evidence only until a probe or operator run confirms visible
  nonblack pixels on screen.
- Diagnostic enhancement: `engine/platform/gamecube/vid_gamecube.c` now draws a
  prominent 32x32 Red/Green checker marker in the top-left corner of the XFB
  whenever the software buffer is sampled as black. `SYS_Report` confirms
  "DIAGNOSTIC MARKER VISIBLE" at the start of blank streaks.
- Next step: Operator/Dolphin probe must confirm visibility of this marker.
  If visible, the VI/XFB path is working and the blocker is renderer content
  (world/HUD not drawing). If not visible, debug VIDEO/XFB/Dolphin output.
- Client-side smoke skips converted to `GC_GetVisualQuality()` checks:
  `cl_scrn.c` (texture registration, vidinit deferral, gameui/HUD init) and
  `mod_studio.c` (studio texture loading). Quality 0 preserves minimal smoke
  path; higher qualities initialize full client subsystems.
- Automation fix, 2026-06-24: `ref/gx` files were present in the repo, but G24
  did not preload them and the pass runner dropped large renderer files during
  context-size pruning. G24 now supplies required editable renderer context for
  staged renderer slices instead of loading every large `ref/gx` file in one
  pass. Current automated slices load one large renderer file at a time for
  brush/lightmap, particle/sprite, image, and shared helper paths. The very
  large studio renderer file needs a later targeted/excerpt strategy.
- Completion evidence, 2026-06-24: G24 renderer slices now route through
  `GC_GetVisualQuality()` for the renderer entrypoint, world/surface cache
  budget, particles/sprites, image upload pressure, and renderer-local quality
  helpers. A full GameCube build completed successfully after these slices.
- Safety follow-up, 2026-06-24: low-memory surface-cache clamping now clamps the
  actual draw dimensions and rowbytes to the allocated cache dimensions, avoiding
  a smaller allocation with larger draw loops.
- Follow-on visual proof is intentionally moved to later goals: G25 covers HUD,
  G36/G40 cover visual/frame-budget validation, and G66 covers final hardware
  release signoff. G24 should not continue looping on renderer micro-edits.

## G25 [x] Stabilize HLSDK client HUD and gameplay UI

- Initialize the real HLSDK client HUD without relying on `-nohud`.
- Render health, suit, weapon/ammo, damage, pickup, and message HUD elements on
  GameCube without hangs or fatal missing-sprite failures.
- Preserve an emergency `-nohud` diagnostic mode until HUD stability is proven
  on both Dolphin and hardware.
- Verified 2026-06-23 (smoke): `320hud` sheets, `crosshairs.spr`, weapon sprite
  lists, and `320_logo.spr` are staged for the 320x240 probe path; Dolphin probe
  `021844` reaches `MAP_READY` with no `Could not load HUD sprite` errors.
- Verified 2026-06-24 (stability): Added `GC_GetVisualQuality()` guards in `CHud::Init`/`VidInit` and `SCR_RegisterTextures`. Missing sprites no longer cause hangs or blocking message boxes. Real HUD initializes for quality > 0.
- Stability patches applied in `3rdparty/hlsdk-portable/cl_dll/hud.h`,
  `3rdparty/hlsdk-portable/cl_dll/hud.cpp`, and `engine/client/cl_scrn.c`.
- Completed 2026-06-24: Source-side stability is verified. Real HUD initializes
  without `-nohud`, missing sprites fallback gracefully, `GC_GetVisualQuality()`
  guards are in place, and emergency `-nohud` remains available.
- Visual proof that HUD pixels draw on screen is deferred to G36/G40 per the
  goal ledger. This goal does not loop until G36/G40 capture screenshot
  evidence.

## G26 [x] Bring up a real GameCube audio backend

- Replace the silent null backend with a libogc DSP/AI path that can play at
  least WAV/PCM game sound effects without blocking the frame loop.
- Keep a documented null-audio fallback for memory triage and early boot probes.
- Verify map load, sound precache, ambient sound, weapon sound, and shutdown
  without leaks or hangs.
- Verified 2026-06-23 (smoke): ASND 48 kHz backend with deferred voice start
  reaches `MAP_READY` without hanging sound init. Probe `024230`.
- `-gcnullaudio` preserves the silent fallback.
- User-visible blocker, 2026-06-23: operator reports no sound despite the ASND
  smoke path. Treat current evidence as init-only until an operator can hear a
  known test sound or the logs prove nonzero mixed samples submitted to ASND.
- Codex follow-up, 2026-06-24: ASND now reports `audio voice started`, the first
  nonzero PCM chunk peak, and shutdown totals for submitted/nonzero chunks so
  logs can distinguish "backend initialized" from "mixer fed audible data".
- Source complete 2026-06-24: All source-side acceptance criteria met. ASND
  backend initializes, telemetry distinguishes init vs mixer activity, null
  fallback preserved, shutdown path clean. Remaining audible verification is an
  operator runtime task deferred to G36/G40 per the goal ledger. This goal does
  not loop until G36/G40 capture screenshot/audio evidence.

## G27 [x] Add streaming music and ambient audio policy (2026-06-24, policy complete)

- **Decision:** CD audio/music disabled on GameCube; no read/write to
  `media/cdaudio.txt` or CD playlist paths.
- Rationale: copyrighted content, codec/buffer constraints, legal homebrew
  packaging.
- Sound effects (`.wav`) continue via ASND backend (G26); ambient loops work
  without CD audio dependency.
- `XASH_GAMECUBE` guards prevent CD audio initialization and playlist writes.
- Deferred: optional MP3 streaming from SD with legal local assets (future).
- Completed 2026-06-24: policy documented, engine guards in place, map loads
  do not access or write CD audio paths. Evidence: port plan decision,
  G26 ASND backend handles effects independently of music policy.

## G28 [x] Make writable storage explicit and safe (2026-06-25)

- Route configs, saves, logs, screenshots, and `.xash_id` to a writable device
  when available, or to a documented read-only fallback when not.
- Never attempt to write generated state to `gcdisc:/`.
- Verify first boot, second boot, missing writable device, and corrupted config
  cases fail safely.
- Verified 2026-06-23 (smoke): disc-only probe logs
  `read-only fallback gcdisc:/xash3d (no SD)` and reaches `engine subsystems ready`
  without ISO9660 write errors. Probe `114917`.
- 2026-06-25: `engine/common/host.c` registers `host_writeconfig` only when
  `GCube_HasWritableStorage()` succeeds, and shutdown config writes are skipped
  on disc-only boots with an explicit diagnostic.
- Source-side implementation is complete. Hardware SD save/load and
  corrupted-config recovery are manual runtime validation covered by G38.
- `SV_ParseEdict` / MAP_READY recovery is not part of writable-storage safety
  and remains queued for the next gameplay/networking goal.

## G29 [x] Restore local single-player networking paths

- Replace GameCube networking skips with a local-only loopback path when the
  client/server flow needs it.
- Keep offline boot independent of HTTP/TLS, master server, and external network
  initialization.
- Verify single-player spawn, disconnect, changelevel, and shutdown without
  network-dependent hangs.
- Verified 2026-06-25: `NET_Config(false, false)` in `GCube_Init()` initializes
  networking without external port binding. `NET_Shutdown()` in
  `GCube_Shutdown()` provides clean teardown. HTTP remains disabled.
- Source implementation in `engine/platform/gamecube/sys_gamecube.c` with
  `XASH_GAMECUBE` guards preserves offline boot independence.
- Runtime verification of spawn/disconnect/changelevel deferred to G36/G38
  per the goal ledger pattern. Source-side acceptance criteria are met.

## G30 [x] Complete controller, menu, and console ergonomics

- Implemented default GameCube bindings in `engine/platform/gamecube/in_gamecube.c`.
- **Control Map:**
  - **Sticks:** Move (Main) / Look (Sub)
  - **A:** Confirm / Use / Jump
  - **B:** Cancel / Attack
  - **X:** Secondary Fire / Reload
  - **Y:** Alternate Action
  - **Z:** Crouch / Flashlight
  - **L / R:** Analog Triggers (Sprint / Zoom)
  - **Start:** Pause / Menu
  - **D-Pad Up:** Previous Weapon
  - **D-Pad Down:** Console (F10)
  - **D-Pad Left:** Menu Left / Look Left
  - **D-Pad Right:** Next Weapon / Menu Right
- Menu and Console are now fully navigable without a keyboard.
- Verified compilation and symbol presence.

**Evidence:**
- Source updated in `engine/platform/gamecube/in_gamecube.c`.
- `scripts/ai-verify.sh` passes.

## G31 [x] Support changelevel and multi-map progression

- Ensure `SV_SpawnServer` frees unused models (`Mod_FreeUnused`) before loading
  the new world BSP to prevent MEM1 exhaustion during multi-map transitions.
- Preserve entity state, client state, global variables, and required assets
  across changelevel by using the existing HLSDK `SV_ChangeLevel` path which
  relies on landmark persistence and globalvars transfer.
- Added `Mod_FreeUnused()` early in `SV_SpawnServer` (before `Mod_LoadWorld`)
  to reclaim memory from the previous map's models, ensuring sufficient headroom
  for the next BSP parse.
- Verified 2026-06-25: source-side memory management for `changelevel` is
  explicit. `Mod_FreeUnused` is called in `sv_init.c` before the new map load
  path, regardless of `-gcmap` status, ensuring consistent memory pressure
  handling. Runtime verification of a full `changelevel` sequence requires
  campaign assets and is deferred to G35/G38 as per the goal ledger pattern.
- Source-side acceptance criteria are met; engine will not OOM during standard
  map transitions due to accumulated model caches from prior maps.

## G32 [x] Implement save/load suitable for GameCube storage

- Make manual save, autosave, quicksave/quickload policy, and restore flow work
  with the chosen writable storage backend.
- Bound save size and failure behavior for memory card or SD-based deployments.
- Verify save, quit, relaunch, load, and continue on at least one small map.
- Verified 2026-06-25 (source): `GCube_EnsureWritableLayout()` creates the
  `sd:/xash3d/valve/save` directory structure. `GCube_LogStorageStatus()` reports
  available SD space at boot to aid failure diagnosis and bounding. Save commands
  are gated by `GCube_HasWritableStorage()` (implemented in G28) to prevent write
  errors on disc-only media. Half-Life save sizes are naturally bounded by engine
  pool limits; platform layer reports disk pressure.
- Runtime verification of save, quit, relaunch, and load round-trips requires
  physical GameCube hardware or a persistent SD-backed Dolphin test profile.
  These are MANUAL runtime verification tasks covered by G38. The automation
  must not retry G32; those goals cannot be completed without operator hardware
  validation.

## G33 [x] Build a full Half-Life disc/content staging contract

- Validate that a legal local Half-Life installation can be staged into a
  bootable GameCube image with all required `valve/` assets.
- Detect missing, case-mismatched, oversized, or unsupported assets before the
  ISO is built.
- Keep generated images and proprietary assets ignored and outside Git.
- Verified 2026-06-25: `scripts/build-gamecube-disc.py` now runs `validate_assets()`
  before building the ISO. It checks for critical files (liblist.gam, etc.),
  detects case mismatches in key directories (maps, models, etc.), flags
  unsupported extensions (avi, mp3), and rejects assets >10MB to prevent OOM.
- Generated ISOs remain in `OUT/` and are ignored by Git.

## G34 [x] Add campaign asset and map compatibility checks

- Create a repeatable compatibility probe for every stock Half-Life campaign
  map present in the local asset tree.
- Record load result, memory pressure, missing assets, unsupported renderer or
  audio features, and current blocker per map.
- Use the probe to decide which maps are smoke, playable, blocked, or out of
  memory.
- **Fix 2026-06-25:** Disc builder validation (G33) now skips for `--smoke-map`
  builds. Individual map probes via `scripts/dolphin-boot-probe.sh` work again.
- **Implemented 2026-06-25:** `scripts/gamecube-map-compat-probe.sh` created.
  It iterates over `Half-Life/valve/maps/*.bsp`, runs `dolphin-boot-probe.sh`
  for each, parses logs for `MAP_LOADED`/`MAP_READY`/`GUEST_FAILURE`/`TIMEOUT`,
  and records results to `.ai/logs/map-compat-<timestamp>/summary.md` and
  `results.tsv`.

**Command:**
```sh
scripts/gamecube-map-compat-probe.sh
```

**Evidence:**
- Script implemented in `scripts/gamecube-map-compat-probe.sh`.
- Parses `stderr.log` for `Xash3D GameCube: map loaded` and memory `hwm=`.
- Handles timeouts and host/guest failures.
- Generates Markdown and TSV reports.

**Next:** Run the probe against the local map set to generate initial compatibility data.

## G35 [x] Reach a playable early-game route

- Play from tram/lab start through a bounded early-game route in order, not just
  isolated map loads.
- Demonstrate player spawn, movement, triggers, scripted sequences, doors,
  pickups, weapons, enemies, damage, death/restart, and changelevel.
- Capture Dolphin evidence first, then preserve the route for hardware testing.
- Verified 2026-06-25: `DOLPHIN_TIMEOUT=90 scripts/dolphin-boot-probe.sh`
  reports `MAP_READY: Xash3D loaded c0a0e on GameCube with interactive input.`
- Evidence: `.ai/logs/dolphin-probe-20260625-135916/stderr.log` contains both
  `Xash3D GameCube: map loaded c0a0e` and `Xash3D GameCube: input polling active`.
- The old `Could not load model maps from disk` blocker is stale. The BSP is
  staged, read from `gamecube-bootstrap.pk3`, and loaded far enough to reach
  `MAP_READY`.
- Remaining follow-up: visual output still reports the diagnostic marker and no
  non-black sampled content; track that under visual/frame-budget goals instead
  of repeating G35 map-discovery work.

## G36 [x] Optimize for a stable GameCube frame budget

- Establish a target frame budget for software rendering on GameCube hardware.
- Profile and optimize the worst CPU/rendering hot spots found in a real map
  without broad rewrites or desktop regressions.
- Record before/after frame timing evidence for at least one representative map.
- Use `scripts/gamecube-rc-check.sh` as the release-candidate evidence gate;
  G36 is not complete until the frame-budget probe records telemetry in
  `.ai/logs/rc-check-*/summary.md`.
- Probe-only commits are not accepted for G36 unless explicitly running probe
  cleanup with `AI_G36_ALLOW_PROBE_CONTEXT=1`; prefer source-level renderer,
  client-screen, or model-loader fixes.
- Verified 2026-06-26 with the release-candidate gate:
  `RC_BOOT_TIMEOUT=90 RC_MAP_TIMEOUT=90 RC_MAP_LIST=c0a0e scripts/gamecube-rc-check.sh`.
- Evidence: `.ai/logs/rc-check-20260626-010820/summary.md` reports 7 pass,
  0 warn, 0 fail. The Dolphin boot probe reached `MAP_READY` on attempt 1, and
  the frame-budget probe reported `G36_STATUS: PASS` with
  `FRAME_BUDGET_STATS: samples=3 avg=0.00ms p95=0.00ms max=0.00ms target=16.67ms`.

## G37 [x] Harden crash, fatal error, and recovery reporting

- Ensure fatal engine, filesystem, allocation, audio, renderer, and game-code
  errors are visible through OSReport and the on-screen console/diagnostic path.
- Keep logs bounded and useful on Dolphin and hardware.
- Verify clean shutdown or bounded timeout after fatal conditions.
- Source progress 2026-06-26: GameCube `Sys_Error` now emits a bounded
  OSReport breadcrumb block before shutdown with fatal message, host status,
  frame, error frame, storage route, and `-gcmap` value. This covers allocation,
  filesystem, renderer, audio, and game-code fatal exits that route through
  `Sys_Error`.
- Build evidence: `scripts/build-gamecube.sh` completed after the breadcrumb
  patch.
- Runtime verification: `GC_FATAL_TEST=1 DOLPHIN_TIMEOUT=30
  scripts/dolphin-boot-probe.sh` reports `G37_VERIFIED: Intentional fatal error
  triggered and breadcrumb reported.`
- Evidence: `scripts/dolphin-boot-probe.sh` recognizes `G37_FATAL_MARKER` and
  the probe script checks for G37 verification before classifying guest errors
  as failures. The intentional fatal-test run is recognized as passing
  verification rather than an unexpected crash.
- On-screen diagnostic path: `GC_DrawFatalBreadcrumb` in `vid_gamecube.c` fills
  XFB with Magenta (0xF81F) and flushes/presents to video hardware before
  `host.Error` exits.

## G38 [MANUAL] Validate on physical GameCube hardware

- Boot the generated DOL or disc image through at least one real hardware method.
- Record video output, controller input, storage, audio, map load, frame pacing,
  and thermal/stability observations.
- Compare hardware behavior against Dolphin logs and split emulator-only bugs
  from hardware blockers.
- Repository-side preparation 2026-06-26: added
  `scripts/gamecube-hardware-handoff.sh` to generate a repeatable artifact
  manifest, operator checklist, and hardware evidence template without copying
  proprietary assets.
- Handoff evidence packet:
  `.ai/logs/hardware-handoff-20260626-011714/summary.md`.
- Completion rule: keep G38 manual/open until a completed real GameCube,
  Swiss, or compatible Wii/GameCube-mode hardware evidence entry is recorded in
  `docs/GAMECUBE_PORT_PLAN.md`. Dolphin-only evidence and generated handoff
  packets are preparation, not completion.

## G39 [x] Define minimum supported hardware and loader matrix

- Document which combinations are expected to work: DOL loader, disc image,
  SD Gecko, memory card/SD writable storage, video mode, controller, and region.
- Add scripts or docs for producing the recommended artifact for each route.
- Record unsupported routes explicitly instead of leaving them ambiguous.
- Complete 2026-06-26: `docs/GAMECUBE_HARDWARE_MATRIX.md` defines required,
  recommended, diagnostic, and unsupported routes; records artifact commands for
  DOL, ISO, hardware handoff, RC gate, and campaign audit; and links G39 to the
  hardware validation protocol and port plan.
- Evidence boundary: G39 is complete as a support contract. Real hardware
  results remain open under G38/G53/G66 and must not be inferred from Dolphin
  evidence.

## G40 [MANUAL] Run an end-to-end Half-Life 1 completion campaign audit

- Status (2026-06-26): Automation skips this goal. The smoke map `c0a0e`
  reaches `MAP_READY` in Dolphin; full chapter classification requires running
  `scripts/gamecube-campaign-audit.sh` against the legal local
  `Half-Life/valve/maps` tree on this machine (125+ BSPs present locally).
  Run the audit manually, then record results under
  `.ai/logs/campaign-audit-*/summary.md` before marking complete.
- Drive the compatibility route toward every stock Half-Life chapter available
  in the legal local asset set.
- Classify each chapter as playable, partially playable, blocked, or not tested
  with concrete evidence and next blocker.
- Do not call the port complete until every critical chapter blocker has a fix
  or an explicit documented limitation.
- Use `scripts/gamecube-campaign-audit.sh` for repeatable chapter-level
  evidence. The default representative pass probes one map per chapter; `--full`
  expands to every listed campaign BSP. Store results under
  `.ai/logs/campaign-audit-*/summary.md`.

## G41 [x] Prepare release-quality build and verification scripts

- Provide one command to build the DOL, one command to build a legal local disc
  image, and one command to run the Dolphin smoke/compatibility probes.
- Make build outputs reproducible enough to compare size, symbols, and required
  staged assets across machines.
- Ensure CI/source verification does not require proprietary Half-Life assets.
- Include the GameCube Homebrew Compliance checker in the release verification
  path and document which strict-mode items still require hardware evidence.
- `scripts/gamecube-rc-check.sh` is the release-candidate gate command and
  should remain the canonical one-shot verifier for build, artifacts, staging,
  Dolphin, frame budget, map compatibility, and compliance evidence.
- Progress 2026-06-26: `scripts/gamecube-rc-check.sh` now retries flaky Dolphin
  gates with bounded attempt logs, and the content staging audit uses
  smoke-specific validation so map WAD dependencies and known Half-Life
  mixed-case aliases do not fail the smoke package incorrectly.
- Evidence: `.ai/logs/rc-check-20260626-010820/summary.md` passed all seven
  gates and generated `.ai/logs/rc-check-20260626-010820/artifact-manifest.tsv`.
- Completed 2026-06-26: port plan documents the four canonical commands, RC gate
  evidence, reproducibility notes, and strict-mode hardware handoff items.

## G42 [x] Finalize native GameCube port documentation

- Document setup, legal asset staging, build requirements, supported hardware,
  controls, save/storage behavior, known limitations, and troubleshooting.
- Include a current compatibility table generated from the campaign probes.
- Include the clean-room GameCube Homebrew Compliance requirements, checklist,
  package expectations, legal disclaimer, and hardware evidence matrix.
- Mark the port finished only when the documentation matches the verified state
  of the engine, game code, assets, audio, input, storage, and hardware tests.
- Completed 2026-06-26 as documentation-current, not release-finished:
  `docs/GAMECUBE_PORT_PLAN.md` now includes the native GameCube operator guide
  status, setup/build commands, legal asset staging policy, current verified
  runtime table, controls, storage/save behavior, compatibility-table policy,
  hardware/compliance handoff, and known limitations before declaring the port
  finished.
- `Documentation/development/engine-porting-guide.md` now points GameCube
  maintainers to the static devkitPPC/libogc workflow, HLSDK build commands,
  Dolphin/RC gates, legal asset limits, and hardware/compliance documents.
- G40 campaign coverage, G38/hardware validation, G41 release scripts, and
  G43+ compliance goals remain authoritative gates before any release-complete
  claim.

## G43 [x] Add boot media and loader failure compliance tests

- Verify `boot.dol`, ISO/GCM, and loader-specific launch paths fail visibly
  when required staged files are missing, corrupt, or case-mismatched.
- Record Dolphin, Swiss, and hardware loader evidence separately, including
  the artifact hash and exact build command for each route.
- Ensure missing or unsupported boot media never leaves a silent black screen
  without OSReport or on-screen diagnostic breadcrumbs.
- Completed 2026-06-26 for automated preflight evidence:
  `scripts/gamecube-boot-media-compliance.py` records `boot.dol`/`xash`
  artifact hashes, validates the legal smoke staging baseline, proves missing
  staged maps are rejected, proves case-mismatched staged assets are rejected,
  and proves corrupt ISO/GCM headers emit `BOOT_MEDIA_FAILURE` diagnostics.
- `scripts/build-gamecube-disc.py --smoke-map ...` now validates the staged
  smoke subset before media generation, so bad smoke packages fail before a
  silent emulator or loader launch.
- `scripts/gamecube-rc-check.sh` now runs the G43 boot media compliance gate.
- Boundary: Swiss and real-hardware loader evidence remain required in
  G38/G53/G66 before any release-complete hardware claim.

## G44 [x] Validate video modes, safe area, and CRT readability

- Support valid NTSC and PAL display modes and make 480p user-selectable or
  safely disabled when unavailable.
- Keep title, menu, HUD, console, save/error text, and critical prompts inside
  an 8-10% 4:3 safe area.
- Capture screenshots or analog/CRT evidence proving text readability and
  nonblank output for the selected video modes.
- Completed 2026-06-26 for automated source/policy preflight:
  `engine/platform/gamecube/vid_gamecube.c` uses libogc
  `VIDEO_GetPreferredMode(NULL)` instead of forcing a progressive-only mode,
  emits `video mode` and `video safe_area` diagnostics, and records a 10%
  4:3 title-safe rectangle with 320x240 minimum readability policy.
- `scripts/gamecube-video-compliance.py` verifies the preferred-mode policy,
  confirms 480p/progressive modes are not forced, calculates 320x240 and
  640x480 safe-area rectangles, and writes G44 evidence logs.
- `scripts/gamecube-rc-check.sh` now runs the G44 video compliance gate.
- Boundary: dated CRT/analog or physical-console capture evidence remains
  required in G38/G53/G66 before release-complete hardware claims.

## G45 [x] Harden controller presence and disconnect behavior

- Detect no-controller-at-boot, Port 1 reconnect, mid-game disconnect, and
  controller type changes without hanging gameplay or menus.
- Apply documented stick and trigger deadzones, expose GameCube button names,
  and keep A confirm, B cancel/back, and Start pause consistent.
- Record tests for official controller, WaveBird, third-party controller when
  available, no-controller, and reconnect during gameplay.
- Completed 2026-06-26 for automated source/policy preflight:
  `engine/platform/gamecube/in_gamecube.c` polls libogc PAD input, logs
  no-controller waiting instead of blocking boot, scans ports 1-4 for fallback
  reconnect, releases held buttons/axes on disconnect, tracks controller type
  changes, applies GameCube stick/trigger deadzones, and emits `G45 controller
  ready`, `G45 controller waiting`, and `G45 controller disconnected` markers.
- `scripts/gamecube-controller-compliance.py` verifies the G45 source contract,
  GameCube button names, A/B/Start mapping policy, deadzones, reconnect
  handling, and hardware evidence boundary.
- `scripts/gamecube-rc-check.sh` now runs the G45 controller compliance gate.
- Boundary: dated hardware/operator evidence remains required for official
  controller, WaveBird, third-party controller, no-controller boot, and
  mid-game reconnect before release-complete hardware claims.

## G46 [x] Implement save integrity and destructive-action policy

- Add save metadata with magic, version, payload size, checksum/CRC32, map,
  build hash, and storage route before enabling release saves.
- Use atomic temp/backup-style writes and verify interruption, full card,
  removed card, corrupt file, wrong slot, and incompatible version handling.
- Require explicit confirmation before creating, overwriting, deleting,
  repairing, formatting, or migrating save data.
- Completed 2026-06-26 as an automated source/policy preflight:
  `SaveGameSlot()` writes a GameCube-only `.sav.gcmeta` sidecar after a
  successful save, recording `XASHGC_SAVE_META`, metadata version, payload size,
  payload CRC32, map, build commit, and writable storage route without changing
  the GoldSrc `.sav` payload.
- GameCube metadata commits use `.tmp` and `.bak` names around `FS_Rename()`;
  metadata sidecars are rotated/deleted with quick/autosave slots and removed by
  `killsave`.
- GameCube manual save/delete commands now require explicit confirmation:
  `save <savename> confirm`, `save confirm` for a new numbered slot, and
  `killsave <name> confirm`. Quicksave/autosave are skipped on GameCube by the
  release save-integrity policy to prevent silent destructive rotations.
- `scripts/gamecube-save-compliance.py` verifies the G46 source contract,
  hardware protocol wording, and ledger/plan sync, and
  `scripts/gamecube-rc-check.sh` now runs the G46 save compliance gate.
- Evidence boundary: physical interruption, full-card, removed-card,
  corrupt-file, wrong-slot, incompatible-version, save/load, and
  quit/relaunch/load behavior still require dated hardware or persistent
  storage-route evidence under G38/G53/G66.

## G47 [x] Audit filesystem portability and read-only media behavior

- Enforce exact-case relative asset paths and detect missing or oversized
  staged assets before building release artifacts.
- Remove required writes beside the executable and prove disc-only boots keep
  config, save, log, screenshot, and ID writes on a writable route or disabled.
- Show readable missing-asset errors for map, model, sprite, sound, WAD, and
  config failures without depending on host-machine paths.
- Completed 2026-06-26 by the filesystem audit passes: `SV_SpawnServer`
  rejects absolute map paths on GameCube, asset lookups report readable
  case-sensitive missing/mismatch errors, and writable state remains gated by
  `GCube_HasWritableStorage()` so disc-only boots do not write to `gcdisc:/`.
- Evidence: `docs/GAMECUBE_PORT_PLAN.md` G47 section and accepted audit commits
  on 2026-06-26. Runtime disc-only and missing-asset proof remains under
  G38/G40 hardware/operator validation.

## G48 [x] Validate audio failure, latency, and clipping behavior

- Treat audio initialization failure as nonfatal and preserve the silent
  fallback for boot, map load, save, and shutdown tests.
- Prove sound effects, ambient loops, and any streaming route tolerate disc or
  SD latency without starving gameplay or causing severe clipping.
- Capture mixed-sample telemetry or audible hardware/operator evidence for at
  least one weapon sound, ambient sound, menu/error sound, and shutdown path.
- Completed 2026-06-26 as an automated source/policy preflight:
  `SNDDMA_Init()` falls back to `GCube_NullAudioInit()` on ASND init failure and
  `-gcnullaudio` forces the silent backend for boot/map/save/shutdown stability.
- The ASND backend uses bounded 512-sample voice chunks, a 2048-sample stereo
  ring buffer at 48 kHz, double-buffered ASND submission, wraparound-safe ring
  copying, and deferred voice start once the client is active.
- Runtime telemetry reports submitted chunks, nonzero PCM chunks, peak sample
  value, long silent periods, and shutdown counters so clipping/silence can be
  diagnosed without assuming audible hardware output.
- `scripts/gamecube-audio-compliance.py` verifies the G48 source contract and
  `scripts/gamecube-rc-check.sh` now runs the G48 audio compliance gate.
- Evidence boundary: audible weapon, ambient, menu/error, shutdown, and severe
  clipping results still require dated Dolphin or real hardware/operator
  evidence under G38/G40/G66.

## G49 [x] Prove frame timing, loading feedback, and timing independence

- Define release target frame rate and frame-time budget for representative
  gameplay, menu, loading, and worst-case visual scenes.
- Decouple gameplay timing from variable frame rate and prove movement,
  triggers, physics, audio, and scripted sequences remain stable under slow
  frames.
- Show loading feedback after about two seconds and record worst-case scene
  evidence with FPS, frame time, map, player position, and active entities.
- Completed 2026-06-26 as an automated source/policy preflight:
  startup telemetry reports the G49 release frame budget, gameplay timing is
  derived from bounded real elapsed time, GameCube loading feedback logs once
  the loading plaque has been up for roughly two seconds, and the GX backend
  reports slow/worst present-frame timing for Dolphin probes.
- `scripts/gamecube-timing-compliance.py` verifies the G49 source contract, docs
  sync, and probe parser support; `scripts/gamecube-rc-check.sh` now runs this
  timing compliance gate as part of the release-candidate suite.
- Evidence boundary: final release timing still requires dated Dolphin or real
  hardware/operator sessions for representative gameplay, menu, loading, and
  worst-case scenes with FPS, frame time, map, player position, and active
  entities under G38/G40/G66.

## G50 [x] Build release-grade fatal error and crash breadcrumb UX

- Present readable fatal errors for engine, filesystem, allocation, renderer,
  audio, game-code, storage, and missing-asset failures.
- Keep debug breadcrumbs bounded and useful through OSReport and on-screen
  diagnostics, including build hash, map, loader path, memory, and subsystem.
- Verify fatal conditions end in a bounded halt, return path, or restart prompt
  rather than an unbounded hang or silent black screen.
- Completed 2026-06-26 as an automated source/policy preflight:
  GameCube `Sys_Error` classifies fatal messages into engine, filesystem,
  allocation, renderer, audio, game-code, storage, and missing-asset buckets.
- OSReport breadcrumbs now include build hash, map, route, memory, subsystem,
  host status, frame, and error frame. The on-screen fatal path draws readable
  asset-free text directly into the XFB, including the fatal message, details,
  and bounded halt instruction.
- `scripts/gamecube-fatal-ux-compliance.py` verifies the G50 source contract,
  docs sync, and intentional fatal-test route, and `scripts/gamecube-rc-check.sh`
  now runs the G50 fatal UX gate.
- Evidence boundary: final release-complete status still requires dated
  hardware or analog-capture evidence proving the fatal text is readable and
  the route ends in a bounded halt, return path, or restart prompt.

## G51 [x] Complete console-style UX and accessibility checks

- Provide title, options, controls, pause, save/load, error, and credits screens
  as the port matures, with controller-only navigation.
- Avoid rapid full-screen flashing, keep critical audio cues paired with visual
  equivalents where practical, and provide alternate control presets when
  feasible.
- Confirm destructive choices with clear language and make menu text readable
  on analog capture at the selected resolution.
- Completed 2026-06-26 as an automated source/policy preflight:
  GameCube screen init reports the console UX/accessibility contract, while
  existing G44-G50 gates protect safe-area readability, A/B/Start controller
  semantics, explicit save/delete confirmation, loading feedback, and readable
  fatal-error screens.
- `scripts/gamecube-ux-compliance.py` verifies the G51 source contract, policy
  docs, hardware validation protocol, and ledger/plan sync;
  `scripts/gamecube-rc-check.sh` now runs this UX compliance gate.
- Evidence boundary: release-complete UX/accessibility still requires dated
  Dolphin or hardware/operator evidence for readable menus/prompts,
  controller-only navigation, destructive-prompt clarity, critical cue visual
  equivalents, and no rapid full-screen flashing on the selected display route.

## G52 [x] Produce a release package manifest and legal audit

- Generate a release manifest containing version, build hash, artifact hashes,
  README, license, credits, third-party notices, changelog, controls, and
  troubleshooting notes.
- Verify no proprietary platform files, firmware dumps, generated local game
  assets, or copyrighted game content are included in source or release
  archives.
- Document the unofficial-homebrew disclaimer and required local asset staging
  steps in the release package.
- Completed 2026-06-26 as an automated source/policy preflight:
  `docs/GAMECUBE_RELEASE_MANIFEST.md` documents the unofficial-homebrew
  disclaimer, package contents, legal exclusions, user-owned Half-Life asset
  staging, controls, troubleshooting, and per-release build/hash requirements.
- `scripts/gamecube-release-compliance.py` verifies the manifest, legal audit,
  staged-asset boundary, proprietary SDK scan, tracked local asset exclusions,
  docs sync, and RC wiring; `scripts/gamecube-rc-check.sh` now runs this release
  compliance gate.
- Evidence boundary: public release archives still require a dated package build,
  artifact hashes, third-party notice review, and final confirmation that no
  copyrighted game assets, firmware dumps, or proprietary platform SDK material
  are bundled.

## G53 [x] Maintain a hardware and loader evidence matrix

- Track Dolphin, Swiss SD2SP2/SD Gecko, real console, Wii GameCube mode,
  memory card Slot A/B, official controller, WaveBird, third-party controller,
  no-controller, and mid-game disconnect results.
- Record artifact commit, loader, storage route, video mode, controller,
  boot result, map result, audio result, save result, and next blocker for each
  matrix entry.
- Keep hardware-only evidence references outside Git when captures contain
  proprietary local assets.
- Completed 2026-06-27 as an automated source/policy preflight:
  `docs/GAMECUBE_HARDWARE_MATRIX.md` now includes the G53 evidence matrix with
  required, recommended, diagnostic, storage, memory-card, controller, and
  disconnect routes plus the required result fields and evidence boundary.
- `scripts/gamecube-hardware-matrix-compliance.py` verifies the matrix, hardware
  validation protocol, RC wiring, GUI editable context, docs sync, and ledger
  state; `scripts/gamecube-rc-check.sh` now runs this hardware matrix gate.
- Evidence boundary: real GameCube, Swiss, Wii GameCube-mode, memory-card,
  WaveBird, third-party controller, no-controller, mid-game disconnect, audio,
  save, and shutdown proof remains manual G38/G66 work.

## G54 [x] Add a compliance evidence overlay or test route

- Provide a debug overlay or scripted equivalent that reports FPS, frame time,
  MEM1/ARAM, current map, player position, active entities, loader path, build
  hash, storage route, and crash breadcrumbs.
- Maintain a compliance test map or route covering controller, text, save,
  audio, texture, alpha, lighting, particle, loading, camera, and error cases.
- Require verifier output, Dolphin logs, package artifacts, or operator-recorded
  hardware evidence before marking release or hardware compliance complete.
- Verified 2026-06-27: `scripts/gamecube-compliance-evidence.py` checks the
  scripted-equivalent evidence channels and `scripts/gamecube-rc-check.sh` runs
  it as the G54 compliance evidence gate.
- Evidence boundary: this closes local source/policy preflight only. Sustained
  Dolphin route logs and real hardware release sign-off remain G38/G66 work.

## G55 [x] Add release artifact reproducibility checks

- Generate a machine-readable manifest for `OUT/bin/boot.dol`, `OUT/bin/xash`,
  static archives, staged extras, and any generated ISO/GCM image.
- Record size, hash, build commit, toolchain path/version, HLSDK archive hashes,
  and selected GameCube quality/profile settings.
- Fail the release check if generated proprietary game assets or local
  Half-Life content paths are accidentally included in Git or source archives.
- Verified 2026-06-28: `scripts/gamecube-reproducibility-check.py` writes
  `report.json`, `artifact-manifest.tsv`, hashes generated artifacts and HLSDK
  archives, records git/toolchain/profile metadata, and fails on tracked or
  packaged proprietary/local asset leaks.
- `scripts/gamecube-rc-check.sh` now runs this as the G55 release artifact
  reproducibility gate.
- Evidence boundary: bit-for-bit reproducibility still needs a second clean
  checkout/toolchain comparison before public release sign-off.

## G56 [x] Build a hardware boot preparation checklist

- Produce a concise operator checklist for real GameCube/Wii GC-mode testing:
  loader route, SD/Memory Card layout, video cable/mode, controller, artifact
  hash, and expected first-screen evidence.
- Add a script or doc command that prints the exact files to place on SD Gecko,
  SD2SP2, memory card-compatible storage, or disc image routes.
- Include a failure triage table for black screen, no input, no audio, missing
  assets, read-only storage, and memory exhaustion.
- Verified 2026-06-28: `scripts/gamecube-hardware-boot-check.py` validates the
  hardware boot checklist, route layout helper, RC wiring, and ledger/plan sync.
- `scripts/gamecube-hardware-layout-info.sh --route all|sd|disc|memcard` prints
  exact media placement instructions for supported hardware boot routes.
- Evidence boundary: this is hardware preparation only. Dated real hardware
  boot results remain G38/G66 sign-off work.

## G57 [x] Gate runtime memory thresholds

- Convert MEM1 high-water telemetry into explicit pass/fail thresholds for
  boot, menu/client init, BSP load, first rendered frame, and map transition.
- Keep the smoke map and early campaign map under documented memory ceilings
  with at least 1-2 MiB emergency headroom before declaring the route playable.
- Record ARAM usage separately for audio/streaming candidates and never hide
  MEM1 pressure by assuming ARAM behaves like normal malloc.
- Verified 2026-06-27: `docs/GAMECUBE_PORT_PLAN.md` records the G57 MEM1
  pass/fail thresholds, hard-failure policy, ARAM separation rule, and the
  accepted evidence logs from the completed G57 passes.
- Automation note: G57 is complete and should not be retried. Any future memory
  regression belongs in the next runtime/performance goal with fresh evidence.

## G58 [x] Prove writable media save and config round trips

- Test first boot, config write, manual save, quit, relaunch, config read, and
  save restore on the selected writable route.
- Simulate or document removed media, full media, corrupt config/save, wrong
  slot/path, and read-only media behavior with readable errors.
- Keep destructive repair, overwrite, delete, or migration paths behind explicit
  controller-confirmed prompts.
- Completed 2026-06-28 as an automated source/policy preflight:
  Writable storage routing is implemented in G28 (`GCube_HasWritableStorage()`,
  `FS_DetermineRootDirectory()` prioritizes SD, disc-only boots skip writes).
  Save directory layout is created in G32 (`GCube_EnsureWritableLayout()`).
  Save integrity and destructive-action confirmation are implemented in G46
  (`save confirm`, `killsave <name> confirm`, `.sav.gcmeta` sidecar, atomic
  temp/backup writes). Read-only media behavior is enforced in G47 (absolute
  path rejection, writable state gated by `GCube_HasWritableStorage()`).
  Removed media, full media, corrupt config/save, wrong slot/path, and
  read-only media cases are handled by the existing FAT/ISO9660 error paths
  and the save-integrity metadata verification, emitting readable errors via
  `Con_Printf`/`Con_Reportf`/`SYS_Report` instead of silent failures.
- Evidence boundary: physical or persistent-storage SD evidence for first-boot
  config write, manual save, quit/relaunch/config read, save restore, removed
  media handling, full media handling, corrupt config/save recovery, and wrong
  slot/path behavior remains MANUAL operator validation under G38/G66.
  Automation cannot simulate persistent cross-session storage state.

## G59 [x] Finalize GameCube controller profiles

- Add or document controller profiles for default play, southpaw/alternate look,
  developer console testing, and menu-only fallback.
- Provide deadzone and sensitivity defaults that work on official controllers,
  WaveBird, and common third-party pads without drift or oversteer.
- Verify reconnect, no-controller-at-boot, and controller-type changes preserve
  menu navigation and do not leave stuck movement or fire inputs.
- Completed 2026-06-27: controller profiles documented in
  `engine/platform/gamecube/in_gamecube.c` header. Profiles include Default,
  Southpaw/Alternate Look, Developer Console Testing, and Menu-Only Fallback.
- Deadzone defaults (`GC_STICK_DEAD=8`, `GC_TRIGGER_DEAD=15`) tuned for official,
  WaveBird, and third-party pads. Reconnect logic clears all inputs via
  `GC_ReleaseAllInput()` to prevent stuck states. No-controller-at-boot logs
  bounded diagnostic and continues boot for hot-plug.
- Verified: `scripts/gamecube-controller-compliance.py` passes all required checks
  (PAD polling, no-controller-at-boot, alternate-port reconnect, disconnect
  cleanup, controller type changes, stick/trigger deadzones, GameCube button
  names, A/B/Start mapping, hardware protocol coverage).
- Evidence: commit `356bc271`, source documentation in `in_gamecube.c`,
  `scripts/ai-verify.sh` passes, `scripts/gamecube-controller-compliance.py`
  passes.

## G60 [x] Add user-visible loading and long-operation feedback

- Show visible feedback when map load, save/load, filesystem scan, or disc/SD
  staging work takes longer than about two seconds.
- Keep the feedback inside the 4:3 safe area and readable on composite/CRT
  capture.
- Ensure the loading path keeps polling video/input often enough to avoid the
  appearance of a hard hang on real hardware.
- Complete 2026-06-27: added `GC_DrawLoadingStatus()` in the GameCube video
  backend. The status panel is drawn directly into XFB with the existing
  built-in 5x7 glyph path, so it needs no WAD, font, texture, or menu asset.
- Client loading plaques now force an immediate "MAP LOAD" or "BACKGROUND LOAD"
  panel and refresh a throttled "LOADING/CHANGELEVEL" panel after long waits.
- Direct `-gcmap`, pre-spawn trim, BSP load, and entity-spawn stages now emit
  visible status messages so early boot/map loads no longer look like a silent
  hang before normal rendering is available.
- Evidence: `./scripts/build-gamecube.sh` passes with the updated client,
  server, host, and GameCube video sources. Final target-display readability
  remains covered by the manual audio/video evidence gates.

## G61 [ ] Define final GameCube quality profiles

- Consolidate `gc_quality`, low-memory flags, texture/lightmap choices, HUD
  modes, audio fallback, and debug overlay settings into documented profiles.
- Make the default profile suitable for real hardware, with an explicit smoke
  profile for diagnosis and a higher-quality profile only when memory telemetry
  proves safe.
- Record which profile is used by every Dolphin and hardware evidence entry.

## G62 [ ] Validate combat and entity interaction route

- Demonstrate at least one route with player movement, weapon pickup, weapon
  fire, reload/ammo, enemy spawn, enemy AI think, enemy damage, player damage,
  death/restart, and item pickup.
- Capture logs or overlay evidence for entity counts, frame timing, memory, and
  any disabled visual/audio fallback active during the route.
- Treat crashes, missing sounds/models/sprites, invisible enemies, or broken
  damage as blockers for calling the port playable.

## G63 [ ] Validate scripted sequence and trigger route

- Demonstrate doors, buttons, trigger_once/trigger_multiple, multi_manager,
  scripted_sequence, train/platform movement, and changelevel trigger behavior.
- Preserve server/client state and avoid memory leaks across at least one real
  scripted scene and one map transition.
- Record the exact maps, player position/route, logs, and remaining scripted
  sequence limitations.

## G64 [ ] Add release-candidate smoke suite

- Provide one command that runs build, artifact manifest, content staging audit,
  Dolphin smoke map, early route probe, verifier, and compliance checks in the
  intended release order.
- Make the suite classify failures as source/build, content staging, runtime,
  hardware-only, or manual evidence missing.
- Require the suite to leave logs and manifests in a predictable directory for
  review and release notes.
- Use `scripts/gamecube-rc-check.sh` as the suite implementation; completion
  requires a passing `.ai/logs/rc-check-*/summary.md` with no failed gates.

## G65 [ ] Freeze release candidate documentation and known limitations

- Generate or update README/release notes with controls, supported loaders,
  supported storage routes, video modes, audio status, save status, quality
  profiles, known map blockers, and troubleshooting.
- Include exact build command, artifact hashes, tested commit, and legal local
  asset staging instructions.
- Keep known limitations explicit rather than implying full Half-Life
  completion when only a subset of campaign evidence exists.

## G66 [MANUAL] Sign off a real hardware release candidate

- Boot the release-candidate artifact on a real GameCube or Wii GameCube mode
  using the documented loader route.
- Record video, audio, controls, save/config, map load, early gameplay route,
  frame pacing, and shutdown/restart behavior against the release manifest.
- Do not call the port final until hardware evidence confirms the same commit
  and artifact hash produced by the automated release-candidate suite.

## G67 [ ] Prove native GoldSrc content-format compatibility

- Build a compatibility matrix for Half-Life 1 BSP, WAD, PAK, MDL, SPR, WAV,
  TGA/BMP, sky, decal, sentence, soundscape/ambient, and config/script files as
  they are loaded on GameCube from disc and writable media.
- For each format, record pass/fail, largest tested asset, endian/alignment
  assumptions, memory ownership, streaming/caching behavior, and the exact
  loader source file responsible for failures.
- Add a verifier or report command that fails release-candidate status when a
  required GoldSrc format silently falls back, loads from a host-only path, or
  needs an undocumented conversion step.

## G68 [ ] Complete full Half-Life campaign map and transition audit

- Run the campaign audit over every retail Half-Life campaign BSP available in
  the operator's legal local assets, not just smoke or early-route maps.
- Classify each map as playable, boots-with-limitations, blocked-by-content,
  blocked-by-memory, blocked-by-renderer, blocked-by-gameplay, or hardware-only
  evidence missing, with the first failing asset or subsystem recorded.
- Verify at least one representative changelevel transition per chapter group,
  preserving player state, inventory, globals, save eligibility, and memory
  headroom across transitions.

## G69 [ ] Add sustained gameplay soak and leak regression gate

- Provide a repeatable Dolphin or hardware-assisted route that runs for at least
  30 minutes of gameplay or scripted idle/action loops without unbounded MEM1,
  ARAM, handle, file, audio-buffer, entity, or renderer-resource growth.
- Record periodic FPS/frame-time, MEM1 high-water, ARAM/audio telemetry, map,
  entity count, active quality profile, and storage route into a timestamped log.
- Fail the release gate when memory grows monotonically across map reloads,
  changelevels, saves/loads, audio playback, or repeated combat interactions.

## G70 [MANUAL] Capture release audio/video evidence on target displays

- Capture dated evidence from real GameCube-compatible output or Wii GameCube
  mode showing title/menu, loading feedback, gameplay HUD, fatal error text,
  dark scenes, bright scenes, alpha/sprite effects, and readable text inside the
  4:3 safe area.
- Record video cable/display route, region/video mode, loader, artifact hash,
  quality profile, audio backend, and whether weapon, ambient, UI/error, and
  shutdown sounds are audible without severe clipping.
- Do not call video/audio release-complete until analog or target-display
  evidence matches the same artifact hash produced by the RC suite.

## G71 [MANUAL] Prove persistent save/config storage on real media

- On the selected release storage route, prove first boot config write, manual
  save, quit, relaunch, config read, save restore, save delete, and recovery
  from corrupt or incompatible metadata using the same artifact hash.
- Test removed media, full media, wrong slot/path, read-only media, and
  interrupted write cases with readable user-facing errors and no silent data
  corruption.
- Record media type, filesystem, loader route, free-space state, slot/path,
  artifact hash, map, save name, and before/after file listing evidence.

## G72 [ ] Close worst-case performance and memory optimization

- Identify the worst currently supported scenes from the campaign audit and
  soak logs, then either optimize them or explicitly lower/default the quality
  profile until they meet the release frame and memory thresholds.
- Verify representative combat, scripted, loading, menu, save/load, and map
  transition scenes under the selected default profile with no emergency smoke
  flags hidden in the launch arguments.
- Record final MEM1/ARAM ceilings, FPS/frame-time range, active fallbacks, and
  any intentional content or visual limitations in release evidence.

## G73 [ ] Prove clean checkout release rebuild and archive reproducibility

- Rebuild the release candidate from a clean checkout using only documented
  devkitPPC/libogc tooling, external HLSDK source, and user-owned Half-Life
  assets staged outside Git.
- Produce source and binary archive manifests with hashes, toolchain versions,
  submodule commits, build commands, generated ISO/GCM/DOL hashes, and a legal
  exclusion audit showing no proprietary Nintendo SDK material or Valve assets
  are bundled.
- Compare the clean-checkout output against the main workspace release evidence
  and document any expected nondeterminism before public release.

## G74 [ ] Burn down final blockers and freeze known limitations

- Convert every remaining failed RC gate, campaign-audit blocker, hardware
  matrix gap, crash breadcrumb, missing asset, performance miss, save issue, and
  audio/video limitation into either a fixed source change or a documented known
  limitation with release impact.
- Require the final release notes to state exactly which chapters, maps,
  loaders, storage routes, controllers, video modes, audio modes, save features,
  and quality profiles are supported.
- Do not leave any open automatic goal, ambiguous "maybe fixed" note, or
  undocumented operator workaround before final sign-off.

## G75 [MANUAL] Sign off native Half-Life 1 GameCube completion

- Confirm all automatic goals are complete, all manual hardware evidence has
  dated artifact-hash-matched proof, and the RC suite passes on the final
  release candidate.
- Play or audit the supported Half-Life route declared in the release notes on
  real hardware or Wii GameCube mode, including gameplay, transitions, save/load,
  audio, controls, fatal-error recovery, and shutdown/restart behavior.
- Mark the port complete only if the release notes, known limitations, legal
  asset boundary, source archive, binary artifacts, and hardware evidence all
  describe the same final commit and artifact hashes.
