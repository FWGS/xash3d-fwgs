Auto-port task for Xash3D GameCube
=================================

Current goal:
Reactor-map beam proof (c3a2*); lean Decal; Swiss `--require-swiss`

**DONE (G320 textured blit emit)**:
- Deferred beam arm → alloc at SCR frame 16 → stash billboard verts + spr tex
- Emit at `R_BlitScreen` via `R_GXEmitPendingLeanBeam` (EffectsTriBegin/End)
- Do **not** TriAPI-emit beams during `DrawEntities` (stalls SCR)
- HUD preload + bootstrap inject `sprites/lgtning.spr`; blit binds spr tex
- Probe `20260809-002241`: armed/seeded `lgtning.spr`, blit `tipsafe=1 tex=87`
  + NEWGAME_READY + G45_ACTION PASS

**Next**:
- Prove beams on c3a2d/c3a2b (map still hangs post-spserver on lean)
- Lean-safe gunshot decal / PLAYBACK_EVENT
- Swiss hardware `--require-swiss`

Rules:
- Force-relink after HLSDK archive changes.
- Do not leave forever beams active during early SCR sustain.
- Do not TriAPI beam emit inside lean DrawEntities.
- Do not GiveNamedItem(glock) in PutInServer — use G103 grant path.
