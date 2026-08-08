Auto-port task for Xash3D GameCube
=================================

Current goal:
EFX carefully or bounded world thinks

**DONE (2026-08-08 probe 20260808-022209)**:
- Lean Flipper viewmodel: `G155 GX studio tris=329 viewmodel=1`
- Root cause of silent VM skip: `r_drawviewmodel` stayed 0 on lean
  (V_RenderView pump skips force-on). Lean landmark mesh bypasses that
  gate; ForceBegin for VM after world studio End.
- EmitBrush rebinds G105 crowbar when studios admit
- `NEWGAME_READY` + G45 actions + G506 PASS; G164/G167 also green

Next acceptance step:
- Re-enable EFX carefully (still skipped on lean path)
- Bounded world thinks
- Keep `lean_player_only` until proven otherwise

Rules:
- Make one bounded implementation or validation patch.
- Preserve unmodified Half-Life asset compatibility.
- Run relevant builds and probes.
- Update durable evidence.
- Do not touch generated build output unless generation is the explicit task.
- Do not commit third-party submodule divergence accidentally.
