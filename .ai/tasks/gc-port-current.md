Auto-port task for Xash3D GameCube
=================================

Current goal:
Anomalous Materials continuity (c1a0b → c1a0c)

**DONE (2026-08-10)**:
- Tram: c0a0d→c0a0e→c1a0 CHANGELEVEL_READY
- c1a0→c1a0d CHANGELEVEL_READY `.ai/logs/dolphin-probe-20260810-005013`
- c1a0d→c1a0a CHANGELEVEL_READY `.ai/logs/dolphin-probe-20260810-035123`
- **c1a0a→c1a0b CHANGELEVEL_READY** `.ai/logs/dolphin-probe-20260810-040544`
  G45 actions PASS, ladder 10/10, G36 WEAK (~64ms avg), G506 WEAK
- G321 lean entity caps; G322 Capture novis reentry skip; G325 skip visdata
- G326 pre-map client ensure + keep + skip crowbar seed
- G327 defer prepare HUD; skip G290 late HUD sheets under lean newgame
- G328 FS_CopyImage NULL-safe after ImageLib soft-fail
- G329/G330 soundent + small-ent private slab fallbacks
- G331 soft-fail ref_gx zone; G332 inhibit sitting scientists
- G334 tail essentials-only after index 128 (keep landmark/changelevel/start)
- G335 skip Flipper present pump on denser AM → deferred G68 hop
- G336 lean HUD for denser AM + HUDLIST static pool + skip MOTD/Menu tail
  (unblocked cold New Game HUD_Init hang after StatusIcon on c1a0a)

**NEXT**:
- Prove c1a0b→c1a0c (landmark `c1a0btoc`)
- Tighten G36 frame budget / restore fuller entity spawn when MEM allows

Rules:
- Force-relink after HLSDK archive changes.
- No Zap/TE map beams from lean StrikeThink.
- No GiveNamedItem(glock/crowbar) in PutInServer under G326 — G103/G120 path.
