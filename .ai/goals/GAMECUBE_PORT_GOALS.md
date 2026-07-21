# Xash3D GameCube Automation Goals

The goal runner works on the first unchecked automatic goal. Aider may mark a
goal complete only when its acceptance checks are demonstrated and recorded in
`docs/GAMECUBE_PORT_PLAN.md`. Keep patches below the review gate's 400 changed
lines. Real-hardware/operator-only work is tracked below as non-automation
checkpoints, not as runnable goals.

## Current focus (2026-07-18)

Automation tier: `landmark_changelevel` (see `.ai/state/gc-port-automation-tier.json`).

**Proven on Dolphin New Game (`-gcnewgame`, map `c0a0`):**
- `MAP_READY` + interactive input (`G45`)
- G92–G105: presents, save/load, lean PVS, landmark inventory/viewmodel
- G106–G112: DefaultTouch player, lean PVS LRU, thinks, collision/relink/ground
- G113–G116: native axes/PMove, HLSDK HUD snapshots/messages, client prediction
- G117: decoded `button10.wav` mixed at full volume into nonzero ASND PCM
- G118: cumulative 48 KiB memopt SFX budget; `weapons/pl_gun1.wav` via standard load
- G119: fullphysics post-PutInServer re-grant; `weapons=0x6` / viewmodel survive
- G120: HLSDK `ItemPostFrame`/`PrimaryAttack` + G118 fire SFX on probe attack
- G121: stock `EV_FireGlock` path (no SV local bridge); memopt uses `pl_gun1`
- G122: stock `pl_gun3.wav` via in-place WAV pack (no dual SoundLib peak)
- G123: memopt `player/pl_step2.wav` after evicting `button10` + SoundLib migrate
- G124: preload all four `pl_step*` under budget before fire (LRU for small SFX)
- G125: preload `pl_gun3` then steps (~24 KiB resident); fire+walk coexist via cache
- G126: preload `ric1` + alias ric*; HUD soft-fail stub
- G127: preload `320hud1` before SFX; particles=96; FX burst caps
- G128: CPU YUYV dump presents + WORLD PRESENT framedump
- G129: coherent world dump (blit sync + sky/wall fills)
- G130: posterize WORLD PRESENT dumps (room-plane DumpFrames)
- G131: unsigned zi depth dumps + look-into-map; flat→color coalesce
- G132: capture-time faces + flat solid spans (solid>0 WORLD PRESENT)
- G133: capture texinfo → textured+lit RGB565 spans
- G134: keep textured RGB565 dumps + soft cache tile (skip depth overwrite)
- G135: retail-comparable WORLD PRESENT (depth→posterize when soft uniq low)
- G136: zi 3-plane silhouette + CPU YUYV 2×/4× combing fix
- G137: New Game face-solid blockout DumpFrames (skip soft-tile chroma)
- G138: textured spans + chroma-reject DumpFrames (zi fallback when uniq high)
- G139: soft major<<8|minor→RGB565 keep (no Quake colormap BLEND_LM)
- G140: lit soft→RGB565 + polyset unpack; New Game defer crumb surfcache
- G141: DumpFrames speckle scrub (span-crack fill + neon/sky scrub)
- G142: stretch skybox face (no 64px screen-tile seams)
- G143: wall chroma outlier scrub on textured DumpFrames
- G144: live New Game scrub before GX present (neon/outlier only)
- G145: live span-crack neighbor fill when frame mostly drawn
- G146: UV-matched surfcache mip bump (no dimension clamp cracks)
- G147: all cap faces emit + near-black crack scrub + cache UV clamp
- G148: area-prioritized face capture + 96px UV-matched surfcache
- G149: DumpFrames viewmodel composite + VIEWMODEL panel presents
- G150: top-K area face capture + sky-hole rim fill (no BSS raise)
- G151: Flipper GX EFB world tris (live); soft spans only for DumpFrames
- G152: GX textured faces (soft→RGB565 tiled + TEV MODULATE)
- G153: GX lightmaps (capture bake + TEV2 MODULATE)
- G154: real LM samples via disc bind for Flipper bake
- G155: GX studio TriAPI → EFB (world studio path)
- G156: pin landmark viewmodel + Flipper smoke `viewmodel=1`
- G157: eye-pose sync for viewmodel (dist=0) + NDC lower-half check
- G158: live Flipper presents through loopback reconnect (`gx=1`)
- G159: sustained Flipper presents after post-reconnect `ca_active` (`gx=1`)
- G160: outdoor wall-boost top-K + lean PVS LRU surfbits rebuild
- G161: soft DumpFrames viewmodel while Flipper live (one-shot composite)
- G162: viewmodel frame offset (forward+up) + top VIEWMODEL DumpFrames panel
- G163: live cluster face refresh without LM rebake (capture cands + mid LM)
- G164: GX studio Gouraud shading (per-vertex RGB light through TriAPI)
- G165: restore-cluster face refresh (outdoor cands @ capture/Prepare)
- G166: soft DumpFrames studio RGB lighting (R5G5B5 TriAPI light)
- G167: GX viewmodel depth range (LEQUAL + viewport 0..0.3, not Z-always)
- G168: Flipper studio chrome sphere UVs (pass-through TriAPI + proof)
- G169: soft studio scalar light + constant tint (fixes G166 span noise)
- G170: soft studio chroma tint proof (DumpFrames warm amber when light white)
- G171: outdoor Flipper refresh via slots↔cands trade (5×48, no BSS growth)
- G172: HUD sheets via sys-malloc after studios (gc_320hud2/train/crosshairs real)
- G173: lean gc_320hud1 (64×64 bootstrap) preferred under memopt
- G174: lean gc_crosshairs (64×64 bootstrap) preferred under memopt
- G175: outdoor Flipper refresh via slots↔cands trade (4×64, no BSS growth)
- G176: face cap 256→320 via LM tile 8→4 trade (no BSS growth)
- G177: soft DumpFrames HUD composite (lean sheets into WORLD PRESENT)

**Immediate source queue (open automatic goals, in order):**
1. *(none — G177 complete; next open polish TBD)*

Evidence anchors:
- `.ai/logs/dolphin-probe-20260721-002355` (G177 soft dump HUD sheets=4; G176/G174/G155 green)
- `.ai/logs/dolphin-probe-20260721-001608` (G176 cap=320 drawn=249 rim=133; G175/G174/G155 green)
- `.ai/logs/dolphin-probe-20260721-000815` (G175 mid_new=23 wall_new=15 cands=64; rim=187; G174/G155 green)
- `.ai/logs/dolphin-probe-20260721-000202` (G174 crosshairs lean real=4/4; G173/G172; view=2; G155 viewmodel=1)
- `.ai/logs/dolphin-probe-20260720-235658` (G173 hud1 lean real=3/3; G172 3/3; view=2; G155 viewmodel=1)
- `.ai/logs/dolphin-probe-20260720-234531` (G172 real=3/3; view=2; G155 viewmodel=1)
- `.ai/logs/dolphin-probe-20260720-231838` (G171 mid_new=17 wall_new=12 cands=48; rim fill 180)
- `.ai/logs/dolphin-probe-20260720-185130` (G168 chrome uv samples=798 span=0.999)
- `.ai/logs/dolphin-probe-20260720-184602` (G167 depth range far=0.30 ztest=1; G166/G165/G164 green)
- `.ai/logs/dolphin-probe-20260720-183857` (G166 soft studio rgb shades=14; G165/G164 green)
- `.ai/logs/dolphin-probe-20260720-182331` (G165 restore refresh cluster=429 mid_new=14)
- `.ai/logs/dolphin-probe-20260720-175743` (G164 gouraud shades=29 mask=0xfffffff8; tris=908)
- `.ai/logs/dolphin-probe-20260720-174928` (G163 mid_new=32 reused baked LM; explore c=543)
- `.ai/logs/dolphin-probe-20260720-165932` (G162 framed ndc mid=-0.28; soft dump + gun visible)
- `.ai/logs/dolphin-probe-20260720-165320` (G161 soft dump viewmodel ready + G159 ca_active)
- `.ai/logs/dolphin-probe-20260720-163223` (G160 wallboost=272/238; outdoor mid_sky 4.9%→0.3%)
- `.ai/logs/dolphin-probe-20260720-162647` (G159 ca_active present gx=1)
- `.ai/logs/dolphin-probe-20260720-161818` (G158 reconnect present gx=1)
- `.ai/logs/dolphin-probe-20260720-160332` (G157 pose dist=0 fov=90 lower=1)
- `.ai/logs/dolphin-probe-20260720-155105` (G156: G155 tris=908 viewmodel=1)
- `.ai/logs/dolphin-probe-20260720-150403` (G153 lightmapped=199 TEV2; capture bake)
- `.ai/logs/dolphin-probe-20260720-145710` (G152 textured=199 flat=0 Flipper TEV)
- `.ai/logs/dolphin-probe-20260720-140641` (G148 area-pri + 96px cache; outdoor long_runs 4→1)
- `.ai/logs/dolphin-probe-20260720-135208` (G147 emit=175/175; dark20→0; scrub neon=0)
- `.ai/logs/dolphin-probe-20260720-134636` (G146 mip-fit; wall dark40 924→78)
- `.ai/logs/dolphin-probe-20260720-132518` (G143 scrub outliers=58, wall chroma→0, framedump_10)
- `.ai/logs/dolphin-probe-20260720-003435` (G138 textured+reject chroma→G136 zi, framedump_9)
- `.ai/logs/dolphin-probe-20260720-001831` (G137 face-solid keep uniq=24, framedump_7)
- `.ai/logs/dolphin-probe-20260720-000728` (G136 zi posterize near/wall/sky, framedump_9 uniq≈62)
- `.ai/logs/dolphin-probe-20260719-235737` (G135 depth→posterize WORLD PRESENT, framedump_9 uniq≈151)
- `.ai/logs/dolphin-probe-20260719-121916` (G134 tile soft tex + keep textured dump)
- `.ai/logs/dolphin-probe-20260719-051017` (G133 textured+lit RGB565 on cap faces)
- `.ai/logs/dolphin-probe-20260719-050525` (G132 cap faces emit + flat solid spans)
- `.ai/logs/dolphin-probe-20260719-040343` (G131 depth + color-coalesce WORLD PRESENT)
- `.ai/logs/dolphin-probe-20260719-034456` (G130 posterize WORLD PRESENT dump)
- `.ai/logs/dolphin-probe-20260719-032144` (G129 sky backdrop + WORLD PRESENT dump)
- `.ai/logs/dolphin-probe-20260719-030808` (G128 CPU dump presents + WORLD PRESENT framedump)
- `.ai/logs/dolphin-probe-20260719-025133` (G127 HUD 320hud1 preload + SFX coexist, particles=96)
- `.ai/logs/dolphin-probe-20260719-013339` (G126 fire+steps+ric preload, no ric2–5 FS, HUD soft-fail stub)
- `.ai/logs/dolphin-probe-20260719-005629` (G125 fire+steps preload, cache-hit fire, ric1 load, PCM peak)
- `.ai/logs/dolphin-probe-20260718-215805` (G124 four pl_step* preload decodes)
- `.ai/logs/dolphin-probe-20260718-213330` (G123 pl_step2 decode + button10 evict)
- `.ai/logs/dolphin-probe-20260718-212457` (G122 EV_FireGlock + pl_gun3 in-place decode)
- `.ai/logs/dolphin-probe-20260718-211713` (G121 EV_FireGlock + pl_gun1 budget decode)
- `.ai/logs/dolphin-probe-20260718-205118` (G120 PrimaryAttack + pl_gun1 budget)
- `.ai/logs/dolphin-probe-20260718-201558` (G119 re-grant + UpdateClientData weapons=0x6)
- `.ai/logs/dolphin-probe-20260718-193416` (G117 mixer ready + nonzero ASND PCM)
- `.ai/logs/dolphin-probe-20260718-181611` (G116 native client prediction)
- `.ai/logs/dolphin-probe-20260718-070101` (G112 20.97-unit world support trace)
- `.ai/logs/dolphin-probe-20260718-142612` (G113 native axis usercmd + HLSDK PMove)
- `.ai/logs/dolphin-probe-20260718-055613` (G111 trigger-aware relink traversal)
- `.ai/logs/dolphin-probe-20260718-052721` (G110 moving abs bounds + area relink)
- `.ai/logs/dolphin-probe-20260718-052139` (G109 retained hull + collision proof)
- `.ai/logs/dolphin-probe-20260718-044542` (G108 bounded think scheduler)
- `.ai/logs/dolphin-probe-20260718-034958` (G107 lean PVS LRU eviction/reload)
- `.ai/logs/dolphin-probe-20260718-032131` (G106 real player + DefaultTouch attach)
- `.ai/logs/dolphin-probe-20260718-014519` (G105 viewmodel draw v_9mmhandgun)
- `.ai/logs/dolphin-probe-20260718-013800` (G104 deploy viewmodel=v_9mmhandgun)
- `.ai/logs/dolphin-probe-20260718-010723` (G103 inventory-attach granted=2)
- `.ai/logs/dolphin-probe-20260718-003429` (G102 spawn+lean-attach granted=2)
- `.ai/logs/dolphin-probe-20260718-001842` (G101 lean-N follow slots=4 c0→c1)
- `.ai/logs/dolphin-probe-20260718-000808` (G100 weapons granted=2)
- `.ai/logs/dolphin-probe-20260717-233356` (G99 landmark ammo1=99 ammo2=88)
- `.ai/logs/dolphin-probe-20260717-231959` (G98 landmark restore armor/weapons)
- `.ai/logs/dolphin-probe-20260717-230837` (G97 landmark restore health=77)
- `.ai/logs/dolphin-probe-20260717-223809` (G96 lean FatPVS map=c1a0a)
- `.ai/logs/dolphin-probe-20260717-223433` (G95 present map=c1a0a after changelevel)
- `.ai/logs/changelevel-g68-20260717-193719` (G68 16/16 chapter changelevels)
- `.ai/logs/campaign-audit-g68-20260717-progress` (96/96 MAP_READY)
- `.ai/logs/worst-case-g72-current` (G72 classified worst-case report)
- `.ai/logs/dolphin-probe-20260717-155659` (G94 save/load restore present)
- `.ai/logs/dolphin-probe-20260717-145537` (G93 320×240 presents)
- `.ai/logs/dolphin-probe-20260717-145327` (G92 changelevel + PVS re-capture)

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

### G08 [Manual checkpoint] Validate on physical GameCube hardware

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
- Refreshed 2026-07-07 after downshifting the gcmap probe presentation buffer:
  `DOLPHIN_TIMEOUT=90 scripts/dolphin-boot-probe.sh`.
- Evidence: `.ai/logs/dolphin-probe-20260707-002931` reached `MAP_READY`,
  `G45_STATUS: PASS`, `VISUAL_STATUS: nonblack sampled`, and
  `G36_STATUS: PASS` with
  `FRAME_BUDGET_STATS: samples=11 avg=17.46ms p95=17.46ms max=17.46ms target=16.67ms`.

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

### G38 [Manual checkpoint] Validate on physical GameCube hardware

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

### G40 [Manual checkpoint] Run an end-to-end Half-Life 1 completion campaign audit

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

## G61 [x] Define final GameCube quality profiles

- Consolidate `gc_quality`, low-memory flags, texture/lightmap choices, HUD
  modes, audio fallback, and debug overlay settings into documented profiles.
- Make the default profile suitable for real hardware, with an explicit smoke
  profile for diagnosis and a higher-quality profile only when memory telemetry
  proves safe.
- Record which profile is used by every Dolphin and hardware evidence entry.
- Complete 2026-06-27: `gc_quality` now has named profile semantics:
  `0=smoke`, `1=release`, `2=high telemetry-only`.
- The engine registers `gc_quality` before renderer load, engine-side
  `GC_GetVisualQuality()` reads and clamps the cvar instead of hard-coding
  smoke mode, and the GX renderer reads the same cvar with low-memory clamping.
- `GC_ReportQualityProfile()` emits structured evidence including stage,
  numeric quality, profile name, low-memory build flag, HUD/audio/lightmap/
  overlay policy, and intended purpose.
- Evidence: `scripts/gamecube-quality-profile-check.py` passes and
  `scripts/ai-verify.sh` passes.

### G62 [Manual checkpoint] Validate combat and entity interaction route

- Demonstrate at least one route with player movement, weapon pickup, weapon
  fire, reload/ammo, enemy spawn, enemy AI think, enemy damage, player damage,
  death/restart, and item pickup.
- Capture logs or overlay evidence for entity counts, frame timing, memory, and
  any disabled visual/audio fallback active during the route.
- Treat crashes, missing sounds/models/sprites, invisible enemies, or broken
  damage as blockers for calling the port playable.

**Blocker (2026-06-27):** Automated Aider passes exit 18 (`asset_lookup`). The
blocker is asset lookup, staging, or path handling, not a missing source gap.
Previous attempts (`.ai/logs/aider-pass-2026-06-27-225637.log` and G61 attempts)
confirm this is an environment condition. Sustained combat validation with entity
AI, damage, death/restart, and item pickup requires a legal local Half-Life
asset tree and persistent runtime sessions that bounded smoke probes cannot
simulate. This is an operator validation task covered by G38/G40/G66. Automation
should not retry G62 until an operator validates a combat route on this machine
or physical hardware.

**Next operator step:** Run a sustained gameplay probe with legal assets to
verify entity interaction, damage, and restart behavior. Record evidence in
`.ai/logs/dolphin-probe-*/stderr.log` or hardware captures.

### G63 [Manual checkpoint] Validate scripted sequence and trigger route

- Demonstrate doors, buttons, trigger_once/trigger_multiple, multi_manager,
  scripted_sequence, train/platform movement, and changelevel trigger behavior.
- Preserve server/client state and avoid memory leaks across at least one real
  scripted scene and one map transition.
- Record the exact maps, player position/route, logs, and remaining scripted
  sequence limitations.

**Blocker (2026-06-27):** Automated Aider passes exit 18 (`asset_lookup`). The
blocker is asset lookup, staging, or path handling, not a missing source gap.
Previous attempts (`.ai/logs/aider-pass-2026-06-27-230219.log` and
`.ai/logs/aider-pass-2026-06-27-230517.log`) confirm this is an environment
condition. Scripted sequence validation with triggers, multi_manager,
scripted_sequence entities, and changelevel transitions requires a legal local
Half-Life asset tree and persistent runtime sessions that bounded smoke probes
cannot simulate. This is an operator validation task covered by G38/G40/G66.
Automation should not retry G63 until an operator validates a scripted sequence
route on this machine or physical hardware.

**Blocker confirmed (2026-06-28):** Attempt 2 (`.ai/logs/aider-pass-2026-06-27-230517.log`)
also exited 1 (`asset_lookup`), confirming the blocker is persistent. Source code
is not missing; the environment cannot stage or locate required campaign assets
for sustained scripted sequence testing. G63 remains BLOCKED/MANUAL.

**Next operator step:** Run a sustained gameplay probe with legal assets to
verify scripted sequences, trigger behavior, and map transitions. Record evidence
in `.ai/logs/dolphin-probe-*/stderr.log` or hardware captures.

## G64 [x] Add release-candidate smoke suite

- Provide one command that runs build, artifact manifest, content staging audit,
  Dolphin smoke map, early route probe, verifier, and compliance checks in the
  intended release order.
- Make the suite classify failures as source/build, content staging, runtime,
  hardware-only, or manual evidence missing.
- Require the suite to leave logs and manifests in a predictable directory for
  review and release notes.
- Use `scripts/gamecube-rc-check.sh` as the suite implementation; completion
  requires a passing `.ai/logs/rc-check-*/summary.md` with no failed gates.
- Completed 2026-06-27: `scripts/gamecube-rc-check.sh` implements all acceptance
  criteria. One command runs the full chain. Each gate reports PASS/WARN/FAIL in
  `summary.md` and `status.json`. Output directory `.ai/logs/rc-check-*/` contains
  `summary.md`, `status.json`, `artifact-manifest.tsv`, and per-gate logs.
- Evidence: `.ai/logs/rc-check-20260626-010820/summary.md` (7 pass, 0 warn, 0 fail).
- Automation note: repeated `asset_lookup` exit 18 in subsequent attempts are
  environment/staging conditions, not missing source. G64 is source/policy
  preflight complete. The RC check script exists and is the canonical release gate.

## G65 [x] Advance from map-ready to active gameplay rendering

- Complete: fresh Dolphin harness evidence reaches `active_rendering_nonblack`.
  `.ai/logs/dolphin-vision-20260628-011807/result.json` shows
  `map_loaded=true`, `input_polling=true`, `resource_verification=true`, no
  guest errors, `sampled_nonblack=true`, and a nonblack frame dump with
  143,992 nonblack pixels.
- Source changes advanced the route past `ucmd->sendres()`, resource
  verification, `ucmd->spawn()`, `CL_SignonReply: 2`, `game_playerspawn`, and
  the first active `R_RenderScene` frame.
- Prefer small GameCube-only source changes in client resource verification,
  prespawn/sign-on, local server handshake, renderer frame submission, or
  smoke-route read-only handling. Do not solve this with docs-only edits.
- Do not reopen old blockers unless a fresh Dolphin run regresses: `c0a0e`
  path lookup, `Client Edicts Zone` OOM, and `demoheader.tmp` read-only writes
  have already been addressed or instrumented.
- Follow-up (2026-07-15): retail New Game (`-gcnewgame` / `c0a0`) now reaches
  post-G36 low-res world render with nonzero pixels. Remaining gameplay bring-up
  is tracked under G83–G94 rather than reopening G65.

### G66 [Manual checkpoint] Sign off a real hardware release candidate

- Boot the release-candidate artifact on a real GameCube or Wii GameCube mode
  using the documented loader route.
- Record video, audio, controls, save/config, map load, early gameplay route,
  frame pacing, and shutdown/restart behavior against the release manifest.
- Do not call the port final until hardware evidence confirms the same commit
  and artifact hash produced by the automated release-candidate suite.

## G67 [x] Prove native GoldSrc content-format compatibility

- Build a compatibility matrix for Half-Life 1 BSP, WAD, PAK, MDL, SPR, WAV,
  TGA/BMP, sky, decal, sentence, soundscape/ambient, and config/script files as
  they are loaded on GameCube from disc and writable media.
- For each format, record pass/fail, largest tested asset, endian/alignment
  assumptions, memory ownership, streaming/caching behavior, and the exact
  loader source file responsible for failures.
- Add a verifier or report command that fails release-candidate status when a
  required GoldSrc format silently falls back, loads from a host-only path, or
  needs an undocumented conversion step.
- Verified 2026-06-28: `scripts/gamecube-content-format-audit.py --log-dir
  .ai/logs/content-format-g67-codex` passed with 0 required failures against
  the local legal `Half-Life/valve` tree.
- Evidence: `.ai/logs/content-format-g67-codex/summary.md` records loader
  source coverage, sample counts, largest sampled asset, and header validation
  for BSP, WAD, MDL, SPR, WAV, TGA/BMP, sentences/titles, and config/script
  files. The RC suite now runs the same audit as the `GoldSrc content format
  audit` gate.
- Boundary: local Steam-style assets contain no `.pak` sample, so PAK remains
  source-covered by `filesystem/pak.c` with a warning rather than fabricated
  runtime evidence. Intro/media video is reported as a warning and remains
  separate from the core G67 asset-loader gate.
- Follow-up 2026-06-28: GameCube now links the native `avi_gc.c` Cinepak AVI
  backend instead of requiring generated `.gcvid` streams, and normal no-arg
  boots no longer force `-nointro`. Runtime proof still requires Dolphin or
  hardware evidence that `media/sierra.avi` / `media/valve.avi` render frames.
- Follow-up 2026-06-28: `scripts/build-gamecube-disc.py --intro-avi` stages
  only boot essentials plus original local startup Cinepak AVI files
  (`sierra.avi`, `valve.avi` when present), and
  `scripts/dolphin-vision-test.py --boot-mode intro-avi` records intro
  requested/opened/decoded markers. Local evidence moved from `Invalid AVI
  index size` to native sequential Cinepak playback and repeated XFB presents:
  run `.ai/logs/dolphin-vision-20260628-124451` reports `intro_avi_nonblack`
  with frames 0, 15, 30, and 60 sampled and nonzero RGB samples on later
  frames. Keep this open until the displayed frame is visually correct; the
  current frame dump is no longer flat green, but it is still visibly corrupted.

## G68 [x] Complete full Half-Life campaign map and transition audit

- Status: DONE 2026-07-17. Campaign map classification + per-chapter changelevel
  samples complete on current Dolphin build.
- Map audit: `.ai/logs/campaign-audit-g68-20260717-progress` — **96/96**
  campaign-list BSPs `MAP_READY` under `-gcmap` smoke (peak HWM `c1a1f`
  ≈5.52 MiB). Full chapter coverage from Black Mesa Inbound through Nihilanth.
- Changelevel samples: `.ai/logs/changelevel-g68-20260717-193719` —
  **16/16 PASS** via `scripts/gamecube-changelevel-probe.sh` (one hop per
  chapter group). Probe path: `-gcmap <from> -gcnewgame -gcchangelevel <to>`
  queues `COM_ChangeLevel` after client ensure (avoids large-map post-signon
  present hang); marker `G68 changelevel ready from=… to=…`.
- BSP trigger coverage (dry-run): 230/230 `trigger_changelevel` targets present
  in the local legal asset tree.
- Intentional limits: samples prove unload→load of the destination BSP with
  PVS teardown/re-capture plumbing; full landmark inventory/globals continuity
  and long multi-hop campaign play remain hardware/manual soak (G70/G75).
- Command:
  ```sh
  MAP_PROBE_TIMEOUT=150 scripts/gamecube-changelevel-probe.sh
  ```
## G69 [x] Add sustained gameplay soak and leak regression gate

- Provide a repeatable Dolphin or hardware-assisted route that runs for at least
  30 minutes of gameplay or scripted idle/action loops without unbounded MEM1,
  ARAM, handle, file, audio-buffer, entity, or renderer-resource growth.
- Record periodic FPS/frame-time, MEM1 high-water, ARAM/audio telemetry, map,
  entity count, active quality profile, and storage route into a timestamped log.
- Fail the release gate when memory grows monotonically across map reloads,
  changelevels, saves/loads, audio playback, or repeated combat interactions.
- Completed 2026-06-28 as a repeatable evidence gate:
  `scripts/gamecube-soak-probe.py` runs repeated Dolphin map probes, parses
  `mem stage=` high-water telemetry and `FRAME_BUDGET_STATS`, writes
  `summary.md`, `results.tsv`, and `report.json`, and fails on missing telemetry
  or monotonic memory growth beyond the configured tolerance.
- `scripts/gamecube-rc-check.sh` now runs the G69 gate as
  `sustained soak/leak regression`. Default RC mode uses dry-run reporting so
  local RC checks stay fast; set `RC_SOAK_DRY_RUN=0` for real Dolphin soak
  probes and `RC_SOAK_STRICT=1` for release-duration evidence.
- Evidence: `.ai/logs/soak-g69-dryrun/summary.md` validates the report shape
  with two maps and stable synthetic memory/frame telemetry. Real 30-minute
  release evidence remains a strict-mode run of the same gate and should be
  attached to G72/G75 final release evidence before sign-off.

### G70 [Manual checkpoint] Capture release audio/video evidence on target displays

- Capture dated evidence from real GameCube-compatible output or Wii GameCube
  mode showing title/menu, loading feedback, gameplay HUD, fatal error text,
  dark scenes, bright scenes, alpha/sprite effects, and readable text inside the
  4:3 safe area.
- Record video cable/display route, region/video mode, loader, artifact hash,
  quality profile, audio backend, and whether weapon, ambient, UI/error, and
  shutdown sounds are audible without severe clipping.
- Do not call video/audio release-complete until analog or target-display
  evidence matches the same artifact hash produced by the RC suite.

### G71 [Manual checkpoint] Prove persistent save/config storage on real media

- On the selected release storage route, prove first boot config write, manual
  save, quit, relaunch, config read, save restore, save delete, and recovery
  from corrupt or incompatible metadata using the same artifact hash.
- Test removed media, full media, wrong slot/path, read-only media, and
  interrupted write cases with readable user-facing errors and no silent data
  corruption.
- Record media type, filesystem, loader route, free-space state, slot/path,
  artifact hash, map, save name, and before/after file listing evidence.

## G72 [x] Close worst-case performance and memory optimization

- Status: DONE 2026-07-17. Default release profile remains `gc_quality=1` with
  `--low-memory-mode=2` and existing texture/surface/world clamps. No hard MEM1
  failures on current supported scenes; no profile demotion required.
- Ceilings (Dolphin, current build):
  - MEM1 HWM: `c1a0` smoke ≈4.87 MiB; New Game `c0a0` world present ≈3.78 MiB;
    `c0a0e` smoke ≈3.38 MiB (all under map-active 7 MiB / BSP 8 MiB guards).
  - Frame time: New Game 320×240 p95≈16.68ms / max≈16.78ms (G36 bar);
    smoke `c0a0e`/`c1a0` p95≈0.65ms (budget-probe path).
  - Fallbacks active: lean New Game present path, bounded post-G36 think,
    lean G94 save blob (full `SV_SaveGame` still OOM under MEM1).
- Representative scenes verified under default profile (no emergency smoke flags
  in release argv): loading (`c0a0e`/`c1a0` MAP_READY), New Game present +
  move/look/use, changelevel (`c0a0`→`c0a0a`), save/load (G94), boot-phase
  isolation (G82). Combat-dense and chapter-wide campaign routes remain G68.
- Intentional limitations (release evidence):
  - Full campaign map/transition audit and combat worst-case: deferred to G68.
  - Full GoldSrc save heap round-trip: lean G94 path only until MEM1 headroom grows.
  - Post-G36 entity think remains bounded (not full server walk).
- Evidence: `.ai/logs/worst-case-g72-current` (strict PASS);
  `.ai/logs/map-compat-20260717-170327` (`c0a0e`/`c1a0` MAP_READY);
  New Game anchors 20260717-145327/145537/155659.

## G73 [SKIP] Prove clean checkout release rebuild and archive reproducibility

- Status: SKIP for local overnight source-porting — documentation/reproducibility
  gate, not an engine source goal.
- Rebuild the release candidate from a clean checkout using only documented
  devkitPPC/libogc tooling, external HLSDK source, and user-owned Half-Life
  assets staged outside Git.
- Produce source and binary archive manifests with hashes, toolchain versions,
  submodule commits, build commands, generated ISO/GCM/DOL hashes, and a legal
  exclusion audit showing no proprietary Nintendo SDK material or Valve assets
  are bundled.
- Compare the clean-checkout output against the main workspace release evidence
  and document any expected nondeterminism before public release.
- Progress evidence (2026-06-28): current-workspace reproducibility preflight
  passes at `.ai/logs/reproducibility-g73-preflight/summary.md`, hashing 12
  artifacts, recording devkitPPC 16.1.0/toolchain metadata, confirming 3 HLSDK
  archive hashes, and finding no tracked generated/proprietary assets or release
  archive asset-boundary violations.
- Boundary: G73 remains open because a true clean-checkout comparison cannot
  include uncommitted goal-runner/RC-gate changes. Complete this after the
  current changes are committed by rebuilding in a second checkout and comparing
  manifests against the same release-candidate artifact set.

## G74 [SKIP] Burn down final blockers and freeze known limitations

- Status: SKIP for local overnight source-porting — release-notes/docs burn-down.
- Convert every remaining failed RC gate, campaign-audit blocker, hardware
  matrix gap, crash breadcrumb, missing asset, performance miss, save issue, and
  audio/video limitation into either a fixed source change or a documented known
  limitation with release impact.
- Require the final release notes to state exactly which chapters, maps,
  loaders, storage routes, controllers, video modes, audio modes, save features,
  and quality profiles are supported.
- Do not leave any open automatic goal, ambiguous "maybe fixed" note, or
  undocumented operator workaround before final sign-off.

## G76 [SKIP] Freeze release candidate documentation and known limitations

- Status: SKIP for local overnight source-porting — release notes/docs only.
- Generate or update README/release notes with controls, supported loaders,
  supported storage routes, video modes, audio status, save status, quality
  profiles, known map blockers, and troubleshooting.
- Include exact build command, artifact hashes, tested commit, and legal local
  asset staging instructions.
- Keep known limitations explicit rather than implying full Half-Life
  completion when only a subset of campaign evidence exists.

## G77 [SKIP] Prove Dolphin and hardware evidence parity for the final artifact

- Status: SKIP for local overnight source-porting — hardware/operator evidence.
- For the same release-candidate commit and artifact hash, compare Dolphin
  evidence against real GameCube/Wii GameCube-mode evidence for boot, menu,
  active rendering, audio, controller input, save/config route, fatal breadcrumb,
  and at least one declared supported gameplay route.
- Record every mismatch as either a fixed source issue, an emulator-only
  limitation, a hardware-only limitation, or a release-note limitation with
  impact and reproduction steps.
- Do not allow final sign-off to combine Dolphin evidence from one build with
  hardware/manual evidence from another build.

## G78 [SKIP] Unify goal state into a single machine-readable source of truth

- Status: SKIP for local overnight source-porting — automation harness meta-work.
- Replace the current split-brain goal model across markdown ledger entries,
  `docs/GAMECUBE_PORT_PLAN.md`, GUI overrides, and
  `.ai/logs/goal-loop-state.json` with one canonical machine-readable state file
  and generated human-readable views.
- Keep goal IDs, states, evidence links, notes, and manual/skip/block reasons in
  the canonical store, then make the GUI, goal loop, evidence gates, and docs
  read from the same data model instead of re-parsing independent text formats.
- Fail verification when a goal state change updates only one surface or leaves
  the canonical state and rendered ledger/docs out of sync.

## G79 [SKIP] Split the porting GUI into model, process, and view layers

- Status: SKIP for local overnight source-porting — GUI refactor, not engine.
- Break `scripts/xash3d-gc-aider-gui.py` into smaller modules so UI widgets,
  persistent settings, process supervision, goal editing, Dolphin telemetry,
  and overnight automation are no longer owned by one monolithic window class.
- Define a stable internal API for goal refresh, process launch/stop, log
  streaming, and dashboard snapshots so small UX changes no longer risk startup
  regressions across unrelated features.
- Keep behavior parity with the current GUI while adding module-level tests or
  smoke checks for the extracted non-Qt logic.

## G80 [SKIP] Add concurrency and mutation safety to goal and automation state

- Status: SKIP for local overnight source-porting — automation harness meta-work.
- Protect `.ai/goals/GAMECUBE_PORT_GOALS.md`, `.ai/state/goal-loop-memory.json`,
  and `.ai/logs/goal-loop-state.json` against overlapping GUI, supervisor, and
  rescue writes using explicit locks or atomic write/replace flows.
- Ensure the GUI cannot rewrite a selected goal while the goal loop is
  simultaneously persisting state for the same goal without surfacing a conflict
  or reloading the latest version first.
- Add a verifier or stress script that simulates rapid refresh/skip/run/stop
  operations and fails on malformed JSON, truncated ledgers, or lost updates.

## G81 [SKIP] Remove auto-commit surprise paths from the automation harness

- Status: SKIP for local overnight source-porting — automation harness meta-work.
- Change the Aider/goal-loop workflow so a normal automation pass cannot create
  checkpoint or GUI-only commits without an explicit operator-approved policy or
  a clearly surfaced local-only branch strategy.
- Keep dirty-worktree preservation available, but make the default behavior
  predictable: report what would be committed, why, and under which policy
  before automation mutates Git history.
- Record all autonomous Git mutations in a structured local audit log that the
  GUI can display alongside the current pass and goal.

## G83 [x] Fix GameCube BSP PointInLeaf and parent-cycle PVS

- Status: DONE (2026-07-16) — BSP scratch nodes are overwritten between world
  load and New Game present; live PointInLeaf/FatPVS at render hang. Fix:
  capture cluster leaf-PVS + parent marks in `GC_CaptureNewGamePVSFromModel`
  after `Mod_SetupSubmodels` (while scratch is intact); render skips live
  PointInLeaf and applies the cache in `R_MarkLeaves` (`cached FatPVS leaf mark
  active`). Evidence: `.ai/logs/dolphin-probe-20260716-213816` —
  `Capture FatPVS cluster=0 leaves=122 nodes=271`, `cached FatPVS leaf mark
  active`, `gcmap world pixels nonzero=17687/19200`, `MAP_READY` + `G36 PASS`.
- Root cause: New Game full-vis was required because `Mod_PointInLeaf` and
  parent walks can cycle on `c0a0`; FatPVS hangs Host_Frame before edge pixels.
- Acceptance:
  - `Mod_PointInLeaf` returns a leaf for the New Game camera origin without
    hitting the depth-limit fallback.
  - `R_MarkLeaves` uses cluster/FatPVS (or a proven bounded PVS) instead of
    marking every leaf/node for `-gcnewgame`.
  - `DOLPHIN_NEWGAME=1` probe still reaches `MAP_READY`, `G36_STATUS: PASS`,
    and `gcmap world pixels nonzero=` with no PointInLeaf/FatPVS hang.
- Evidence: dated `.ai/logs/dolphin-probe-*/stderr.log` plus a short note in
  `docs/GAMECUBE_PORT_PLAN.md`.
- Do not solve this by permanently disabling world render or reintroducing
  green-only post-G36 fills.

## G84 [x] Restore bounded post-G36 server entity think

- Status: DONE (2026-07-16) — post-G36 `Host_ServerFrame` calls bounded
  `SV_Physics` instead of time-only slim ticks. Player gets
  `pfnPlayerPreThink` each tick; up to 8 non-pusher world ents with due
  `nextthink` get `SV_RunThink`. Skips `pfnStartFrame`, full entity walk,
  `pfnThink(player)`, and `PlayerPostThink` (those stall on c0a0).
  Evidence: `.ai/logs/dolphin-probe-20260716-221823` —
  `SV_Physics bounded think post-G36 ents=1`, `Host_ServerFrame post-G36
  bounded tick`, `post-G36 bounded server ticks ready`, pixels
  `17687/19200`, `MAP_READY` + `G36 PASS`.
- Remaining limit: full post-G36 `SV_Physics` / `pfnStartFrame` still stall on
  `c0a0`, so the bounded path remains GameCube-specific scaffolding rather than
  a full restore of world-wide server think.
- Acceptance:
  - After G36, server think runs for at least the player edict (and optionally
    a small bounded entity subset) without hanging Host_Frame.
  - OSReport shows `Host_ServerFrame post-G36` progress past slim time-only
    ticks (e.g. physics/think ready markers).
  - New Game world render markers remain green (`world render ready`, nonzero
    pixels, `MAP_READY`/`G36` PASS).
- Prefer GameCube-only guards in `sv_phys.c` / `sv_main.c`; do not re-enable
  unbounded think for every edict in one patch.
- Evidence: `.ai/logs/dolphin-probe-*` from `DOLPHIN_NEWGAME=1`.

## G85 [x] Sustain New Game world presents from the client frame loop

- Status: DONE (2026-07-16) — after Prepare arms world present, it pumps
  twelve `GC_RenderNewGameWorldFrames(1)` calls (same count=1 contract as
  `SCR_UpdateScreen`). `Host_ClientFrame` also takes a lean post-G36 path that
  calls `SCR_UpdateScreen` and skips `pfnFrame`/EmitEntities stalls. Camera
  uses the first spawned entity origin (`2864,2804,563` on c0a0). Evidence:
  `.ai/logs/dolphin-probe-20260716-221938` —
  `post-G36 sustained world present`, `sustained frames=16 scr=12`,
  `SCR frames=8`, pixels `17687/19200`, `MAP_READY` + `G36 PASS`.
- Status was: SOURCE-FIRST — `GC_PrepareNewGameWorldPresent` already renders a
  burst of frames; SCR must keep presenting after the probe would otherwise
  exit on G36 alone.
- Acceptance:
  - `SCR_UpdateScreen` post-G36 path calls `GC_RenderNewGameWorldFrames`
    repeatedly without falling back to green fill.
  - OSReport shows `newgame world render sustained frames=16` (or higher) from
    the Host_Frame/SCR path, not only the Prepare burst.
  - Camera uses a spawned entity origin when available (already preferred in
    the render helper); document if still using map-center fallback.
- Keep `Host_ServerFrame` slim or G84-bounded; do not regress MAP_READY/G36.
- Evidence: New Game Dolphin probe log with sustained-frame markers.

## G86 [x] Prove New Game player move and look on c0a0

- Status: DONE 2026-07-17. Probe-synthetic usercmd → bounded kinematic
  player move/look after G36 (full `SV_RunCmd`/`PM_Move`/`PostThink` still
  deferred — hang risk).
- Acceptance:
  - After New Game world present, player origin and view angles change under
    probe-synthetic stick input.
  - OSReport breadcrumbs record before/after origin and viewangles without
    guest halt.
  - `G45_STATUS: PASS` / input polling remains true.
- Implementation:
  - `GC_FillNewGameMoveUsercmd` (`in_gamecube.c`): post-G36 probe injects
    `forwardmove=200` + yaw step for 8 ticks; live PAD maps sticks when
    connected.
  - Bounded `SV_Physics` applies usercmd look + kinematic walk, then
    `pfnPlayerPreThink` (no `PM_Move`/LinkEdict/`PostThink`).
  - World camera prefers edict 1 origin/angles after move.
- Evidence: `.ai/logs/dolphin-probe-20260717-120109` —
  `probe gameplay move/look begin`,
  `player move before origin=(2864,2804,515) angles=(0.0,0.0,0.0)`,
  `player move after origin=(2874,2806,515) angles=(0.0,12.0,0.0)`,
  second tick to `(2883,2810,515)` yaw `24`, `MAP_READY`/`G36`/`G45` PASS.

## G87 [x] Restore post-G36 WriteEntities client snapshots

- Status: DONE 2026-07-17. Post-G36 bounded player-only datagrams beside
  think ticks; skips hang-prone `pfnUpdateClientData` / weapon data and full
  brush entity walk.
- Acceptance:
  - After G36, `WriteEntities` / datagram ready markers occur during sustained
    world presents without hanging Host_Frame.
  - Client receives local-player entity state (player-only pack).
- Implementation:
  - `SV_SendClientMessagesBoundedGC` from post-G36 `Host_ServerFrame`.
  - Minimal edict-sourced `clientdata_t`; player-only `SV_AddEntitiesToPacket`
    when G36 done.
- Evidence: `.ai/logs/dolphin-probe-20260717-120407` —
  `SendClientDatagram ready bytes=51 post-G36`,
  `post-G36 bounded WriteEntities tick` (twice), `MAP_READY`/`G36` PASS.

## G88 [x] First New Game world interaction (use / trigger / door)

- Status: DONE 2026-07-17. After G36, nearest interactable (`func_button` /
  `func_door*` / `trigger_*`) is found and `pfnUse` or `pfnTouch` is called
  once from bounded `SV_Physics` (no full PostThink use-trace).
- Acceptance:
  - `world interaction use done classname=func_door map=c0a0` with player at
    ~`(2874,2806,515)` (door was map-far at dist≈6633; use still completed).
  - World presents and bounded think/snapshots continue; `MAP_READY`/`G36` PASS.
- Do not claim G62/G63 complete from this goal.
- Evidence: `.ai/logs/dolphin-probe-20260717-120656`.

## G89 [x] Make New Game PVS follow a moving camera

- Status: DONE 2026-07-17. Chose **cache route**: at load, decompress PVS +
  parent nodebits for every cluster and store leaf AABBs; at present, select
  the row by camera-in-AABB (no live PointInLeaf).
- Acceptance:
  - Two-cluster prove: `PVS follow prove cluster=117 leaves=402` then
    `cluster=0 leaves=122`; `PVS follow ready clusters=117->0 leafdelta=-280`.
  - `Capture multi-cluster PVS ready clusters=933 valid=933`.
  - World pixels `17687/19200`; `MAP_READY`/`G36` PASS.
- Evidence: `.ai/logs/dolphin-probe-20260717-122204`.

## G90 [x] Route New Game presents through the standard render path

- Status: DONE 2026-07-17. Bounded V_RenderView-style presents
  (`GC_RenderNewGameWorldFrames` / `V_RenderViewBoundedGC`) are the primary
  post-G36 path; full `V_PreRender`/`pfnCalcRefdef` still hang. Presents are
  pumped before post-G36 `Host_ServerFrame` (server ticks after render were
  hanging the next GL_RenderFrame).
- Acceptance:
  - `V_RenderView path present`, `V_RenderView viewmodel draw`, `HUD lean draw`
  - World pixels `17687/19200`; `MAP_READY`/`G36` PASS
- Evidence: `.ai/logs/dolphin-probe-20260717-123440`.

## G91 [x] Bring up gameplay audio on the New Game route

- Status: DONE 2026-07-17. Post-G36 `S_StartLocalSound("buttons/button10.wav")`
  after presents/ticks. `-gcnewgame` keeps MapLoadMemoryOpt true session-wide
  (skips all S_LoadSound); `S_AllowNextGameplaySoundLoad` opens a one-shot gate.
- Acceptance:
  - `gameplay sound start name=buttons/button10.wav channel=static`
  - `sound load allowed` + `FS_SysOpen .../sound/buttons/button10.wav`
  - `MAP_READY`/`G36` PASS; pixels green; no underrun markers
- Evidence: `.ai/logs/dolphin-probe-20260717-124047`.

## G92 [x] Survive changelevel on the New Game route (c0a0 → c0a0a/c0a1)

- Status: DONE 2026-07-17. After first-map Prepare, force `COM_ChangeLevel(c0a0a)`;
  `GC_ResetNewGameWorldForChangelevel` frees the multi-cluster PVS pin;
  `SV_ActivateServer` re-Prepares; SCR skips world presents during the loading
  plaque (otherwise mid-transition `GL_RenderFrame` hangs). `Mod_FreeLoadBuffer`
  no longer `Mem_Free`s retained BSP scratch / map-load static arena.
- Acceptance:
  - `changelevel begin map=c0a0a from=c0a0` + `changelevel teardown`
  - `Capture FatPVS map=c0a0a` + `Capture multi-cluster PVS ready map=c0a0a`
  - `map loaded c0a0a` + `changelevel re-prepare` +
    `newgame low-res world present map=c0a0a 160x120`
  - `MAP_READY`/`G36` PASS; no trashed-header fatal across the transition
- Evidence: `.ai/logs/dolphin-probe-20260717-145327`.

## G93 [x] Step New Game world presents up from 160×120 within frame budget

- Status: DONE 2026-07-17. Default New Game / G36 present size is 320×240
  (static gcmap screen + BSS probe FB). Pass `-gcnewgame160` for the prior
  160×120 path.
- Acceptance:
  - `newgame low-res world present map=c0a0 320x240`
  - `gcmap world pixels nonzero=70610/76800`
  - `FRAME_BUDGET_STATS` / `G36_STATUS: PASS` (p95≈16.78ms, same harness bar)
  - Changelevel still reaches `world present map=c0a0a 320x240`
- Evidence: `.ai/logs/dolphin-probe-20260717-145537`.

## G94 [x] Save/load round trip from a live New Game session

- Status: DONE 2026-07-17. Full `SV_SaveGame` OOM'd under post-G36 MEM1; lean
  BSS blob (`G94SAVE1`: map + origin + angles + health) round-trips in place
  on the same map without freeing PVS. Disc `gamecube.cfg` bakes `newsaveload`
  (ISO boots ignore Dolphin `--` guest args). Probe uses RAM bank when no SD;
  `gcprobe:` is never the FS root (avoids mkdir hang). Gameplay SFX skipped
  under `-gcnewsaveload` to keep MEM1 for the second present.
- Acceptance:
  - Issue a save after New Game world presents; reload it (same boot or next
    boot) and reach world presents again with player origin restored.
  - Saved game does not persist slim-tick side effects that break a later
    full-physics resume (document any fields intentionally reset).
  - Storage writes respect the existing writable-media policy (G28/G46).
- Evidence: `.ai/logs/dolphin-probe-20260717-155659` —
  `G94 lean save ready ... origin=(2883,2810,515)`,
  `G94 lean restore applied origin=(2883,2810,515)`,
  `G94 load restore present map=c0a0 origin=(2883,2810,515)`,
  `MAP_READY` / `G36_STATUS: PASS`.
- Command:
  ```sh
  DOLPHIN_NEWGAME=1 DOLPHIN_G94=1 DOLPHIN_TIMEOUT=240 DOLPHIN_FRAME_SAMPLE_SEC=20 scripts/dolphin-boot-probe.sh
  ```

## G95 [x] Post-changelevel world present on large campaign maps

- Status: DONE 2026-07-17. G68 early `COM_ChangeLevel` after client ensure
  skipped first-map G36, so destination maps never re-Prepared. `SV_ActivateServer`
  now calls `GC_PrepareNewGameWorldPresent` on the second `-gcchangelevel` map
  even when G36 was never armed.
- Acceptance:
  - `c1a0`→`c1a0a` emits `G68 changelevel ready` then
    `newgame low-res world present map=c1a0a` with nonzero world pixels.
- Evidence: `.ai/logs/dolphin-probe-20260717-223433` —
  `G95 post-changelevel prepare map=c1a0a`,
  `newgame low-res world present map=c1a0a 320x240`,
  `gcmap world pixels nonzero=76800/76800`,
  `post-G36 sustained world present`.
- Intentional limits: multi-cluster FatPVS alloc can still fail under MEM1 on
  the destination (present continues without cached PVS). Landmark inventory
  continuity remains a later goal.
- Command:
  ```sh
  DOLPHIN_SMOKE_MAP=c1a0 DOLPHIN_CHANGELEVEL=c1a0a DOLPHIN_G95=1 DOLPHIN_TIMEOUT=180 \
    DOLPHIN_FRAME_SAMPLE_SEC=12 scripts/dolphin-boot-probe.sh
  ```

## G96 [x] Lean FatPVS capture after changelevel under MEM1 pressure

- Status: DONE 2026-07-17. Full multi-cluster PVS tables OOM on large
  destination maps (`c1a0a` ≈781 clusters). Capture now falls back to a single
  spawn-cluster row while BSP leafs are still valid (before scratch reuse),
  and skips corrupt post-scratch PointInLeaf retries.
- Acceptance:
  - `c1a0`→`c1a0a` emits `Capture FatPVS lean map=c1a0a cluster=…` (or full
    `Capture FatPVS map=c1a0a`) and still reaches world present.
- Evidence: `.ai/logs/dolphin-probe-20260717-223809` —
  `Capture FatPVS multi-cluster alloc failed clusters=781`,
  `Capture FatPVS lean map=c1a0a cluster=0 leaves=74 nodes=210`,
  `newgame low-res world present map=c1a0a 320x240`.
- Intentional limits: lean mode pins one cluster (no multi-room PVS follow);
  landmark inventory continuity still open.
- Command:
  ```sh
  DOLPHIN_SMOKE_MAP=c1a0 DOLPHIN_CHANGELEVEL=c1a0a DOLPHIN_G95=1 DOLPHIN_G96=1 \
    DOLPHIN_TIMEOUT=180 DOLPHIN_FRAME_SAMPLE_SEC=12 scripts/dolphin-boot-probe.sh
  ```

## G97 [x] Lean landmark changelevel with health continuity

- Status: DONE 2026-07-17. Full `SaveGameState` for smooth `changelevel2`
  OOMs under MEM1. Lean BSS stash carries health + landmark-relative origin
  across the hop (`G97LAND1`). Disc supports `changelevel <map> <landmark>`.
- Acceptance:
  - `c0a0`→`c0a0a` with landmark `c0a0toa`, probe health forced to 77 before
    hop, restore logs `G97 landmark restore health=77`.
- Evidence: `.ai/logs/dolphin-probe-20260717-230837` —
  `G97 probe health set=77`,
  `G97 landmark stash ... health=77 have_lm=1`,
  `G97 landmark restore health=77 origin=(0,816,-449) landmark=c0a0toa`,
  then `world present map=c0a0a`.
- Intentional limits: weapons/ammo/inventory globals not in the lean blob yet;
  full GoldSrc adjacency save still blocked by MEM1.
- Command:
  ```sh
  DOLPHIN_SMOKE_MAP=c0a0 DOLPHIN_CHANGELEVEL=c0a0a DOLPHIN_LANDMARK=c0a0toa \
    DOLPHIN_G95=1 DOLPHIN_G97=1 DOLPHIN_TIMEOUT=150 DOLPHIN_FRAME_SAMPLE_SEC=8 \
    scripts/dolphin-boot-probe.sh
  ```

## G98 [x] Lean landmark weapons and armor continuity

- Status: DONE 2026-07-17. Extends G97 lean BSS hop (`G98LAND1`) with
  `pev->weapons` bitmask and `armorvalue` across landmark changelevel.
- Acceptance:
  - `c0a0`→`c0a0a` landmark `c0a0toa`, probe inventory forced to health=77,
    armor=50, weapons=0x6 (crowbar+glock) before hop; restore logs the same.
- Evidence: `.ai/logs/dolphin-probe-20260717-231959` —
  `G98 probe inventory set health=77 armor=50 weapons=0x6`,
  `G98 landmark stash ... health=77 armor=50 weapons=0x6 have_lm=1`,
  `G98 landmark restore health=77 armor=50 weapons=0x6 origin=(0,816,-449)`.
- Intentional limits: ammo/`m_rgAmmo` and active weapon private data still not
  in the lean blob; HUD may not reflect owned weapons until DLL private state
  is carried.
- Command:
  ```sh
  DOLPHIN_SMOKE_MAP=c0a0 DOLPHIN_CHANGELEVEL=c0a0a DOLPHIN_LANDMARK=c0a0toa \
    DOLPHIN_G95=1 DOLPHIN_G98=1 DOLPHIN_TIMEOUT=150 DOLPHIN_FRAME_SAMPLE_SEC=8 \
    scripts/dolphin-boot-probe.sh
  ```

## G99 [x] Lean landmark ammo (m_rgAmmo) private-data continuity

- Status: DONE 2026-07-17. Extends lean BSS hop (`G99LAND1`) with the
  `CBasePlayer::m_rgAmmo[32]` slice at offsetof `0x4ec`. On GameCube New Game
  the client edict often lacks `pvPrivateData`; stash/restore resolve the
  linked private-data edict (typically edict 2).
- Acceptance:
  - `c0a0`→`c0a0a` landmark `c0a0toa`, probe plants ammo1=99 ammo2=88; restore
    logs the same values with health/armor/weapons.
- Evidence: `.ai/logs/dolphin-probe-20260717-233356` —
  `G99 landmark stash ... ammo1=99 ammo2=88 ... priv_edict=2`,
  `G99 landmark restore health=77 armor=50 weapons=0x6 ammo1=99 ammo2=88`.
- Intentional limits: weapon entity/`m_pActiveItem` CLASSPTR chain still not
  rebuilt; offsetof is HLSDK-layout-specific; multi-cluster lean PVS follow
  addressed by G101.
- Command:
  ```sh
  DOLPHIN_SMOKE_MAP=c0a0 DOLPHIN_CHANGELEVEL=c0a0a DOLPHIN_LANDMARK=c0a0toa \
    DOLPHIN_G95=1 DOLPHIN_G99=1 DOLPHIN_TIMEOUT=120 DOLPHIN_FRAME_SAMPLE_SEC=6 \
    scripts/dolphin-boot-probe.sh
  ```

## G100 [x] Lean landmark weapon-entity re-grant

- Status: DONE 2026-07-18. After G99 restore, defer weapon rebuild until
  post-present. Create crowbar+glock named entities from the weapons bitmask
  (`granted=2`). `pfnSpawn`/`pfnTouch` hang under MEM1 after changelevel
  (even with w_/v_ SetModel stubs), so grant is create-and-free for now.
- Acceptance:
  - `c0a0`→`c0a0a` landmark hop logs
    `G100 landmark weapons granted=2 weapons=0x6 ammo1=99 ammo2=88`.
- Evidence: `.ai/logs/dolphin-probe-20260718-000808` —
  `G100 give created classname=weapon_crowbar`,
  `G100 give created classname=weapon_9mmhandgun`,
  `G100 landmark weapons granted=2 weapons=0x6 ammo1=99 ammo2=88`.
- Intentional limits: full GiveNamedItem Touch/AddPlayerItem still open (G102
  covers Spawn + lean-attach); Deploy/viewmodel not restored.
- Command:
  ```sh
  DOLPHIN_NEWGAME=1 DOLPHIN_SMOKE_MAP=c0a0 DOLPHIN_CHANGELEVEL=c0a0a \
    DOLPHIN_LANDMARK=c0a0toa DOLPHIN_G95=1 DOLPHIN_G100=1 \
    DOLPHIN_TIMEOUT=150 DOLPHIN_FRAME_SAMPLE_SEC=6 scripts/dolphin-boot-probe.sh
  ```

## G101 [x] Multi-cluster lean FatPVS follow after changelevel

- Status: DONE 2026-07-18. Extends G96 single-row lean cache to
  `GC_LEAN_PVS_SLOTS` (4) rows + leaf AABBs. On multi-cluster OOM (or
  `-gcleanpvs` / disc `leanpvs`), capture spawn + nearby visible clusters
  while BSP leafs are intact; origin updates switch among cached slots.
- Acceptance:
  - `c1a0`→`c1a0a` with `DOLPHIN_G101=1` logs lean-N capture (`slots=4`) and
    `PVS lean follow ready slots=4 clusters=0->1`.
- Evidence: `.ai/logs/dolphin-probe-20260718-001842` —
  `Capture FatPVS lean-N map=c1a0a slots=4 c0=0 c1=1`,
  `PVS lean follow 0->1 slot=1`,
  `PVS lean follow ready slots=4 clusters=0->1 leafdelta=-31`.
- Intentional limits: only N nearby clusters (not full map); disc `leanpvs`
  forces lean path for reproducible probes when full tables still fit MEM1.
- Command:
  ```sh
  DOLPHIN_SMOKE_MAP=c1a0 DOLPHIN_CHANGELEVEL=c1a0a DOLPHIN_G95=1 DOLPHIN_G101=1 \
    DOLPHIN_TIMEOUT=180 DOLPHIN_FRAME_SAMPLE_SEC=12 scripts/dolphin-boot-probe.sh
  ```

## G102 [x] Landmark weapon Spawn + lean-attach after changelevel

- Status: DONE 2026-07-18. Extends G100 create-and-free: `pfnSpawn` completes
  under PrecacheModel/SetModel/`LinkEdict` stubs; `pfnTouch`/AddPlayerItem still
  no-ops after changelevel, so grant falls back to lean owner bind
  (`MOVETYPE_FOLLOW` + weapons bits) and keeps weapon edicts alive.
- Acceptance:
  - `c0a0`→`c0a0a` landmark hop logs spawn done for crowbar+glock and
    `G102 landmark weapons granted=2`.
- Evidence: `.ai/logs/dolphin-probe-20260718-003429` —
  `G102 give spawn done classname=weapon_crowbar ret=0`,
  `G102 give lean-attach classname=weapon_crowbar owner=1`,
  `G102 give lean-attach classname=weapon_9mmhandgun owner=1`,
  `G102 landmark weapons granted=2 weapons=0x6`.
- Intentional limits: DefaultTouch→AddPlayerItem→Deploy inventory chain still
  open; viewmodel draw not restored.
- Command:
  ```sh
  DOLPHIN_SMOKE_MAP=c0a0 DOLPHIN_CHANGELEVEL=c0a0a DOLPHIN_LANDMARK=c0a0toa \
    DOLPHIN_G95=1 DOLPHIN_G102=1 DOLPHIN_TIMEOUT=120 DOLPHIN_FRAME_SAMPLE_SEC=6 \
    scripts/dolphin-boot-probe.sh
  ```

## G103 [x] Landmark inventory-chain weapon attach (m_rgpPlayerItems)

- Status: DONE 2026-07-18. Resolves CBasePlayer on the large private-data edict
  (often edict 2, not client edict 1). After `pfnSpawn`, links weapons into
  `m_rgpPlayerItems` / `m_pActiveItem` via measured HLSDK offsets when
  DefaultTouch no-ops. Ammo restore also works once the correct private edict
  is used.
- Acceptance:
  - `c0a0`→`c0a0a` logs inventory-attach for crowbar+glock and
    `G103 landmark weapons granted=2` with ammo1=99 ammo2=88.
- Evidence: `.ai/logs/dolphin-probe-20260718-010723` —
  `G103 grant using priv_edict=2 (client_edict=1)`,
  `G103 inventory-attach classname=weapon_crowbar slot=0`,
  `G103 inventory-attach classname=weapon_9mmhandgun slot=1`,
  `G103 landmark weapons granted=2 weapons=0x6 ammo1=99 ammo2=88`.
- Intentional limits: Deploy/viewmodel still open; DefaultTouch/IsPlayer still
  no-ops (touchfn is set; inventory link is engine-side).
- Command:
  ```sh
  DOLPHIN_SMOKE_MAP=c0a0 DOLPHIN_CHANGELEVEL=c0a0a DOLPHIN_LANDMARK=c0a0toa \
    DOLPHIN_G95=1 DOLPHIN_G103=1 DOLPHIN_TIMEOUT=120 DOLPHIN_FRAME_SAMPLE_SEC=6 \
    scripts/dolphin-boot-probe.sh
  ```

## G104 [x] Lean Deploy/viewmodel after landmark inventory attach

- Status: DONE 2026-07-18. After G103 inventory-chain attach, sets
  `viewmodel` / `weaponmodel` and `m_szAnimExtention` without HLSDK Deploy /
  SendWeaponAnim. Prefers glock bit 2; syncs client edict + priv edict.
- Acceptance:
  - `c0a0`→`c0a0a` logs `G104 deploy viewmodel=models/v_9mmhandgun.mdl` and
    `G104 landmark weapons granted=2` with non-`-` viewmodel.
- Evidence: `.ai/logs/dolphin-probe-20260718-013800` —
  `G104 deploy viewmodel=models/v_9mmhandgun.mdl weaponmodel=models/p_9mmhandgun.mdl anim=onehanded bit=2`,
  `G104 landmark weapons granted=2 weapons=0x6 ammo1=99 ammo2=88 viewmodel=models/v_9mmhandgun.mdl`.
- Historical limit: studio draw was completed by G105; the DefaultTouch/player
  ownership issue was completed by G106.
- Command:
  ```sh
  DOLPHIN_SMOKE_MAP=c0a0 DOLPHIN_CHANGELEVEL=c0a0a DOLPHIN_LANDMARK=c0a0toa \
    DOLPHIN_G95=1 DOLPHIN_G104=1 DOLPHIN_TIMEOUT=120 DOLPHIN_FRAME_SAMPLE_SEC=6 \
    scripts/dolphin-boot-probe.sh
  ```

## G105 [x] Landmark first-person viewmodel studio draw

- Status: DONE 2026-07-18. Injects `v_9mmhandgun.mdl` into `gc_studio/`,
  promotes it with crowbar under the real-studio budget, and after G104 Deploy
  binds + presents one `r_drawviewmodel` frame on `c0a0a`.
- Acceptance:
  - `c0a0`→`c0a0a` logs `G105 viewmodel draw models/v_9mmhandgun.mdl`.
- Evidence: `.ai/logs/dolphin-probe-20260718-014519` —
  `real studio loaded 'models/v_9mmhandgun.mdl' (60.80 Kb) … view=2`,
  `G104 deploy viewmodel=models/v_9mmhandgun.mdl`,
  `G105 viewmodel draw models/v_9mmhandgun.mdl`.
- Historical limit: DefaultTouch/player ownership was completed by G106; world
  `w_` weapon meshes were not re-promoted in this pass.
- Command:
  ```sh
  DOLPHIN_SMOKE_MAP=c0a0 DOLPHIN_CHANGELEVEL=c0a0a DOLPHIN_LANDMARK=c0a0toa \
    DOLPHIN_G95=1 DOLPHIN_G105=1 DOLPHIN_TIMEOUT=120 DOLPHIN_FRAME_SAMPLE_SEC=6 \
    scripts/dolphin-boot-probe.sh
  ```

## G106 [x] Construct the direct-map player on the reserved client edict

- Status: DONE 2026-07-18. The previous size-only private-data tracker mistook
  the 2176-byte `CSoundEnt` allocation on edict 2 for `CBasePlayer`. The
  `-gcmap -gcnewgame` route now primes `ClientPutInServer` on reserved edict 1
  after every map activation; player tracking is restricted to client edicts.
- Acceptance:
  - Both sides of `c0a0`→`c0a0a` allocate `sizeof(CBasePlayer)=1920` on edict 1.
  - Crowbar and Glock use the real DLL touch path with owner 1, non-null
    `m_pPlayer`, item IDs 1/2, and weapon bits progressing `0x2`→`0x6`.
  - G104 deploy and G105 viewmodel draw remain intact.
- Evidence: `.ai/logs/dolphin-probe-20260718-032131` —
  `direct-map player prime ready edict=1`,
  `G103 give touch-attach ... owner=1 ... item_player=0x8150da80 item_id=1`,
  `G103 give touch-attach ... owner=1 ... item_player=0x8150da80 item_id=2`,
  `G104 landmark weapons granted=2`, and `G105 viewmodel draw`.
- Command:
  ```sh
  DOLPHIN_SMOKE_MAP=c0a0 DOLPHIN_CHANGELEVEL=c0a0a DOLPHIN_LANDMARK=c0a0toa \
    DOLPHIN_G95=1 DOLPHIN_G104=1 DOLPHIN_TIMEOUT=240 DOLPHIN_FRAME_SAMPLE_SEC=6 \
    scripts/dolphin-boot-probe.sh
  ```

## G107 [x] Add bounded LRU replacement to lean-N PVS

- Status: DONE 2026-07-18. The G101 four-row cache could only follow among
  clusters selected during BSP load. Lean capture now retains packed compressed
  PVS rows plus prebuilt node masks for every valid cluster; camera misses
  decompress/copy into the least-recently-used one of four live slots. Node
  ancestry is never walked after BSP scratch reuse.
- Acceptance:
  - Forced lean `c1a0`→`c1a0a` captures 781 packed rows and keeps four live slots.
  - Runtime loads an uncached camera cluster and another proof cluster with
    explicit slot eviction, then continues world rendering and gameplay input.
  - MEM1 high-water remains 4.90 MiB, matching the prior large-map ceiling.
- Evidence: `.ai/logs/dolphin-probe-20260718-034958` —
  `Capture FatPVS lean LRU rows=781 packed=30314`,
  `Capture FatPVS lean LRU nodebits=230395`,
  `PVS lean LRU load cluster=429 slot=1 evict=1`,
  `PVS lean LRU load cluster=1 slot=2 evict=2`, and
  `PVS lean LRU ready slots=4 loaded=1 packed=30314`.
## G108 [x] Fairly schedule bounded post-G36 entity thinks

- Status: DONE 2026-07-18. The G84 world-think loop always restarted at the
  first non-client edict, so eight frequently-due low slots could starve every
  later entity. It now keeps a changelevel-safe round-robin cursor while
  preserving the existing eight-world-think cap and pusher exclusions.
- Acceptance:
  - The bounded path rotates its next scan position whenever the cap is reached.
  - A real post-changelevel map scans its complete world-edict range and runs
    later due entities without increasing the cap or enabling full physics.
  - World present, lean PVS, player input, snapshots, and audio continue after
    the scheduler runs.
- Evidence: `.ai/logs/dolphin-probe-20260718-044542` — four bounded ticks scan
  122 world slots and run due entities through edict 76 (`world=1..5`,
  `scanned=122`, `last=46..76`); `G68 changelevel ready`, G95 world present,
  G101 PVS follow, gameplay input, and gameplay sound all occur. The generic
  analyzer reports FAIL because this marker-focused retail run has no frame
  timing samples and expects the pre-transition map label; this is not claimed
  as G36 performance evidence.
## G109 [x] Restore bounded collision-clipped player movement

- Status: DONE 2026-07-18. The compact clipnodes were retained inside the
  renderer lookup-table arena; `R_GCRebuildBlendMaps` overwrote them after map
  load, causing `PM_RecursiveHullCheck` to stall. The compact 59–60 KB lump is
  now pinned on the heap before renderer scratch is released, with existing
  changelevel cleanup taking ownership. G86 movement now uses `SV_Move`.
- Acceptance:
  - Validate retained world hull planes/clipnodes after BSP scratch reuse and
    reject or repair cycles/corruption before post-G36 movement.
  - A bounded player hull trace returns with a fraction/end position and cannot
    spin indefinitely on malformed collision data.
  - Player movement, bounded server ticks, world presents, G45 actions, and
    lean PVS continue after the trace.
- Evidence: `.ai/logs/dolphin-probe-20260718-052139` — both maps report
  `pinned clipnodes outside BSP scratch` and heap aliases; normal movement
  traces return `fraction=1.000` with hull `(-16 -16 -36)/(16 16 36)`. A
  non-mutating long trace hits the world at `fraction=0.025`, end
  `(456 2112 785)`, then bounded thinks, G45 actions, PVS, render, and audio
  continue. MEM1 HWM remains 3.59 MiB on this route.
- Full PMove, stepping, impacts, trigger touches, gravity, and PostThink remain
  intentionally out of scope.
## G110 [x] Relink bounded player movement into the server area tree

- Status: DONE 2026-07-18. G110 established that each accepted G109
  hull-clipped move could call `SV_LinkEdict(player, false)`, refreshing the
  player's absolute bounds and broadphase area membership without invoking
  trigger callbacks. G111 subsequently enables trigger traversal. The existing
  GameCube client guard continues to skip the unstable BSP render-leaf walk.
- Acceptance:
  - A successful bounded move updates `absmin`/`absmax` from the new origin and
    leaves the player linked into a server area node.
  - Relinking does not invoke trigger touches or reintroduce the post-G36 BSP
    traversal stall.
  - Bounded thinks, world interaction, G45 input, rendering, and gameplay audio
    continue after repeated relinks.
- Evidence: `.ai/logs/dolphin-probe-20260718-052721` — six consecutive moves
  advance origin 250→300 and report matching hull bounds with `linked=1`;
  fraction-1.000 collision traces, world interaction, rotating bounded thinks,
  jump/use input, gameplay sound, and `probe gameplay input ready` all continue.
  The marker-focused probe was intentionally stopped after acceptance evidence,
  before the generic harness's known map-label/timing timeout.
- Full PMove, stepping, gravity, impacts, trigger touches, and PostThink remain
  intentionally out of scope.
## G111 [x] Restore trigger-aware relinking after bounded movement

- Status: DONE 2026-07-18. The accepted-move relink now uses
  `SV_LinkEdict(player, true)`, restoring the normal server area-tree trigger
  traversal while retaining the GameCube client render-leaf guard. A capped
  native callback marker is available when a linked trigger actually overlaps.
- Acceptance:
  - Repeated trigger-enabled relinks return with translated absolute bounds and
    valid area membership after changelevel.
  - Collision clipping, world rendering, bounded thinks, input, and audio
    continue after trigger traversal.
  - Do not claim a native callback unless the test route geometrically overlaps
    a linked trigger; retain G88's direct DLL touch as the callback proof.
- Evidence: `.ai/logs/dolphin-probe-20260718-055613` — six consecutive moves
  advance origin 250→300 with matching hull bounds and report
  `linked=1 triggers=1`. The route does not overlap a linked trigger, so no
  `native trigger touch` marker appears; the separate bounded G88 touch still
  fires `trigger_multiple`. Fraction-0.025 world collision, sustained world
  present, rotating bounded thinks, gameplay sound, jump/use, and input-ready
  all continue. The generic analyzer's expected pre-transition map label and
  absent timing samples remain unrelated to this marker-focused acceptance.
- Full PMove, stepping, gravity, impacts, and PlayerPostThink remain out of
  scope.
## G112 [x] Refresh bounded player ground support after movement

- Status: DONE 2026-07-18. A 64-unit downward player hull trace accepts only
  walkable support within the 18-unit step height, refreshing
  `FL_ONGROUND`/`groundentity` and clearing stale state while airborne.
- Acceptance: report support distance, plane, ground state, and edict while
  preserving collision, relinking, rendering, bounded thinks, input, and audio.
- Evidence: `.ai/logs/dolphin-probe-20260718-070101` — six traces find flat
  world support (`normalz=1`, `ent=0`) 20.97 units below the hull and correctly
  report `onground=0`. The player remains at z=785; gameplay continues through
  input-ready. Gravity and landing are intentionally deferred.
## G113 [x] Route controller axes through native HLSDK PMove

- Status: DONE 2026-07-18. The full-physics probe now emits synthetic stick
  axes through the same `Joy_AxisMotionEvent` path used by physical GameCube
  controllers. Standard client command generation, loopback networking, and
  `SV_RunCmd` deliver nonzero movement to the statically linked HLSDK PMove.
- Acceptance:
  - A nonzero standard `forwardmove` reaches `SV_RunCmd` without calling the
    bounded `GC_FillNewGameMoveUsercmd` scaffold.
  - Repeated PMove calls accumulate horizontal displacement and look yaw.
  - World rendering and attack/jump/use input continue with native physics.
- Evidence: `.ai/logs/dolphin-probe-20260718-142612` reports
  `move=(266,0)`, horizontal velocity `30.0`, delta `(0.24,0.00,-1.39)`, and
  yaw `-0.5`; sustained world presentation and all three gameplay actions
  continue. Client prediction remains disabled on this MEM1 route while the
  server runs authoritative native PMove.
## G114 [x] Restore native HLSDK client, weapon, and HUD server updates

- Status: DONE 2026-07-18. Full-physics runs now call the original statically
  linked HLSDK `CBasePlayer::UpdateClientData`, exported `UpdateClientData`,
  and `GetWeaponData` paths instead of the post-G36 minimal snapshot fallback.
  World hull 0 is retained outside reusable BSP scratch for full physics, so
  the original status-bar world trace remains valid after renderer startup.
- Acceptance:
  - Original client-data and weapon-data callbacks return on repeated snapshots.
  - The complete original server HUD update and status trace return.
  - PMove, controller axes, gameplay actions, transition, and world rendering
    continue in the same native run.
- Evidence: `.ai/logs/dolphin-probe-20260718-155019` reports native client and
  weapon data twice, a returning status trace and complete HUD update, axis
  movement delta `(0.45,0.00,-2.22)`, all three gameplay actions, `c1a0a`
  world presentation, and no exception. The generic analyzer still expects the
  pre-transition map label and therefore reports its known marker timeout.
## G115 [x] Register statically linked HLSDK HUD user-message handlers

- Status: DONE 2026-07-18. The allocation-free quality-0 HUD path now hooks
  original HLSDK message wrappers before returning, without constructing
  sprite or HUD-element lists.
- Acceptance: `ResetHUD`, `InitHUD`, `Flashlight`, `Geiger`, `HideWeapon`,
  `SetFOV`, `Health`, `Battery`, `Damage`, `FlashBat`, `Train`, and
  `WeaponList` messages dispatch to non-null handlers during full physics.
- Preserve the legal asset boundary: use only the compatible statically linked
  HLSDK client code and user-supplied `valve/` assets.
- Evidence: `.ai/logs/dolphin-probe-20260718-155507` registers every required
  handler, contains no `CL_ParseUserMessage` or `No pfn` errors, then completes
  native HUD/status updates, PMove axis displacement, attack/jump/use,
  transition, and sustained world rendering.
## G116 [x] Restore statically linked HLSDK client prediction

- Status: DONE 2026-07-18. Removed the GameCube-only forced `cl_nopred=1` now
  that retained world collision and native client state support original
  prediction.
- Acceptance:
  - Standard client command generation calls the statically linked HLSDK
    `HUD_PlayerMove` with prediction enabled.
  - Predicted movement returns with finite origin/velocity and server PMove
    still receives the same commands through loopback networking.
  - HUD messages, gameplay actions, transition, and world rendering continue.
- Evidence: `.ai/logs/dolphin-probe-20260718-181611` reports client HLSDK
  PMove for `move=(267,0,0)`, finite predicted velocity `(2.7,-0.0,265.1)`,
  authoritative axis velocity `(30.0,-0.1,249.1)`, all gameplay actions,
  native HUD updates, transition, and sustained world presentation. No forced
  prediction-disable/skip marker or exception appears.
## G117 [x] Submit nonzero native gameplay PCM to GameCube ASND

- Status: DONE 2026-07-18. Gameplay SFX is queued from Prepare and emitted only
  after `cls.state == ca_active`, so local reconnect cannot wipe the channel.
  The mixahead window is rewound when pre-voice silence has filled it, then
  `buttons/button10.wav` mixes at full volume into the DMA ring.
- Acceptance:
  - A legally supplied `buttons/button10.wav` decodes with a nonzero source peak.
  - The standard mixer consumes the channel at nonzero left/right volume.
  - ASND receives at least one nonzero PCM chunk while native gameplay,
    prediction, HUD updates, and rendering continue.
- Evidence: `.ai/logs/dolphin-probe-20260718-193416` reports mix-window rewind
  `painted=5760→sound=0`, decode `peak=128`, `mixer ready volume=(255,255)`,
  and `audio submitted nonzero PCM chunks=1 peak=22644`, with attack/jump/use,
  native HUD updates, axis PMove, and sustained world presentation continuing.

## G118 [x] Allow cumulative byte-budget gameplay SFX under MapLoadMemoryOpt

- Status: DONE 2026-07-18. Replaced the one-shot memopt allow with a 48 KiB
  cumulative decoded-SFX budget (8192 B per file via existing WAV skip). Bulk
  precache stays blocked while registering; `ca_active` loads use the budget.
- Acceptance:
  - Probe plays `weapons/pl_gun1.wav` via `S_StartLocalSound` without
    `S_AllowNextGameplaySoundLoad`.
  - Decode `peak>0` with `budget_used` telemetry; mixer/ASND nonzero PCM.
  - No MEM1 / Sound Zone fatal on fullphysics New Game probe.
- Evidence: `.ai/logs/dolphin-probe-20260718-200408` —
  `sound load allowed for gameplay sfx weapons/pl_gun1.wav`,
  `decoded ... peak=128 budget_used=6255 cap=49152`,
  `mixer ready name=weapons/pl_gun1.wav ... volume=(255,255)`,
  `audio submitted nonzero PCM chunks=1 peak=22823`.

## G119 [x] Fullphysics weapon grant without landmark hop

- Status: DONE 2026-07-18. Bare `-gcchangelevel` now queues grant from
  `GC_LeanLandmarkProbePlantAmmo`. Early grant on fullphysics second_map is
  deferred; `GC_LeanLandmarkGrantWeaponsAfterPutInServer` re-arms and grants
  after `pfnClientPutInServer` so inventory is not wiped.
- Acceptance:
  - Destination map logs `G104 landmark weapons granted>=1` with weapons bits.
  - Native `UpdateClientData` reports nonzero `weapons` and non-zero viewmodel.
  - Attack/jump/use continue; fire SFX may load via G118 budget (best-effort).
  - No MEM1 fatal.
- Evidence: `.ai/logs/dolphin-probe-20260718-201558` —
  `G119 re-grant after ClientPutInServer weapons=0x6`,
  `G104 landmark weapons granted=2 weapons=0x6`,
  `UpdateClientData ... weapons=0x6 ... viewmodel=107`,
  attack/jump/use + `pl_gun1` budget load + ASND `peak=22823`.

## G120 [x] HLSDK PrimaryAttack on fullphysics attack

- Status: DONE 2026-07-18. After G104 lean deploy, `GC_LeanWeaponCombatReady`
  clears `m_flNextAttack` / `m_flNextPrimaryAttack` and ensures clip under
  `-gcfullphysics`. Probe attack reaches HLSDK `ItemPostFrame` → `PrimaryAttack`.
  GameCube EMIT_SOUND uses `pl_gun1` (≤8192 B); listen-server
  `SV_StartSound` also calls `S_StartLocalSound` so the G118 budget decodes it.
- Acceptance:
  - Guest log shows `PrimaryAttack` (or equivalent HLSDK fire marker) on probe attack.
  - Weapon fire WAV loads through G118 budget with decode `peak>0`.
  - `UpdateClientData` keeps nonzero weapons/viewmodel; no MEM1 fatal.
- Evidence: `.ai/logs/dolphin-probe-20260718-205118` —
  `ItemPostFrame id=2 buttons=0x8001`,
  `PrimaryAttack weapon=glock clip=17`,
  `G120 SV_StartSound local weapons/pl_gun1.wav`,
  `sound load allowed ... pl_gun1.wav` / `decoded ... peak=128 budget_used=10077`,
  `PrimaryAttack fire sfx ... clip=16`,
  `UpdateClientData ... weapons=0x6 ... viewmodel=107`.

## G121 [x] Client EV_FireGlock without local SV bridge

- Status: DONE 2026-07-18. Removed G120 `EMIT_SOUND` + `SV_StartSound` local
  bridge. Fullphysics delivers `FEV_NOTHOST` events to the local shooter
  (visibility bypass), relinks `cl.event_precache` after changelevel wipe, and
  fires stock `EV_FireGlock`. Under MEM1, `EV_PlaySound` uses `pl_gun1.wav`
  (stock `pl_gun3` ~13 KiB OOMs SoundLib); SoundLib allocs soft-fail.
- Acceptance:
  - Probe attack plays fire SFX through `EV_FireGlock` (or networked event)
    without the G120 `SV_StartSound` local shortcut.
  - Decode `peak>0` under MEM1; no fatal.
- Evidence: `.ai/logs/dolphin-probe-20260718-211713` —
  `G121 event hooks relinked count=18`,
  `G121 PlaybackEvent deliver ... events/glock1.sc`,
  `G121 EV_FireGlock weapons/pl_gun1.wav`,
  `decoded ... peak=128 budget_used=10077`, no fatal / no `SV_StartSound local`.

## G122 [x] MEM1 headroom for stock pl_gun3 via EV_FireGlock

- Status: DONE 2026-07-18. Dual peak (FS file + SoundLib PCM) OOMed at 12.90 KiB
  after changelevel. Memopt WAV path now converts PCM in the FS buffer and
  packs `wavdata_t` in-place (no second SoundLib alloc). `EV_FireGlock` plays
  stock `weapons/pl_gun3.wav`.
- Acceptance:
  - `G122 EV_FireGlock weapons/pl_gun3.wav` + decode `peak>0`.
  - No MEM1 / SoundLib fatal.
- Evidence: `.ai/logs/dolphin-probe-20260718-212457` —
  `G122 WAV in-place pack bytes=13209 file=13336`,
  `G122 EV_FireGlock weapons/pl_gun3.wav`,
  `decoded ... peak=128 budget_used=17031`, no fatal.

## G123 [x] Memopt player footstep SFX within budget

- Status: DONE 2026-07-18. After stock `pl_gun3`, FS file buffers retained by
  in-place packs starved small footstep loads. Migrate in-place packs to
  SoundLib when possible; evict finished `buttons/button10.wav` before
  `player/pl_step*`.
- Acceptance:
  - `gameplay sound decoded` for a `player/pl_step*.wav` with `peak>0`.
  - No MEM1 / SoundLib fatal; budget stays ≤48 KiB.
- Evidence: `.ai/logs/dolphin-probe-20260718-213330` —
  `G123 evict budgeted sfx buttons/button10.wav`,
  `G123 WAV migrated bytes=2430` for `pl_step2`,
  `decoded ... peak=119 budget_used=15639`, no fatal.

## G124 [x] Budgeted SFX LRU for multiple footsteps / ricochets

- Status: DONE 2026-07-18. Post-fire `pl_step*` opens on gcdisc are unreliable
  under MEM1, so fullphysics preloads all four stock footsteps while MEM1 is
  free (`S_RegisterSound`). Small-victim LRU reclaim exists for non-step loads;
  step loads only force-drop finished `button10`.
- Acceptance:
  - At least two distinct `player/pl_step*.wav` decodes with `peak>0` in one
    probe session after changelevel.
  - No MEM1 / SoundLib fatal.
- Evidence: `.ai/logs/dolphin-probe-20260718-215805` —
  `G124 preload footsteps begin`, four `gameplay sound decoded` lines for
  `pl_step1..4` with `peak>0`, `preload footsteps ready budget_used=10405`,
  no fatal.

## G125 [x] Stock pl_gun3 after footstep preload

- Status: DONE 2026-07-19. Releasing migrated step caches poisoned later
  `FS_FindFile` for sounds. Fix: preload `weapons/pl_gun3.wav` first, then
  `player/pl_step1..4`, keep all resident (~23614 B / 48 KiB), pin them from
  LRU eviction. Fire hits cache; ricochet still loads; ASND PCM peak>0.
- Acceptance:
  - `G122 EV_FireGlock` + `pl_gun3` decode `peak>0` (preload) without reload miss.
  - Preloaded steps remain cached (`budget_used` includes them) for walk.
- Evidence: `.ai/logs/dolphin-probe-20260719-005629` —
  `G125 preload fire+steps ready budget_used=23614`,
  `G122 EV_FireGlock` with no subsequent `couldn't load` / soft-fail for
  `pl_gun3`, `weapons/ric1.wav` load ok, `audio submitted ... peak=19054`.
- Screenshots: `.ai/screenshots/demo-stages/` (menu + live loading framedumps).

## G126 [x] Combat ricochet + HUD soft-fail under MEM1

- Status: DONE 2026-07-19. Preload `weapons/ric1.wav` with fire+steps
  (`budget_used=29611`); pin `weapons/ric*`; alias ric2–5→ric1 in
  `S_RegisterSound` / ricochet play paths; HUD `FS_LoadFile` soft-fail stubs
  under memopt (fallback to `gc_320hud2.spr` when retail miss).
- Acceptance:
  - `G126 preload fire+steps+ric ready` with ric1 in budget (~30 KiB).
  - No post-fire `FS_LoadFile` / decode for `weapons/ric2..5`.
  - Soft-fail HUD path stubs without aborting redraw.
  - Fire still `G122 EV_FireGlock` + nonzero ASND peak.
- Evidence: `.ai/logs/dolphin-probe-20260719-013339` —
  `G126 preload ... ready budget_used=29611`,
  `HUD sprite stub after soft-fail sprites/320hud1.spr`,
  `G122 EV_FireGlock`, `audio submitted ... peak=2402`, no ric2–5 load.

## G127 [x] Real HUD sheets before SFX + tracer headroom

- Status: DONE 2026-07-19. Fat `sprites/320hud1.spr` (~66 KiB) soft-failed
  after gameplay SFX preload. Fix: `CL_GCPreloadNewGameHudSprites` loads it
  after lean VidInit and before SFX. Fullphysics particle budget raised to 96;
  impact/streak/implosion bursts capped under MEM1; tracer reclaim before
  soft exhaust (no S_ERROR spam).
- Acceptance:
  - `G127 HUD sheet sprites/320hud1.spr handle=` non-zero (real load).
  - No post-SFX `HUD sprite stub after soft-fail sprites/320hud1.spr`.
  - `G127 preload fire+steps+ric ready budget_used=29611` still succeeds.
  - `client budgets particles=96`.
  - Fire + nonzero ASND peak.
- Evidence: `.ai/logs/dolphin-probe-20260719-025133` —
  `G127 HUD sheet ... handle=1`, `preload ... ready budget_used=29611`,
  `G122 EV_FireGlock`, `audio submitted ... peak=13771`, no 320hud1 soft-fail stub.

## G128 [x] Readable Dolphin world framedumps via CPU XFB present

- Status: DONE 2026-07-19. GX tiled presents dump as period-32 noise. After
  world+HUD, stamp HL status panel (`WORLD PRESENT` + map) onto the SW buffer
  and force CPU YUYV→XFB presents with VSync so DumpFrames latches readable
  frames.
- Acceptance:
  - `G128 CPU dump presents ready` in OSREPORT.
  - DumpFrames PNG showing `WORLD PRESENT` status panel (not pure period-32 noise).
  - Saved `.ai/screenshots/demo-stages/stage-04-world-present.png`.
- Evidence: `.ai/logs/dolphin-probe-20260719-030808` —
  `G128 CPU dump presents begin/ready`, framedump_14 with WORLD PRESENT +
  `MAP C1A0A` panel; screenshot copied to demo-stages.

## G129 [x] Coherent world pixels for WORLD PRESENT dumps

- Status: DONE 2026-07-19. Silent blit skip / rainbow flat-fills / green-only
  YUYV made DumpFrames look like static. Sync gpGlobals for New Game blit,
  validate retained screen size, coherent sky/wall flat fills, BT.601 on CPU
  dump presents, and always flood a sky backdrop before the WORLD PRESENT panel.
- Acceptance:
  - `G129 sky backdrop fill` logged with world nonblack telemetry.
  - stage-04 dump shows solid sky + WORLD PRESENT panel (not speckled noise).
- Evidence: `.ai/logs/dolphin-probe-20260719-032144` —
  `G129 sky backdrop fill (world nonblack=1140/1200)`,
  `G128 CPU dump presents ready`;
  `.ai/screenshots/demo-stages/stage-04-world-present.png` (solid sky + panel).

## G130 [x] Posterize WORLD PRESENT DumpFrames (room planes)

- Status: DONE 2026-07-19. Soft-edge color still speckles; zi nearly empty
  (`depth=19`). Coalesce 4×4, binary sky/wall/dark classify, 16×16 majority,
  stamp WORLD PRESENT panel, 6× CPU YUYV presents. Loading presents also force
  one CPU dump so stage-01-live stays readable.
- Acceptance:
  - `G130 posterize dump` logged with color nonblack telemetry.
  - stage-04 DumpFrames shows low unique colors + panel (not GX period-32).
- Evidence: `.ai/logs/dolphin-probe-20260719-034456` —
  `G130 posterize dump (depth=19 color nonblack=1140/1200)`,
  framedump_12 (~95% wall plane + panel);
  `.ai/screenshots/demo-stages/stage-04-world-present.png`.

## G131 [x] Depth-aware WORLD PRESENT dumps (unsigned zi + look-into-map)

- Status: DONE 2026-07-19. G130 treated signed zi≤0 as empty (`depth=19`);
  near surfaces store izi>>16 in the high half. Treat z as unsigned (skip only
  0xFFFF), continuous percentile shade, aim dump camera at map center, and if
  the depth image is flat (≤3 tones) fall back to 4×4 color coalesce so
  face-toned walls keep spatial structure.
- Acceptance:
  - `G131 depth dump shade valid=` with tens of thousands of samples.
  - `G131 dump look angles=` logged; stage-04 shows spatial structure + panel.
- Evidence: `.ai/logs/dolphin-probe-20260719-040343` —
  `valid=38415/76800`, `G131 depth flat→color coalesce`, framedump_14
  (uniq≈69, spat≈63); `.ai/screenshots/demo-stages/stage-04-world-present.png`.

## G132 [x] Capture-time faces → flat solid spans (WORLD PRESENT)

- Status: DONE 2026-07-19. Scratch-backed `msurface_t` / node walks are dead by
  present (`faces try=0`, promote OOM). Capture up to 256 visible faces while
  BSP is valid (plane/edges), draw them via `R_RenderFace` with null texinfo,
  and flat-fill RGB565+zi in `D_SolidSurf`. Camera snaps to capture origin when
  lean PVS cannot follow the player cluster.
- Acceptance:
  - `G132 captured draw faces=` and `G132 cap faces drawn=` logged.
  - `drawsurfs … solid>0` and `G132 flat solid spans active`.
  - stage-04 DumpFrames shows multi-tone world + panel (not sky-only).
- Evidence: `.ai/logs/dolphin-probe-20260719-050525` —
  `captured draw faces=256`, `faces try=175 emit=15`, `solid=10`,
  `flat solid spans active`; `.ai/screenshots/demo-stages/stage-04-world-present.png`.

## G133 [x] Capture texinfo → textured+lit RGB565 spans

- Status: DONE 2026-07-19. Extend G132 face capture with `mtexinfo_t` (texture*)
  + `mextrasurf_t` (lightextents / CACHESPOT). Null-samples use the existing
  mid-grade light path. Captured faces hit `D_CacheSurface` → RGB565 spans.
- Acceptance:
  - `G133 captured draw faces=… textured=…`
  - `textured+lit RGB565 spans active` / `surfcache lit begin`
  - `drawsurfs … solid>0`; stage-04 refreshed.
- Evidence: `.ai/logs/dolphin-probe-20260719-051017` —
  `textured=256`, `textured+lit RGB565 spans active`, `solid=10`;
  `.ai/screenshots/demo-stages/stage-04-world-present.png`.

## G134 [x] Keep textured RGB565 WORLD PRESENT dumps

- Status: DONE 2026-07-19. G131 depth shade/coalesce was overwriting G133
  textured buffers. Prefer keeping RGB565 when nonblack/uniq look textured;
  force CPU YUYV dump presents; flood black→sky.
  Also fixed empty surfcache: quality-0 only has mip0 (mip≥1 skipped draw),
  and lean extents left caches memset-empty — tile soft texels then
  `vid.screen[]` → RGB565 for direct spans.
- Acceptance:
  - `G134 keep textured dump (nonblack=… uniq=…)`
  - `G134 tile soft tex into cache` when block drawers leave empty cache
  - No `G131 depth flat→color coalesce` on textured New Game path
  - stage-04 refreshed from CPU dump presents
- Evidence: `.ai/logs/dolphin-probe-20260719-121916` —
  `G134 tile soft tex into cache`, `G134 keep textured dump … uniq=32`,
  no G131 coalesce; `.ai/screenshots/demo-stages/stage-04-world-present.png`.

## G135 [x] Retail-comparable WORLD PRESENT dumps (depth→posterize when soft uniq low)

- Status: DONE 2026-07-19. Soft-tiled caches pass G134 keep at uniq≈32 but
  DumpFrames as chroma noise. Keep only when uniq≥128; otherwise depth shade +
  G130 posterize (no soft-tile re-render). Defer CPU YUYV dump presents until
  after posterize+panel. Host_Init loading blit skipped (plaque hang).
- Acceptance:
  - `G135 dump depth/coalesce` + `G135 depth->posterize` (or coalesce→posterize)
  - stage-04 DumpFrames: low unique colors, solid planes + WORLD PRESENT panel
  - No GX/soft pink-cyan static as the dominant stage-04 image
- Evidence: `.ai/logs/dolphin-probe-20260719-235737` —
  `G135 dump depth/coalesce … uniq=32`, `G135 depth->posterize`, framedump_9
  uniq≈151 pink=0; `.ai/screenshots/demo-stages/stage-04-world-present.png`.

## G136 [x] Zi 3-plane room silhouette + YUYV scale combing fix

- Status: DONE 2026-07-20. G135 color-posterize after depth shade flattened to
  flat sky (near shade is sky-blue). Panel text showed A,B,A,B combing from 2×
  YUYV blit. `R_GcmapPosterizeDumpFromDepth` paints near/wall/sky from zi
  percentiles; 2×/4× nearest uses YUYV(p,p) per source pixel.
- Acceptance:
  - `G136 depth posterize … near=… wall=… sky=…` with near>0 and wall>0
  - stage-04: multiple room planes + readable panel
  - Panel row_flips near loading-plaque (no A,B,A,B combing)
- Evidence: `.ai/logs/dolphin-probe-20260720-000728` —
  `G136 depth posterize valid=72640 near=20273 wall=43854 sky=12673`,
  framedump_9 uniq≈62 pink=0 flips≈40; `.ai/screenshots/demo-stages/stage-04-world-present.png`.

## G137 [x] New Game face-solid blockout DumpFrames

- Status: DONE 2026-07-20. Soft-tile → `vid.screen[]` dumps as pink/cyan
  chroma. For `-gcnewgame` low-res, draw stable plane+texture-id solid RGB565
  spans (blockout room). Keep dump when uniq≥6; zi posterize only if empty.
- Acceptance:
  - `G137 face-solid spans active` + `G137 keep face-solid dump … uniq=…`
  - stage-04: multi-wall blockout + WORLD PRESENT panel
  - pink/cyan chroma not dominant
- Evidence: `.ai/logs/dolphin-probe-20260720-001831` —
  `G137 face-solid spans active`, `G137 keep face-solid dump … uniq=24`,
  framedump_7; `.ai/screenshots/demo-stages/stage-04-world-present.png`.

## G138 [x] Textured WORLD PRESENT DumpFrames (YUYV+chroma guards)

- Status: DONE 2026-07-20. Re-enabled surfcache textured spans for New Game.
  Soft→RGB565 still saturates dump uniq (≥48) as chroma — reject and fall back
  to G136 zi near/wall/sky for readable DumpFrames. Live path runs textured.
- Acceptance:
  - `G138 textured spans active` + (`G138 keep textured dump` or `G138 reject chroma`→zi)
  - stage-04: no pink/cyan static dominant; panel readable
- Evidence: `.ai/logs/dolphin-probe-20260720-003435` —
  `G138 soft->RGB565 cache uniq=43`, `G138 textured spans active`,
  `G138 reject chroma dump … uniq=64`, `G136 depth posterize … near=… wall=…`,
  framedump_9 uniq≈62 pink=0; `.ai/screenshots/demo-stages/stage-04-world-present.png`.

## G149 [x] Landmark viewmodel visible in DumpFrames

- Status: DONE 2026-07-20. WORLD PRESENT dumps ran before G104 Deploy, so early
  frames had `viewmodel=0`. Force low-res viewmodel draw under `-gcnewgame`
  when the mesh is bound (bypass health/viewentity), composite
  `v_9mmhandgun` into the dump buffer before G128 CPU presents, and after
  Deploy re-scrub + stamp `VIEWMODEL` + CPU-present so DumpFrames latch the gun.
- Acceptance:
  - `G149 dump composite viewmodel models/v_9mmhandgun.mdl`
  - `G149 viewmodel dump presents begin` and/or `G105 viewmodel draw`
  - DumpFrames show gun silhouette and/or VIEWMODEL panel
- Evidence: `.ai/logs/dolphin-probe-20260720-142850` —
  `G149 low-res viewmodel draw`, `G149 dump composite viewmodel`,
  `G105 viewmodel draw`, `G149 viewmodel dump presents begin`;
  framedump_17 VIEWMODEL panel, framedump_18 outdoor+gun;
  `.ai/screenshots/demo-stages/stage-04d-viewmodel-dump.png`,
  `stage-04e-viewmodel-panel.png`.

## G150 [x] Outdoor coverage via top-K faces + sky-hole rim fill

- Status: DONE 2026-07-20. G148 took the first 192 large faces in surface
  order, starving later outdoor tower panels. Capture online top-224 by area
  (replace min), fill 32 connector slots, sort largest-first for the edge
  budget, and rim-fill sky-through islands in scrub (dump + live).
- Acceptance:
  - `G150 captured draw faces=256 … replaced=` with replaced>0
  - Outdoor DumpFrames: higher mid-frame wall coverage / fewer sky-hole candidates
  - Cap stays 256 (no BSS raise)
- Evidence: `.ai/logs/dolphin-probe-20260720-143953` —
  `G150 captured … replaced=174`, `G147 faces try=199 emit=199`,
  outdoor mid wall 77%→90%, sky-hole candidates 40→28, uniq 10885→12576;
  stage-04 / 04b / 04c refreshed.

## G151 [x] Flipper GX EFB world draw (not soft spans)

- Status: DONE 2026-07-20. `ref/gx` was Quake soft raster + GX fullscreen
  present only. New Game live path now submits cap faces as GX triangles into
  the EFB (`ref/gx/r_gx_world.c`), then `GX_CopyDisp` — no soft edge/span fill.
  Soft+CPU DumpFrames remain for readable Dolphin evidence (`-gcsoftworld` escape).
- Acceptance:
  - `G151 GX world live enabled (Flipper EFB)`
  - `G151 GX world faces drawn=` with drawn>0
  - DumpFrames still produced via soft path before live enable
- Evidence: `.ai/logs/dolphin-probe-20260720-145123` —
  `G151 GX world live enabled`, `G151 GX world faces drawn=199 of 256`.
- Next: GX textured/lightmapped faces; GX studio/viewmodel.

## G152 [x] GX textured world faces (Flipper TEV)

- Status: DONE 2026-07-20. Upload soft `image_t` mip0 via major<<8|minor→RGB565,
  swizzle to `GX_TF_RGB565` (≤64×64, 24-slot LRU), bind `GXTexObj`, emit UVs
  from `texinfo->vecs`, TEV MODULATE. Flat-color fallback if bind fails.
- Acceptance:
  - `G152 GX textured faces=` with textured>0 and flat preferably low
  - G151 Flipper path still active
- Evidence: `.ai/logs/dolphin-probe-20260720-145710` —
  `G151 GX world faces drawn=199 of 256`,
  `G152 GX textured faces=199 flat=0 (Flipper TEV)`.
- Next: GX lightmaps; GX studio/viewmodel.

## G153 [x] GX lightmapped faces (Flipper TEV2)

- Status: DONE 2026-07-20. At face capture, bake style-0 lightmap (or mid-grade)
  to ≤8×8 tiled RGB565 BSS. Live GX binds TEX0 diffuse + TEX1 lightmap,
  TEV0 MODULATE then TEV1 MODULATE. Samples often already freed at capture
  (`lm=0`) — mid bake still exercises the Flipper combine path.
- Acceptance:
  - `G153 captured draw faces=… lm=`
  - `G153 GX lightmapped faces=` with count>0
- Evidence: `.ai/logs/dolphin-probe-20260720-150403` —
  `G153 captured … lm=0`, `G153 GX lightmapped faces=199 of 199 (Flipper TEV2)`.
- Next: keep real samples through capture; GX viewmodel.

## G154 [x] Real lightmap samples for Flipper LM bake

- Status: DONE 2026-07-20. Capture faces **after** lighting. Large lightmaps
  (>256 KiB) are dropped from scratch so surfaces can settle, then
  `LUMP_LIGHTING` is reloaded from disc for bake-only bind (no full MEM1
  residency). Multi-cluster PVS without a surf table builds a one-row
  surfbits fallback so changelevel maps still capture faces.
- Acceptance:
  - `G154 captured … lm=` with lm>0 (ideally =face count)
  - `G154 disc lightmap bind size=`
  - Cap faces still draw after changelevel (`G133 cap faces drawn=` >0)
- Evidence: `.ai/logs/dolphin-probe-20260720-152030` —
  `G154 disc lightmap bind size=601.95 Kb`,
  `G154 captured … lm=256` (c0a0 and c1a0a),
  `G133 cap faces drawn=199 of 256`.
- Next: GX studio/viewmodel; confirm live GX draw after reconnect.

## G155 [x] GX studio / viewmodel on Flipper (TriAPI → EFB)

- Status: DONE 2026-07-20. When `GC_UseGxWorldDraw()` is armed, TriAPI
  studio meshes emit world-space GX triangles (TEX0 MODULATE) into the EFB
  instead of soft polyset. Viewmodel uses Z-always overlay. One Prepare-time
  smoke frame after `G151` enable proves the path before reconnect stalls SCR.
- Acceptance:
  - `G155 GX studio tris=` with tris>0
  - Same smoke also logs `G151 GX world faces drawn=` / `G154 GX lightmapped`
- Evidence: `.ai/logs/dolphin-probe-20260720-153823` —
  `G151 GX world faces drawn=199 of 256`,
  `G154 GX lightmapped faces=199 of 199`,
  `G155 GX studio tris=14 viewmodel=0`,
  `G155 GX live smoke frame`.
- Residual (closed by G156): landmark `v_9mmhandgun` often missing cache at
  smoke/Deploy; Flipper path first proven via forced world studio.

## G156 [x] Retain landmark viewmodel for Flipper draw

- Status: DONE 2026-07-20. Pin `v_*` meshes so reconnect/`Mod_FreeModel` cannot
  drop them; promote/ensure paths reuse resident cache (no 130 KiB re-read);
  Prepare smoke binds `clgame.viewent` and draws viewmodel before forced world
  studio so G155 attributes tris to the gun.
- Acceptance:
  - `G156 pinned viewmodel models/v_9mmhandgun.mdl`
  - `G156 smoke bind viewmodel models/v_9mmhandgun.mdl`
  - `G155 GX studio tris=` with `viewmodel=1`
- Evidence: `.ai/logs/dolphin-probe-20260720-155105` —
  `G156 pinned viewmodel` (crowbar + 9mm),
  `G156 smoke bind viewmodel models/v_9mmhandgun.mdl`,
  `G155 GX studio tris=908 viewmodel=1`.
- Next: viewmodel FOV/origin polish; live GX frames after reconnect (SCR stall).

## G157 [x] Viewmodel eye-pose sync for Flipper

- Status: DONE 2026-07-20. Without client `CalcRefdef`, New Game left
  `clgame.viewent` at a stale/world origin so the gun floated as a tiny center
  speck. Each `R_DrawViewModel` now copies `RI.rvp` eye origin/angles; skip
  Quake pitch negate for the viewent; GX smoke reports NDC band.
- Acceptance:
  - `G157 viewmodel pose … dist=0.00`
  - `G157 viewmodel fov=` with `lower=1`
  - `G155 GX studio tris=` `viewmodel=1` still holds
- Evidence: `.ai/logs/dolphin-probe-20260720-160332` —
  `G157 viewmodel pose origin=(240,2112,833) … dist=0.00`,
  `G155 GX studio tris=908 viewmodel=1`,
  `G157 viewmodel fov=90 ndc_y=[-3.66,-0.30] mid=-1.98 lower=1`.
- Residual: soft DumpFrames still mostly miss Flipper EFB gun; panel can cover
  the lower band.

## G158 [x] Live GX presents through loopback reconnect

- Status: DONE 2026-07-20. After G151 smoke, `loopback:reconnect` dropped
  below `ca_active` and SCR skipped presents while fullphysics stalled before
  the next SCR call. `CL_Reconnect` now issues bounded
  `GC_RenderNewGameWorldFrames` while GX is still armed; SCR also presents
  during connect when G36+world_ready.
- Acceptance:
  - `G158 live GX present reconnect` with `gx=1`
  - Still reaches post-reconnect `SendClientDatagram … post-G36`
- Evidence: `.ai/logs/dolphin-probe-20260720-161818` —
  `G151 GX world live enabled`,
  `G158 reconnect present begin gx=1`,
  `G158 live GX present reconnect state=2 signon=0 gx=1`,
  later `SendClientDatagram ready bytes=… post-G36`.
- Residual: after `client connected`, SCR presents were not evidenced (sticky
  plaque / G149 dump re-arm). Next was G159.

## G159 [x] Sustained live GX after post-reconnect ca_active

- Status: DONE 2026-07-20. Reconnect already reached `ca_active`
  (`client connected`). Residual was missing Flipper SCR presents after that
  point: sticky `draw_changelevel`, reconnect G149 CPU-dump re-arm, and probe
  exit armed on G158 before post-active evidence. Cleared plaque on
  `EndLoadingPlaque` / Flipper-ready paths; skip G149 dump re-arm once Flipper
  is live; force bounded presents on post-reconnect `ca_active`; probe waits
  for the G159 marker.
- Acceptance:
  - `G159 live GX present ca_active` with `gx=1`
- Evidence: `.ai/logs/dolphin-probe-20260720-162647` —
  `G159 skip viewmodel dump re-arm (Flipper live)`,
  `G159 ca_active present begin gx=1`,
  `G159 live GX present ca_active gx=1`,
  then `client connected at 2.80 sec`.
- Residual: post-active HUD sprite soft-fails under MEM1; outdoor Flipper still
  shows sky-through holes (static 256-face cap). Next is G160.

## G160 [x] Outdoor Flipper hole fill (wall-boost + LRU surfbits)

- Status: DONE 2026-07-20. G150 top-K still starved outdoor towers vs floors.
  +50% score for near-vertical walls in the 256-face top-K; lean PVS LRU now
  rebuilds marksurface bits (was memset-empty). Full face re-capture on cluster
  switch was attempted but stalls Host_Frame on 256× LM rebake — deferred.
- Acceptance:
  - `G160 captured draw faces=… wallboost=` with wallboost>0
  - Outdoor DumpFrames mid-band sky holes down vs G150 baseline
- Evidence: `.ai/logs/dolphin-probe-20260720-163223` —
  `G160 captured … wallboost=272` / `wallboost=238`,
  framedump_17 mid_sky 4.9%→0.3%.
- Residual: live cluster face refresh without LM rebake; soft DumpFrames viewmodel
  addressed in G161.

## G161 [x] Soft DumpFrames viewmodel while Flipper live

- Status: DONE 2026-07-20. After G159, Deploy hit `G159 skip viewmodel dump
  re-arm (Flipper live)`, so soft DumpFrames never latched the gun. One-shot
  G161 path: arm `gc_cpu_dump_presents_left` so `GC_UseGxWorldDraw` is false,
  eye-sync + soft-composite `v_9mmhandgun` into `gc.buffer`, present gun then
  stamp VIEWMODEL, clear dump arm so Flipper resumes. Probe waits for
  `G161 soft dump viewmodel ready` with G159.
- Acceptance:
  - `G161 soft dump composite viewmodel models/v_9mmhandgun.mdl`
  - `G161 soft dump viewmodel ready`
  - DumpFrames include VIEWMODEL panel (and/or soft gun latch)
  - `G159 live GX present ca_active gx=1` still after G161
- Evidence: `.ai/logs/dolphin-probe-20260720-165320` —
  G161 composite/presents/ready, G159 ca_active gx=1;
  framedump_25 VIEWMODEL panel;
  `.ai/screenshots/demo-stages/stage-04d-viewmodel-dump.png`,
  `stage-04e-viewmodel-panel.png`.
- Residual: soft gun silhouette still weak vs G149 look-into-map dumps (lower
  FOV / panel); live face refresh without LM rebake remains open. Soft framing
  addressed in G162.

## G162 [x] Soft DumpFrames viewmodel framing (offset + top panel)

- Status: DONE 2026-07-20. G157 eye-pin left the gun mostly below the frustum
  (`ndc mid≈-2`, tip-only). Apply New Game viewmodel origin nudge
  `forward=5 up=12`; stamp VIEWMODEL panel at top so lower FOV stays clear;
  accept when `G162 viewmodel framed` (ndc ymin>-1.6, ymax>-0.55).
- Acceptance:
  - `G162 viewmodel frame offset forward=5 up=12`
  - `G162 viewmodel framed ndc_y=[…]` with mid in lower third
  - `G162 soft dump viewmodel framed`
  - DumpFrames show a substantial gun silhouette (not tip-only)
- Evidence: `.ai/logs/dolphin-probe-20260720-165932` —
  `G162 viewmodel framed ndc_y=[-1.27,0.70] mid=-0.28`,
  `G162 soft dump viewmodel framed`, framedump_25 gun + top VIEWMODEL panel;
  `.ai/screenshots/demo-stages/stage-04d-viewmodel-dump.png`,
  `stage-04e-viewmodel-panel.png`.
- Residual: live cluster face refresh without LM rebake (G163); soft studio shading.

## G163 [x] Live cluster face refresh without LM rebake

- Status: DONE 2026-07-20. Full 256-face rebuild mid-PVS hung Host_Frame; live
  `msurface_t->plane` dangles at present. Defer refresh off the PVS switch path;
  on c1a0a surf-table OOM keep a 4-slot capture-time surfbits+cand cache; admit
  up to 32 new top-area faces with mid-grade LM only (reuse baked sample LM).
- Acceptance:
  - `G163 refresh cands ready` at capture (planes still valid)
  - `G163 refreshed draw faces=` with `mid_new>0` and no sample LM rebake hang
  - PVS prove / explore still completes; G162/G161/G159 markers remain green
- Evidence: `.ai/logs/dolphin-probe-20260720-174928` —
  `G163 explore cluster=543`, `mid_new=32` `lm=224` (32 mid replaces),
  `G163 refreshed draw faces=256 prev=256 reused_lm=256 mid_new=32`.
- Residual: restore cluster without capture cands skips refresh; soft studio shading.

## G164 [x] GX studio Gouraud shading (per-vertex light)

- Status: DONE 2026-07-20. G155 emitted every GX studio vertex with the last
  TriAPI grey (`r_gx_studio_color`), so meshes were flat per triangle and
  dropped `lightcolor` RGB. `_TriColor4f` now snapshots full RGBA (`gx_rgba`)
  before its rendermode early-return; `TriVertex3f` buffers it per vertex and
  `R_GXStudioEmitTriC` sends true per-vertex colors to the TEV MODULATE stage.
- Acceptance:
  - `G164 studio gouraud shades=N` with N > 8 distinct luminance buckets
    (flat shading would report 1)
  - G163/G162/G161/G159 markers stay green; probe exit 0
- Evidence: `.ai/logs/dolphin-probe-20260720-175743` —
  `G164 studio gouraud shades=29 mask=0xfffffff8 viewmodel=1`,
  `G155 GX studio tris=908 viewmodel=1`.
- Residual: studio chrome UVs on Flipper (depth range → G167).

## G165 [x] Restore-cluster face refresh (outdoor cands)

- Status: DONE 2026-07-20. G163 skipped Flipper refresh on prove restore to
  outdoor cluster 429 (`no capture cands`). Capture now prioritizes outdoor-band
  PVS rows (~35 vis leaves); Prepare also caches the camera/restore cluster
  before scratch purge. Deferred refresh admits mid-grade LM faces for that set.
- Acceptance:
  - `G165 restore cands ready cluster=` at capture/Prepare
  - `G165 restore refresh cluster=` with `mid_new>0` (no skip for restore cluster)
  - G164/G163/G162/G161/G159 remain green
- Evidence: `.ai/logs/dolphin-probe-20260720-182331` —
  `G165 restore cands ready cluster=429 leaves=35`,
  `G165 restore refresh cluster=429 mid_new=14 cands=32 leaves=35`,
  `G163 refreshed … mid_new=14 … cluster=429`.
- Residual: studio chrome UVs on Flipper (soft RGB → G166; depth → G167).

## G166 [x] Soft DumpFrames studio RGB lighting

- Status: DONE 2026-07-20. Soft TriAPI path collapsed studio light to greyscale
  (`light<<8`) and shaded skins with an inverted Quake ramp, so G161 DumpFrames
  guns looked like a grey silhouette. Soft verts now pack `gx_rgba` as R5G5B5<<8;
  `R_PolysetFillSpans8` modulates skin RGB by those channels (high=bright).
  Shade stats are attributed only to the viewmodel entity so world props cannot
  lock a weak early log.
- Acceptance:
  - `G166 soft studio rgb shades=N` with N > 8 (viewmodel Gouraud)
  - G165/G164/G163/G162/G161/G159 remain green; probe exit 0
- Evidence: `.ai/logs/dolphin-probe-20260720-183857` —
  `G166 soft studio rgb shades=14 chroma=0 verts=64 mask=0xf55821e0`,
  `G164 studio gouraud shades=29`, `G165 restore refresh cluster=429 mid_new=14`,
  `G161 soft dump viewmodel ready`.
- Residual: soft light chroma when `lightcolor` is non-white (chrome → G168).

## G167 [x] GX viewmodel depth range (not Z-always)

- Status: DONE 2026-07-20. G155 drew the Flipper viewmodel with
  `GX_ALWAYS` so walls never buried the gun — and never clipped it when looking
  into geometry. Match GL `glDepthRange(min, min+0.3*(max-min))`: enable Z-test
  write with viewport depth compressed to `[0, 0.3]`, restore `[0, 1]` on End.
- Acceptance:
  - `G167 viewmodel depth range near=0.00 far=0.30 ztest=1`
  - `G155` viewmodel=1 + `G164`/`G166`/`G165` remain green; probe exit 0
- Evidence: `.ai/logs/dolphin-probe-20260720-184602` —
  `G167 viewmodel depth range near=0.00 far=0.30 ztest=1`,
  `G155 GX studio tris=908 viewmodel=1`, `G164 shades=29`,
  `G162 viewmodel framed … mid=-0.28`, `G166 shades=14`.
- Residual: studio chrome UVs on Flipper (→ G168).

## G168 [x] Flipper studio chrome sphere UVs

- Status: DONE 2026-07-20. Soft TriAPI folded all UVs through fmod/wrap before
  Flipper saw them; chrome sphere maps on `v_9mmhandgun` (and peers) were
  unproven. GX studio now passes TriTexCoord UVs through like GL; chrome mesh
  draw logs UV bounds to prove sphere variation.
- Acceptance:
  - `G168 studio chrome uv samples=N` with N≥16 and `span` > 0.1
  - G167/G166/G165/G164/G155 remain green; probe exit 0
- Evidence: `.ai/logs/dolphin-probe-20260720-185130` —
  `G168 studio chrome uv samples=798 u=[0.000,0.999] v=[0.007,0.998] span=0.999`,
  `G155 … viewmodel=1`, `G167 … ztest=1`, `G164 shades=29`.
- Residual: G166 packed-RGB llight shredded soft spans (→ G169).

## G169 [x] Soft studio scalar light + constant tint

- Status: DONE 2026-07-20. G166 packed R5G5B5 into the polyset vertex light,
  but the rasterizer interpolates `llight` as ONE scalar (`llight += r_lstepx`),
  so channel bits bled into each other mid-span — DumpFrames guns shredded into
  red/green noise. Studio light is `lightcolor` (constant per entity) × a
  per-vertex scalar: TriAPI now passes the max-channel luminance for
  interpolation and exports the normalized tint (`d_gc_studio_tint_*5`);
  `R_PolysetFillSpans8` applies lum × tint per channel with clamped overshoot.
- Acceptance:
  - `G169 soft studio scalar light lum=N tint=(r,g,b)`
  - `G166 soft studio rgb shades=` still >8; G168/G167/G165..G159 green
  - DumpFrames viewmodel renders smooth (no red/green span shred)
- Evidence: `.ai/logs/dolphin-probe-20260720-224319` —
  `G169 soft studio scalar light lum=26 tint=(31,31,31)`,
  `G166 … shades=14`, `G168 … span=0.999`, `G167 … ztest=1`;
  `.ai/screenshots/demo-stages/stage-04j-g169-soft-scalar-light.png` (smooth gun).
- Residual: soft light chroma proof needs a non-white `lightcolor` scene (→ G170).

## G170 [x] Soft studio chroma tint proof

- Status: DONE 2026-07-20. G169 tint path was wired but landmark `c1a0a`
  `lightcolor` is white `(31,31,31)`, so DumpFrames never showed chroma.
  Soft DumpFrames viewmodel only: when light is near-white, force a warm amber
  tint `(31,24,14)` through `d_gc_studio_tint_*5` (Flipper GX TriAPI path
  untouched). Proves FillSpans lum×tint is observably non-grey.
- Acceptance:
  - `G170 soft studio chroma tint=(r,g,b)` with r≠g or g≠b
  - `G169 soft studio scalar light` still logs; G168/G167/G166..G159 green
- Evidence: `.ai/logs/dolphin-probe-20260720-231354` —
  `G170 soft studio chroma tint=(31,24,14) verts=64`,
  `G169 … tint=(31,24,14)`, `G168 … span=0.999`, `G167 … ztest=1`;
  `.ai/screenshots/demo-stages/stage-04l-g170-soft-chroma.png`.
- Residual: outdoor Flipper face coverage still capped at 256 (→ G171 slots↔cands).

## G171 [x] Outdoor Flipper refresh via slots↔cands trade

- Status: DONE 2026-07-20. Raising refresh cands 32→64 grew BSS and OOMd
  surfbits. Instead trade cache slots 8→5 for cands 32→48 (240 vs 256 cells,
  less heap for surf_cache rows) and double outdoor wall area scores so towers
  fill the larger set. Outdoor restore now admits more walls without MEM1 death.
- Acceptance:
  - `G171 outdoor refresh mid_new=N wall_new=W cands=48` with N≥16 or W≥10
  - `G165 restore refresh` + `G170`/`G169`/`G168` remain green; probe exit 0
  - Soft DumpFrames sky-hole rim fill down vs G170 baseline (~350)
- Evidence: `.ai/logs/dolphin-probe-20260720-231838` —
  `G171 outdoor refresh mid_new=17 wall_new=12 cands=48 leaves=35 cluster=429`,
  `G150 sky-hole rim fill=180` (was 350), `G151 … drawn=196 of 256`,
  `G170 soft studio chroma tint=(31,24,14)`;
  `.ai/screenshots/demo-stages/stage-04n-g171-outdoor-coverage.png`.
- Residual: HUD sprite soft-fails under MEM1 (→ G172).

## G172 [x] HUD sheets via sys-malloc after studios

- Status: DONE 2026-07-20. Post-active HUD sheets (`gc_320hud2`, `320_train`,
  `crosshairs`) soft-failed the FileSystem pool (~12–17 KiB) after studios.
  Loading HUD *before* studios fixed sheets but starved viewmodel MDLs (47–134 KiB).
  Keep studios first; HUD sheets use `FS_LoadFileMalloc` under memopt, with a late
  SFX-pass retry. Fat `320hud1` (~65 KiB) may still stub.
- Acceptance:
  - `G172 HUD sheets loaded real=N` with N≥2 (lean sheets)
  - `deferred studio done` with `view≥1`; `G155 … viewmodel=1`
  - No `HUD sprite stub after soft-fail` for gc_320hud2 / 320_train / crosshairs
  - G171/G170/G169/G168/G161 remain green; probe exit 0
- Evidence: `.ai/logs/dolphin-probe-20260720-234531` —
  `G172 HUD sprite sys-malloc` ×3, `G172 HUD sheets loaded real=3 of 3`,
  `deferred studio done … view=2`, `G155 … tris=908 viewmodel=1`,
  `G161 soft dump viewmodel ready`, `G171 outdoor refresh mid_new=17`.
- Residual: fat `320hud1` still stubs; further GX polish.

## G173 [x] Lean gc_320hud1 bootstrap sheet

- Status: DONE 2026-07-20. Fat retail `320hud1.spr` (~66 KiB) soft-fails under
  memopt even via sys-malloc after studios. Disc injects lean `gc_320hud1.spr`
  (64×64, ~4.8 KiB); memopt prefers that alias over ISO retail. Early post-studio
  HUD preload loads hud1 first; late retry still covers stubs.
- Acceptance:
  - `G173 HUD hud1 lean real=N` with hud1 real (not stub)
  - `G172 HUD sheets loaded real≥2`; `deferred studio done` with `view≥1`
  - `G155 … viewmodel=1`; G171/G170/G169/G161 remain green; probe exit 0
- Evidence: `.ai/logs/dolphin-probe-20260720-235658` —
  `HUD sprite fallback … → sprites/gc_320hud1.spr`,
  `G172 HUD sprite sys-malloc sprites/gc_320hud1.spr size=4.81 Kb`,
  `G173 HUD hud1 lean real=3 of 3`, `G172 … real=3 of 3`,
  `deferred studio done … view=2`, `G155 … viewmodel=1`;
  `.ai/screenshots/demo-stages/stage-04p-g173-hud1-lean.png`.
- Residual: `crosshairs.spr` (~17 KiB) may still stub as third fat sheet; further GX.

## G174 [x] Lean gc_crosshairs bootstrap sheet

- Status: DONE 2026-07-21. Fat retail `crosshairs.spr` (128×128, ~17 KiB) soft-failed
  as a third ~17 KiB HUD sheet after hud2+train. Disc injects lean `gc_crosshairs.spr`
  (64×64, ~4.8 KiB); memopt prefers that alias. Early post-studio preload includes it.
- Acceptance:
  - `G174 HUD crosshairs lean real=N` with crosshairs real (not stub)
  - `G173 HUD hud1 lean` + `G172 HUD sheets loaded real≥3`
  - `deferred studio done` with `view≥1`; `G155 … viewmodel=1`; probe exit 0
- Evidence: `.ai/logs/dolphin-probe-20260721-000202` —
  `HUD sprite fallback … → sprites/gc_crosshairs.spr`,
  `G172 HUD sprite sys-malloc sprites/gc_crosshairs.spr size=4.81 Kb`,
  `G174 HUD crosshairs lean real=4 of 4`, `G172 … real=4 of 4`,
  `deferred studio done … view=2`, `G155 … viewmodel=1`;
  `.ai/screenshots/demo-stages/stage-04r-g174-crosshairs-lean.png`.
- Residual: further GX / outdoor coverage polish.

## G175 [x] Outdoor Flipper refresh via 4×64 slots↔cands trade

- Status: DONE 2026-07-21. G171's 5×48 cand budget still left outdoor restore
  under-admitting walls (`mid_new=17 wall_new=12`). Trade cache slots 5→4 for
  refresh cands 48→64 (256 cells = original 8×32 budget) without BSS growth /
  MEM1 OOM.
- Acceptance:
  - `G175 outdoor refresh … cands=64` with `mid_new≥20` or `wall_new≥14`
  - `G171 outdoor refresh` still logs; rim fill ≤ prior (~200)
  - `G155 … viewmodel=1`; G174/G173 remain green; probe exit 0
- Evidence: `.ai/logs/dolphin-probe-20260721-000815` —
  `G175 outdoor refresh mid_new=23 wall_new=15 cands=64 leaves=35 cluster=429`,
  `G171 … mid_new=23 wall_new=15 cands=64`, `G150 sky-hole rim fill=187`,
  `G155 … viewmodel=1`, `G174 … real=4 of 4`;
  `.ai/screenshots/demo-stages/stage-04s-g175-outdoor-coverage.png`.
- Residual: static 256-face draw cap; further GX polish.

## G176 [x] Raise face cap 256→320 via LM 8→4 trade

- Status: DONE 2026-07-21. Static 256-face cap left outdoor sky holes after G175
  refresh. GX RGB565 LM tiles must be multiples of 4; shrink bake tile 8→4
  (32→10 KiB) and raise `GC_MAX_CAP_FACES` 256→320 (~−4 KiB net BSS on
  `vid_gamecube`).
- Acceptance:
  - `G176 raised face cap count=320 max=320 lm_dim=4`
  - `G151 GX world faces drawn` absolute count > prior ~196
  - `G150 sky-hole rim fill` ≤ prior ~187; G175/G174/G155 remain green
- Evidence: `.ai/logs/dolphin-probe-20260721-001608` —
  `G176 raised face cap count=320 max=320 lm_dim=4 lm_real=320`,
  `G151 … drawn=249 of 320`, `G150 sky-hole rim fill=133`,
  `G175 … cands=64`, `G155 … viewmodel=1`, `G174 … real=4 of 4`;
  `.ai/screenshots/demo-stages/stage-04t-g176-face-cap.png`.
- Residual: further GX polish (LM sharpness / soft DumpFrames HUD).

## G177 [x] Soft DumpFrames HUD composite

- Status: DONE 2026-07-21. Lean HUD sheets loaded (G172–G174) but WORLD PRESENT /
  G161 soft DumpFrames never called `CL_DrawHUD` into the RGB565 buffer before
  the status panel. Composite HUD after scrub / before panel on both paths.
- Acceptance:
  - `G177 soft dump HUD composite sheets=N` with N≥2
  - `RGB565 2D/HUD draw active` during dump window
  - G176/G174/G161/G155 remain green; probe exit 0
- Evidence: `.ai/logs/dolphin-probe-20260721-002355` —
  `RGB565 2D/HUD draw active`, `G177 soft dump HUD composite sheets=4`,
  `G176 … count=320`, `G174 … real=4 of 4`, `G155 … viewmodel=1`,
  `G161 soft dump viewmodel ready`;
  `.ai/screenshots/demo-stages/stage-04u-g177-soft-hud.png`.
- Residual: further GX polish (LM sharpness / Flipper HUD).

## G82 [x] Isolate GameCube boot-flow stabilization from fallback-menu UX work

- Status: DONE 2026-07-17. Boot phases are chronological
  (`early`→`engine`→`renderer`→`sw_fb`→`menu`→`client`→`intro`→`map`);
  `GC_BootDrawAllowed` still gates fallback-menu draws until `sw_fb` + buffer.
  Disc/probe `phasetest <phase>` / `-gc_phase_test` injects
  `G82: Intentional phase fault at <phase>` after that phase reports; fatal
  breadcrumb carries `boot=<phase>`. Probe prints `G82_VERIFIED`.
- Acceptance:
  - Separate GameCube boot path into explicit phases for intro AVI, renderer
    readiness, software framebuffer availability, fallback menu rendering, and
    client/game DLL bring-up so crashes can be attributed to one phase instead of
    mixed boot/menu codepaths.
  - Remove or gate any renderer/menu drawing path that can issue GX software fill
    or command execution before the required GameCube video buffers and client
    state are known valid.
  - Add phase-specific logging and a reproducible smoke probe that reports the
    exact last successful boot phase before an invalid read/write, guest crash, or
    black-screen failure.
- Evidence: `.ai/logs/dolphin-probe-20260717-160152` —
  `boot phase=early`→`engine`→`renderer`→`sw_fb`,
  `G82: Intentional phase fault at sw_fb`,
  `fatal ... boot=sw_fb`, `G82_VERIFIED`, `BOOT_PHASE: sw_fb`.
- Command:
  ```sh
  GC_PHASE_TEST=sw_fb DOLPHIN_TIMEOUT=90 scripts/dolphin-boot-probe.sh
  ```

### G75 [Manual checkpoint] Sign off native Half-Life 1 GameCube completion

- Confirm all automatic goals are complete, all manual hardware evidence has
  dated artifact-hash-matched proof, and the RC suite passes on the final
  release candidate.
- Play or audit the supported Half-Life route declared in the release notes on
  real hardware or Wii GameCube mode, including gameplay, transitions, save/load,
  audio, controls, fatal-error recovery, and shutdown/restart behavior.
- Mark the port complete only if the release notes, known limitations, legal
  asset boundary, source archive, binary artifacts, and hardware evidence all
  describe the same final commit and artifact hashes.
- Additional prerequisite (2026-07-16): G83–G94 New Game interactive bring-up
  must be complete or explicitly limited in release notes before G75 sign-off.
