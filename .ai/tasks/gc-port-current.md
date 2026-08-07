Auto-port task for Xash3D GameCube
=================================

Current goal:
Restore lean `-gcnewgame` client snapshots / WriteEntities without hang

**DONE (2026-08-07 lean New Game → G506 PASS)**:
- Smoke ISO bakes `-gcnewgame` via `gamecube.cfg` (`--probe-newgame`)
- Crowbar `SV_SetModel` weapon stub (no Mod_ForName hang)
- Skip intro tram/player `LinkEdict` on lean ride teleport
- Lean `SV_Physics` tram-only; skip hanging `SV_SendClientMessagesBoundedGC`
- `Host_Frame` calls `GC_PrepareNewGameWorldPresent()` when G36 done and
  `!world_ready` (fixes ServerFrame warm spin)
- Lean crowbar `Mod_GCEnsureLandmarkViewModel` for G105 (not fullphysics-only)
- Probe `20260807-025038` + reconfirm `20260807-032158`: `G281 cl=295`,
  G105 crowbar ready, G36/G45/G506 PASS (`G172,lean-HUD,G105`), LADDER 10/10

**Tried / rolled back**:
- Restoring BoundedGC → first Flipper present hung in `CL_DrawEFX(trans)`
  (`20260807-030413`); lean snapshots remain no-op for now

Next acceptance step:
- Reintroduce minimal lean snapshots without re-hanging first present
- Restore lean player clip / PreThink after tram-only warm is stable
- Keep fullphysics New Game regression green (`20260807-015023` class)

Rules:
- Make one bounded implementation or validation patch.
- Preserve unmodified Half-Life asset compatibility.
- Run relevant builds and probes.
- Update durable evidence.
- Do not touch generated build output unless generation is the explicit task.
- Do not commit third-party submodule divergence accidentally.
