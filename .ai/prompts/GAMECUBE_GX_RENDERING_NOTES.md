# GameCube GX Rendering Notes

Current state:

- GameCube video initialization lives under `engine/platform/gamecube/` and
  GX renderer code under `ref/gx/`.
- Desktop OpenGL behavior must not be assumed.
- Visible diagnostics and OSReport/console markers matter during bring-up.

Rendering constraints:

- MEM1 pressure is severe. Avoid duplicating BSP, lightmap, texture, model, and
  sprite data without a GameCube-specific budget.
- Temporary visual skips such as disabled lightmaps or reduced studio textures
  should become named quality modes with evidence, not silent regressions.
- Rendering goals need frame evidence, screenshots, OSReport markers, or
  compatibility-probe logs.

Prefer:

- Small, isolated GX/platform changes.
- Existing renderer abstractions and console-port precedents.
- Bounded buffers and explicit failure paths.
