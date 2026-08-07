Auto-port task for Xash3D GameCube
=================================

Current goal:
Restore lean `-gcnewgame` player clip / PreThink after tram-only warm

**DONE (2026-08-07 lean G506 + BoundedGC snapshots)**:
- Prepare kick, tram LinkEdict skip, tram-only physics, G105 crowbar
- Probe `20260807-025038` / `032158`: G506 PASS
- Lean BoundedGC restored (`20260807-041023`):
  - particle cycle guards + lean frame-1 EFX(trans) skip
  - pre-present `Host_ServerFrame` ×16 prime in Prepare
  - lean `cs_spawned` + loopback netchan in `SV_GCPrimeDirectMapPlayer`
  - player-only pack (`G319 entities=1 lean_player_only=1`)
  - `WriteEntities tick` + `SendClientDatagram ready … post-G36`
  - G36/G45/G506/LADDER still PASS

Next acceptance step:
- Restore lean player clip / PreThink without Host_Frame hang
- Carefully admit studios into lean snapshots (drop lean_player_only)
- Keep fullphysics New Game regression green

Rules:
- Make one bounded implementation or validation patch.
- Preserve unmodified Half-Life asset compatibility.
- Run relevant builds and probes.
- Update durable evidence.
- Do not touch generated build output unless generation is the explicit task.
- Do not commit third-party submodule divergence accidentally.
