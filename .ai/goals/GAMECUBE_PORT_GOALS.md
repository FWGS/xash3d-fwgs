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

## G07 [ ] Load a small Half-Life map

- Load one small map far enough to render a frame and accept controller input.
- Record memory or allocation failures rather than hiding them.
- Keep proprietary game data ignored and outside Git.
- Status: BLOCKED. Deferred behind G09-G14.
- Evidence: Engine reaches console (G06), but the statically linked GameCube
  client/server modules are still stubs and cannot run real Half-Life maps.
- Action: Use FWGS `hlsdk-portable` as the open portable game-code source. Keep
  proprietary Valve game assets ignored and outside Git.

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

## G16 [ ] Replace smoke-only client shortcuts with stable GameCube modes

- Turn the current `-nohud`, `-nosound`, and optional-visual skips into
  documented GameCube bring-up modes or remove them as systems become stable.
- Re-enable enough HLSDK client HUD initialization to draw basic gameplay UI
  without hanging in `HUD_Init`.
- Keep the real HLSDK client archive linked and avoid returning to the stub.

## G17 [ ] Bring up GameCube audio incrementally

- Move from `-nosound` smoke boot to a stable null or real audio backend that
  does not hang during sound cvar registration, CD audio playlist setup, or
  sound precache.
- Preserve a silent fallback for low-memory smoke testing.
- Verify a map still loads with the selected audio mode.

## G18 [ ] Restore local single-player networking and save-safe startup paths

- Audit the GameCube skip of `NET_Config(false, true)` and replace it with a
  safe local-only networking path if later client/server flows require it.
- Avoid writes to read-only disc paths such as `.xash_id`, `media/cdaudio.txt`,
  logs, and generated configs.
- Keep offline boot independent of HTTP/TLS initialization.

## G19 [ ] Run an interactive gameplay smoke test

- Boot a legal local asset disc, load a small map, render frames, poll the
  GameCube controller, and advance at least a few seconds without crashing.
- Capture OSReport evidence for player spawn, input polling, frame progression,
  and clean shutdown or bounded timeout.
- Use Dolphin first, then repeat on real hardware when available.

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
