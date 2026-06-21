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

## G02 [ ] Establish a repeatable Dolphin boot probe

- Provide a repository command that builds the disc and launches a bounded,
  log-captured Dolphin boot probe.
- Distinguish emulator-host failure from guest-engine failure in its result.
- Preserve useful OSReport output under `.ai/logs/`.

## G03 [ ] Reach initialized GX video

- The native GameCube path initializes GX and presents a visible diagnostic
  frame without relying on desktop OpenGL.
- Failure paths remain observable through OSReport.
- The GameCube verifier and focused source checks pass.

## G04 [ ] Provide native controller input

- Map the first GameCube controller through the engine input abstraction.
- Handle disconnect/reconnect without blocking the host frame loop.
- Document the tested buttons/axes and current limitations.

## G05 [ ] Provide a safe first audio path

- Initialize a GameCube-compatible audio backend or explicitly documented
  silent fallback without breaking engine startup.
- Keep buffers within the console memory budget.
- Document emulator evidence and remaining audio work.

## G06 [ ] Reach the engine console or menu

- Boot from the generated disc with `Half-Life/valve` content discoverable.
- Reach an interactive engine console or menu in Dolphin.
- Capture the boot command, log evidence, and remaining blockers.

## G07 [ ] Load a small Half-Life map

- Load one small map far enough to render a frame and accept controller input.
- Record memory or allocation failures rather than hiding them.
- Keep proprietary game data ignored and outside Git.

## G08 [MANUAL] Validate on physical GameCube hardware

- Boot through an available homebrew loading method on real hardware.
- Record video, input, storage, audio, and stability observations.
