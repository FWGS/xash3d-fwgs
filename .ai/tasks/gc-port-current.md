Auto-port task for Xash3D GameCube
=================================

Current goal:
Flipper tip-safe lightning emit (no SCR stall); reactor-map beam proof;
lean Decal; Swiss `--require-swiss`

**DONE (G320 path plumbing)**:
- Lean draws additive `CL_DrawBeams` on trans (particles still skipped)
- Admit env_beam/env_laser (cap 8); lean laser TraceLine stubs
- Deferred c0a0 proof beam: arm early, alloc at SCR frame 16, retire with
  `G320 beam draw … tipsafe=deferred`
- Probe `20260808-230137`: NEWGAME_READY + G45_ACTION PASS
- Prior emit proof: `20260808-223150` Flipper EFX tris=16 then hung

**Next**:
- Tip-safe Flipper beam emit that survives SCR sustain (not HUD TriSprite /
  R_DrawSegs; not perpetual TransAdd)
- Prove beams on c3a2d/c3a2b (map still hangs post-spserver on lean)
- Lean-safe gunshot decal / PLAYBACK_EVENT
- Swiss hardware `--require-swiss`

Rules:
- Force-relink after HLSDK archive changes.
- Do not leave forever beams active during early SCR sustain.
- Do not CL_LoadClientSprite heavy FX in G291 seed.
- Do not GiveNamedItem(glock) in PutInServer — use G103 grant path.
- Do not reintroduce classname door-admit helpers.
