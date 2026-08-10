Auto-port task for Xash3D GameCube
=================================

Current goal:
Xen continuity (c4a1 → …)

**DONE (2026-08-10)**:
- Through Lambda Core exit to Xen:
  - Surface Tension complete (c2a5→…→c3a1; **G339** unblocked c2a5x→c2a5a)
  - Forget About Freeman:
    - c3a1→c3a1a `.ai/logs/dolphin-probe-20260810-124013`
    - c3a1a→c3a1b `.ai/logs/dolphin-probe-20260810-124323`
    - c3a1b→c3a2e `.ai/logs/dolphin-probe-20260810-124422`
  - Lambda Core:
    - c3a2e→c3a2 `.ai/logs/dolphin-probe-20260810-124520`
    - c3a2→c3a2a `.ai/logs/dolphin-probe-20260810-124620`
    - c3a2a→c3a2b `.ai/logs/dolphin-probe-20260810-124929`
    - c3a2b→c3a2c `.ai/logs/dolphin-probe-20260810-125239`
    - c3a2c→c3a2d `.ai/logs/dolphin-probe-20260810-125408`
    - **c3a2d→c4a1** `.ai/logs/dolphin-probe-20260810-125509`
- **G339**: skip ServerFrame primes on changelevel dest maps
  (unblocked MAP_READY on c2a5a; G278 train hang)

**NEXT**:
- Prove Xen c4a1→… through Interloper / Nihilanth / Endgame
- Tighten G36 frame budget / restore fuller entity spawn when MEM allows

Rules:
- Force-relink after HLSDK archive changes.
- No Zap/TE map beams from lean StrikeThink.
- No GiveNamedItem(glock/crowbar) in PutInServer under G326 — G103/G120 path.
