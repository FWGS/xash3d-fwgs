Auto-port task for Xash3D GameCube
=================================

Current goal:
Harness G36 samples on non-tram maps; lean Decal; Swiss; c3a2d

**DONE (G320 map env_beam → GPU blit)**:
- Root cause: c3a2 `env_beam`s are TOGGLE+RANDOM (no STARTON) + `life=.5`
  (client TE via StrikeThink/Zap). Lean Zap hung Host_Frame.
- Fix: admit + Activate marker; client seeds forever follow beams from
  `env_beam` edicts at SCR-ready (`CL_GCSeedMapEnvBeamEdicts`); Flipper blit.
- c3a2 probe `20260809-010537`: `map env_beam client seed n=2`,
  `map beam draw … tipsafe=pending-gpu`, `CL_DrawBeams … temp=3`,
  `blit emit tipsafe=1 gpu=1`, SCR frames=16 (harness still PARTIAL)
- c0a0 regression `20260809-010713`: NEWGAME_READY + G45_ACTION PASS

**Next**:
- Arm G36 samples on non-tram maps; lean Decal; Swiss; c3a2d

Rules:
- Force-relink after HLSDK archive changes.
- Do not leave forever beams active during early SCR sustain.
- Do not TriAPI beam emit inside lean DrawEntities.
- Do not Zap/TE map beams from lean StrikeThink (hangs Host_Frame).
- Do not GiveNamedItem(glock) in PutInServer — use G103 grant path.
