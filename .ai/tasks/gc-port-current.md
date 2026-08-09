Auto-port task for Xash3D GameCube
=================================

Current goal:
Reactor-map beam draw proof (c3a2d/c3a2b); lean Decal; Swiss `--require-swiss`

**DONE (G320 lasers / electricity)**:
- Lean was skipping **trans** `CL_DrawBeams` — additive env_beam/env_laser
  (reactor lightning) never drew; solid+trans both draw now
- Admit `env_beam`/`env_laser` on `-gcnewgame` (cap 8 lean; fullphysics uncapped)
- Lean-safe: laser StrikeThink TraceLine, no DoSparks/Decal/DamageThink
- Client beams budget 8; stage `sprites/lgtning.spr`
- Probe `20260808-2131*`: c0a0 NEWGAME_READY + G45_ACTION PASS
- DOL ~5880672

**Next**:
- Prove beam draw on c3a2d/c3a2b (map still hangs post-spserver on lean)
- Lean-safe gunshot decal / PLAYBACK_EVENT
- Swiss hardware `--require-swiss`

Rules:
- Force-relink after HLSDK archive changes.
- Do not CL_LoadClientSprite heavy FX in G291 seed (hung PutInServer path).
- Do not GiveNamedItem(glock) in PutInServer — use G103 grant path.
- Do not reintroduce classname door-admit helpers.
