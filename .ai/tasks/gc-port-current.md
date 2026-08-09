Auto-port task for Xash3D GameCube
=================================

Current goal:
Map `env_beam` client draw on c3a2*; harness G36 samples; lean Decal; Swiss

**DONE (G320 reactor map boot + GPU beam)**:
- Inline `spserver.cfg` (disc FS_LoadFile hung on c3a2*); post-smoke Prepare
- Skip non-c0a0 `func_*` Use; ClientFrame-first while disconnected; time-only
  ServerFrame warm after reconnect
- Flipper `R_DrawSegs` blit emit on c3a2: probe `20260809-005246`
  `gpu=1 segs=8 tex=10` + SCR frames=16 (harness still PARTIAL — no G36 samples)
- c0a0 regression `20260809-005353`: NEWGAME_READY + G45_ACTION PASS

**Next**:
- Admit/draw map `env_beam` ents (seeded temp beam only so far); c3a2d
- Arm G36 samples on non-tram maps; lean Decal; Swiss

Rules:
- Force-relink after HLSDK archive changes.
- Do not leave forever beams active during early SCR sustain.
- Do not TriAPI beam emit inside lean DrawEntities.
- Do not GiveNamedItem(glock) in PutInServer — use G103 grant path.
