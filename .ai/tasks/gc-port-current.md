Auto-port task for Xash3D GameCube
=================================

Current goal:
Pack studios into lean snapshots / bounded world thinks (after clip+PreThink)

**DONE (2026-08-07 lean player clip + PreThink)**:
- Owned compact clipnodes for all `-gcnewgame` (no scratch alias hang)
- Present-path arm only; post-present ServerFrame after Render(1)
- Probe `20260807-061405`: clip move / clip proof / PreThink ready + G506 PASS
- Earlier: G506, BoundedGC player-only snapshots (`20260807-041023`)

Next acceptance step:
- Carefully admit studios into lean snapshots (drop lean_player_only)
- Bounded world thinks (still deferred)
- Clear post-clip Render stall on deferred studios if it blocks Host_Frame
- Keep fullphysics New Game regression green

Rules:
- Make one bounded implementation or validation patch.
- Preserve unmodified Half-Life asset compatibility.
- Run relevant builds and probes.
- Update durable evidence.
- Do not touch generated build output unless generation is the explicit task.
- Do not commit third-party submodule divergence accidentally.
