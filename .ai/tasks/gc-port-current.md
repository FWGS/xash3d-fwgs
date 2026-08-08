Auto-port task for Xash3D GameCube
=================================

Current goal:
Viewmodel or EFX carefully; bounded world thinks

**DONE (2026-08-07 probe 20260807-220848)**:
- Lean Flipper studio draw: `G155 GX studio tris=60 viewmodel=0`
- Root cause of gx_tris=0: leftover `r_gx_effects_tri` made EmitTriC
  count effects instead of studio — clear on Begin/ForceBegin; prefer
  studio accounting while studio is armed
- Direct mesh→GX path in `R_StudioDrawNormalMesh` for GX world draw
- `NEWGAME_READY` + G45 actions + G506 PASS; EmitBrush studios=1

Next acceptance step:
- Re-enable viewmodel or EFX carefully
- Bounded world thinks

Rules:
- Make one bounded implementation or validation patch.
- Preserve unmodified Half-Life asset compatibility.
- Run relevant builds and probes.
- Update durable evidence.
- Do not touch generated build output unless generation is the explicit task.
- Do not commit third-party submodule divergence accidentally.
