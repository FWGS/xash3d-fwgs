# Xash3D GameCube Port Rules

Goal: make Xash3D progressively boot and run on Nintendo GameCube using
devkitPPC and libogc.

## Hard constraints

- Do not break existing desktop builds.
- Do not add proprietary platform SDK material or game assets.
- Prefer isolated GameCube platform code and existing platform abstractions.
- Use compile-time platform guards.
- Never describe an untested implementation as working. Mark stubs clearly.
- Keep changes small and reviewable: one blocker per patch.
- Preserve GPL compatibility.
- Update `docs/GAMECUBE_PORT_PLAN.md` in every patch with the exact command run,
  its result, and the next known blocker.

## Hardware constraints

- PowerPC Gekko is big-endian and requires careful alignment.
- There is no general-purpose POSIX operating system.
- Desktop OpenGL and dynamic libraries are unavailable.
- Main memory is 24 MiB; ARAM is 16 MiB and is not ordinary heap memory.
- Prefer bounded or static allocation where practical.
- Use libogc facilities for SD/FAT, timing, controller, video, and networking.

## Milestone order

1. Boot and report diagnostics.
2. Initialize video.
3. Initialize controller input.
4. Probe the filesystem.
5. Start the null or GX renderer.
6. Display the menu.
7. Load game assets.
8. Load a map.

## Patch discipline

- Inspect the current code and build log before editing.
- Fix the smallest demonstrated blocker; do not guess at a large subsystem.
- Do not rewrite a renderer or import large external code in one pass.
- Run `scripts/ai-verify.sh` after editing.
- Stop after one verified patch.
