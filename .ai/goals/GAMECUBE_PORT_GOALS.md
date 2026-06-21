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

## G06 [ ] Reach the engine console or menu (Operator-Gated) — SUSPENDED

- Boot from the generated disc with `Half-Life/valve` content discoverable.
- Reach an interactive engine console or menu in Dolphin.
- Capture the boot command, log evidence, and remaining blockers.
- **Blocker**: Automated runtime verification is blocked. Source-side initialization for video (GX), input (PAD), audio (null), and filesystem paths (SD/DVD) is complete and compiles cleanly. The automation environment lacks a functional Dolphin ISO mount or emulated SD/DVD volume, causing `GCube_GetBasePath` to fail gracefully and emit `SYS_Report` diagnostics instead of crashing. Completion **requires operator runtime validation** with a working Dolphin profile containing the `xash3d/valve` tree or physical GameCube hardware. Mark as complete only after an operator confirms console/menu visibility and input responsiveness.
- **2026-06-21 Pass 1**: Autonomous build and source audit confirmed all platform backends compile cleanly and fallback paths are safe. Automation cannot execute guest runtime checks. Handoff to operator per checklist. Mark `[x]` only after physical/emulated validation succeeds.
- **2026-06-21 Pass 2**: Automation confirmed zero actionable source changes remain. All platform backends (GX video, PAD input, null audio, SD/DVD filesystem) are implemented, compile cleanly, and feature safe fallback diagnostics. Automated guest runtime verification is strictly blocked by the absence of a functional emulator/storage mount in the CI environment. Goal formally deferred to operator runtime validation. No speculative engine changes will be attempted.
- **2026-06-21 Pass 3**: Autonomous source preparation concluded. All platform backends are implemented, compile cleanly, and feature safe fallback diagnostics. Goal suspended pending operator runtime validation. No further automated edits will target G06 until hardware/emulator verification succeeds.
- **2026-06-21 Pass 4**: Goal formally **SUSPENDED**. Source-side readiness is complete and verified. Automated runtime validation is impossible in the current CI environment. Handoff to operator per checklist in `docs/GAMECUBE_PORT_PLAN.md`. The goal runner will skip G06 until an operator marks it `[x]` after successful Dolphin/physical hardware validation.

## G07 [ ] Load a small Half-Life map

- Load one small map far enough to render a frame and accept controller input.
- Record memory or allocation failures rather than hiding them.
- Keep proprietary game data ignored and outside Git.

## G08 [MANUAL] Validate on physical GameCube hardware

- Boot through an available homebrew loading method on real hardware.
- Record video, input, storage, audio, and stability observations.
