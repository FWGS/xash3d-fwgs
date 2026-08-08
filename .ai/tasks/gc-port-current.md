Auto-port task for Xash3D GameCube
=================================

Current goal:
Flipper TriAPI→GX studio tris (mesh path yields 0); bounded world thinks

**DONE (2026-08-07 probe 20260807-173754)**:
- Root-caused prior studio hang: lean `w_crowbar` is mesh-only
  (`numtextures=0`/`skinindex=0`); `R_StudioDrawPoints` OOB-indexed the
  studiohdr as textures — fixed with mesh_only white-texture path
- Lean `R_StudioDrawModelInternal`: engine path, `STUDIO_RENDER` only
  (no client StudioDrawModel / STUDIO_EVENTS)
- Draw no longer hangs; EmitBrush `studios=1`; hdr non-null
  (`lean studio Flipper draw models/w_crowbar.mdl gx_tris=0 hdr=0x81745190`)
- `NEWGAME_READY` + G45 actions + G506 PASS
- Keep `lean_player_only`

Next acceptance step:
- Why TriAPI→GX emits 0 tris for lean world studio despite active Begin
  (G155 still unseen on this path)
- Then viewmodel/EFX; bounded world thinks

Rules:
- Make one bounded implementation or validation patch.
- Preserve unmodified Half-Life asset compatibility.
- Run relevant builds and probes.
- Update durable evidence.
- Do not touch generated build output unless generation is the explicit task.
- Do not commit third-party submodule divergence accidentally.
