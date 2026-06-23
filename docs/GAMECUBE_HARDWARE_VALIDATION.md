# GameCube Hardware Validation

This checklist is the handoff path for goals that automation cannot complete
inside Dolphin alone.

## Required Setup

- A homebrew-capable GameCube or compatible Wii/GameCube loader path.
- One supported controller in port 0.
- A known-good video output path and capture method.
- The generated `OUT/bin/boot.dol` or `OUT/xash3d-gc.iso`.
- The same legal local `Half-Life/valve` asset set used by the Dolphin probes.
- A writable route if testing saves/configs: SD Gecko, memory-card adapter, or
  the chosen storage backend from the port plan.

## Record For Each Run

- Date, hardware revision, loader, video mode, and storage route.
- Artifact commit hash and build command.
- Boot result: no signal, loader failure, bootstrap log, menu, map, gameplay.
- Controller result: connect, movement, look, use, fire, menu/console.
- Storage result: first boot, second boot, config write, save/load if enabled.
- Audio result: null fallback, effects, ambient/music when implemented.
- Stability result: clean exit, bounded hang, crash, thermal or power issue.

## Minimum Completion Bar

The native port is not hardware-complete until a real console can:

- Boot the selected artifact.
- Reach the engine readiness marker.
- Load a legal Half-Life map.
- Spawn the player and accept controller input.
- Run long enough to observe frame pacing and memory behavior.
- Exercise the current audio/storage policy without an unbounded hang.

## Evidence Format

Add a dated entry to `docs/GAMECUBE_PORT_PLAN.md` with:

- The exact artifact and command used to build it.
- The loader/hardware route.
- A log, capture, or photo/video reference kept outside Git if it contains
  proprietary assets.
- The next concrete blocker when the run fails.
