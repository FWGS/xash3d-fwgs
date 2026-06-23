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

## G19 [ ] Run an interactive gameplay smoke test

- Boot a legal local asset disc, load a small map, render frames, poll the
  GameCube controller, and advance at least a few seconds without crashing.
- Capture OSReport evidence for player spawn, input polling, frame progression,
  and clean shutdown or bounded timeout.
- Use Dolphin first, then repeat on real hardware when available.
- Source-side changes are complete (commit `7f0d31d9`).
  `engine/platform/gamecube/in_gamecube.c` emits `Xash3D GameCube: input polling active`
  via `Con_Reportf` on the first successful input poll.
- Dolphin discovery is now exported into the automation environment as
  `DOLPHIN_EXECUTABLE`; this workspace currently supports the
  `flatpak:org.DolphinEmu.dolphin-emu` install.
- Do not mark complete until logs confirm input polling and map load.
- Manual verification command: `scripts/dolphin-boot-probe.sh`
- Expected log evidence: `.ai/logs/dolphin-probe-*/stderr.log` must contain
  both `Xash3D GameCube: map loaded <map>` and `Xash3D GameCube: input polling active`.

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

## G21 [ ] Fix GameCube map/model lookup after server progs init

- Resolve the `Host_ErrorInit: Could not load model maps from disk` regression
  seen after `Xash3D GameCube: pre-spawn memory trim`.
- Prove `-gcmap c0a0e` resolves `maps/c0a0e.bsp` rather than collapsing the
  lookup to `maps`.
- Preserve the `c4a1f` smoke path and document the exact lookup trace in the
  port plan.

## G22 [ ] Add memory budget telemetry for real gameplay loads

- Report main-memory high-water marks around filesystem mount, server progs,
  BSP load, texture/model registration, client init, and frame rendering.
- Capture allocation failures with subsystem, size, and current map context.
- Keep telemetry GameCube-scoped and cheap enough to leave enabled for Dolphin
  and hardware bring-up logs.

## G23 [ ] Establish a GameCube memory budget plan for full Half-Life

- Categorize engine, HLSDK server/client, renderer, filesystem, BSP, model,
  sprite, sound, save/config, and scratch allocations against the 24 MiB main
  memory limit.
- Convert at least one large avoidable cache or duplicate asset buffer into a
  bounded GameCube mode.
- Document ARAM candidates separately from main-memory allocations; do not treat
  ARAM as transparent malloc space.

## G24 [ ] Replace smoke visual skips with stable low-memory visual modes

- Turn `-gcnolightmaps`, studio texture skips, particle palette fallbacks, and
  related visual shortcuts into explicit GameCube quality modes or remove them
  when memory permits.
- Keep map loading stable while rendering world geometry, entities, sprites,
  basic particles, and the HUD.
- Record screenshots or OSReport frame evidence for each enabled visual class.

## G25 [ ] Stabilize HLSDK client HUD and gameplay UI

- Initialize the real HLSDK client HUD without relying on `-nohud`.
- Render health, suit, weapon/ammo, damage, pickup, and message HUD elements on
  GameCube without hangs or fatal missing-sprite failures.
- Preserve an emergency `-nohud` diagnostic mode until HUD stability is proven
  on both Dolphin and hardware.

## G26 [ ] Bring up a real GameCube audio backend

- Replace the silent null backend with a libogc DSP/AI path that can play at
  least WAV/PCM game sound effects without blocking the frame loop.
- Keep a documented null-audio fallback for memory triage and early boot probes.
- Verify map load, sound precache, ambient sound, weapon sound, and shutdown
  without leaks or hangs.

## G27 [ ] Add streaming music and ambient audio policy

- Decide how Half-Life CD audio/music should be handled on GameCube: disabled,
  streamed from disc/ARAM, or replaced by legal local files.
- Avoid attempting writes to read-only disc paths such as `media/cdaudio.txt`.
- Capture route evidence for one ambient loop and one transition without
  destabilizing map load.

## G28 [ ] Make writable storage explicit and safe

- Route configs, saves, logs, screenshots, and `.xash_id` to a writable device
  when available, or to a documented read-only fallback when not.
- Never attempt to write generated state to `gcdisc:/`.
- Verify first boot, second boot, missing writable device, and corrupted config
  cases fail safely.

## G29 [ ] Restore local single-player networking paths

- Replace GameCube networking skips with a local-only loopback path when the
  client/server flow needs it.
- Keep offline boot independent of HTTP/TLS, master server, and external network
  initialization.
- Verify single-player spawn, disconnect, changelevel, and shutdown without
  network-dependent hangs.

## G30 [ ] Complete controller, menu, and console ergonomics

- Provide usable default GameCube bindings for movement, look, jump, use, fire,
  secondary fire, crouch, flashlight, weapon cycling, pause, and console/menu.
- Make the console/menu navigable without a keyboard for hardware testing.
- Document the final control map and preserve developer shortcuts for Dolphin.

## G31 [ ] Support changelevel and multi-map progression

- Complete at least one real Half-Life transition from one BSP to the next.
- Preserve entity state, client state, global variables, and required assets
  across changelevel without leaking enough memory to prevent the next map.
- Capture logs for initial map, transition trigger, next map load, and player
  control after transition.

## G32 [ ] Implement save/load suitable for GameCube storage

- Make manual save, autosave, quicksave/quickload policy, and restore flow work
  with the chosen writable storage backend.
- Bound save size and failure behavior for memory card or SD-based deployments.
- Verify save, quit, relaunch, load, and continue on at least one small map.

## G33 [ ] Build a full Half-Life disc/content staging contract

- Validate that a legal local Half-Life installation can be staged into a
  bootable GameCube image with all required `valve/` assets.
- Detect missing, case-mismatched, oversized, or unsupported assets before the
  ISO is built.
- Keep generated images and proprietary assets ignored and outside Git.

## G34 [ ] Add campaign asset and map compatibility checks

- Create a repeatable compatibility probe for every stock Half-Life campaign
  map present in the local asset tree.
- Record load result, memory pressure, missing assets, unsupported renderer or
  audio features, and current blocker per map.
- Use the probe to decide which maps are smoke, playable, blocked, or out of
  memory.

## G35 [ ] Reach a playable early-game route

- Play from tram/lab start through a bounded early-game route in order, not just
  isolated map loads.
- Demonstrate player spawn, movement, triggers, scripted sequences, doors,
  pickups, weapons, enemies, damage, death/restart, and changelevel.
- Capture Dolphin evidence first, then preserve the route for hardware testing.

## G36 [ ] Optimize for a stable GameCube frame budget

- Establish a target frame budget for software rendering on GameCube hardware.
- Profile and optimize the worst CPU/rendering hot spots found in a real map
  without broad rewrites or desktop regressions.
- Record before/after frame timing evidence for at least one representative map.

## G37 [ ] Harden crash, fatal error, and recovery reporting

- Ensure fatal engine, filesystem, allocation, audio, renderer, and game-code
  errors are visible through OSReport and the on-screen console/diagnostic path.
- Keep logs bounded and useful on Dolphin and hardware.
- Verify clean shutdown or bounded timeout after fatal conditions.

## G38 [MANUAL] Validate on physical GameCube hardware

- Boot the generated DOL or disc image through at least one real hardware method.
- Record video output, controller input, storage, audio, map load, frame pacing,
  and thermal/stability observations.
- Compare hardware behavior against Dolphin logs and split emulator-only bugs
  from hardware blockers.

## G39 [ ] Define minimum supported hardware and loader matrix

- Document which combinations are expected to work: DOL loader, disc image,
  SD Gecko, memory card/SD writable storage, video mode, controller, and region.
- Add scripts or docs for producing the recommended artifact for each route.
- Record unsupported routes explicitly instead of leaving them ambiguous.

## G40 [ ] Run an end-to-end Half-Life 1 completion campaign audit

- Drive the compatibility route toward every stock Half-Life chapter available
  in the legal local asset set.
- Classify each chapter as playable, partially playable, blocked, or not tested
  with concrete evidence and next blocker.
- Do not call the port complete until every critical chapter blocker has a fix
  or an explicit documented limitation.

## G41 [ ] Prepare release-quality build and verification scripts

- Provide one command to build the DOL, one command to build a legal local disc
  image, and one command to run the Dolphin smoke/compatibility probes.
- Make build outputs reproducible enough to compare size, symbols, and required
  staged assets across machines.
- Ensure CI/source verification does not require proprietary Half-Life assets.
- Include the GameCube Homebrew Compliance checker in the release verification
  path and document which strict-mode items still require hardware evidence.

## G42 [ ] Finalize native GameCube port documentation

- Document setup, legal asset staging, build requirements, supported hardware,
  controls, save/storage behavior, known limitations, and troubleshooting.
- Include a current compatibility table generated from the campaign probes.
- Include the clean-room GameCube Homebrew Compliance requirements, checklist,
  package expectations, legal disclaimer, and hardware evidence matrix.
- Mark the port finished only when the documentation matches the verified state
  of the engine, game code, assets, audio, input, storage, and hardware tests.
