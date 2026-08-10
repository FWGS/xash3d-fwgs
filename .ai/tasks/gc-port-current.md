Auto-port task for Xash3D GameCube
=================================

Current goal:
Unforeseen Consequences continuity (c1a1f → …)

**DONE (2026-08-10)**:
- Continuity chain proven through UC:
  - c1a0b→c1a0c `.ai/logs/dolphin-probe-20260810-042242`
  - c1a0c→c1a1 `.ai/logs/dolphin-probe-20260810-043022`
  - c1a1→c1a1a `.ai/logs/dolphin-probe-20260810-043140`
  - **c1a1a→c1a1f** `.ai/logs/dolphin-probe-20260810-043258`
    landmark `c1a1atof`; ladder 10/10; G45 actions PASS; G36/G506 WEAK
- Earlier same day: tram→AM through c1a0a→c1a0b
- G321–G336 lean MEM / HUD / Flipper present / entity-tail gates
- G335 denser list covers c1a0e + c1a1–c1a4

**NEXT**:
- Prove next hop from c1a1f
- Tighten G36 frame budget / restore fuller entity spawn when MEM allows

Rules:
- Force-relink after HLSDK archive changes.
- No Zap/TE map beams from lean StrikeThink.
- No GiveNamedItem(glock/crowbar) in PutInServer under G326 — G103/G120 path.
