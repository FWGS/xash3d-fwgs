Auto-port task for Xash3D GameCube
=================================

Current goal:
Anomalous Materials continuity (c1a0d → c1a0a → …)

**DONE (2026-08-10)**:
- Tram: c0a0d→c0a0e→c1a0 CHANGELEVEL_READY
- c1a0→c1a0d CHANGELEVEL_READY `.ai/logs/dolphin-probe-20260810-005013`
  G45 actions PASS, ladder 10/10, G36 WEAK (~101ms), G506 WEAK
- G321 lean entity caps; G322 Capture novis reentry skip; G325 skip visdata
- G326 pre-map client ensure + keep + skip crowbar seed
- G327 defer prepare HUD; skip G290 late HUD sheets under lean newgame

**NEXT**:
- Prove c1a0d→c1a0a (landmark c1a0dtoa)
- Then deeper AM hops

Rules:
- Force-relink after HLSDK archive changes.
- No Zap/TE map beams from lean StrikeThink.
- No GiveNamedItem(glock/crowbar) in PutInServer under G326 — G103/G120 path.
