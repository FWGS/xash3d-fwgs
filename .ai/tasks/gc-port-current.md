Auto-port task for Xash3D GameCube
=================================

Current goal:
Office Complex continuity (c1a2a → …)

**DONE (2026-08-10)**:
- Through UC into Office Complex:
  - c1a1f→c1a1b `.ai/logs/dolphin-probe-20260810-044126`
  - c1a1b→c1a1c `.ai/logs/dolphin-probe-20260810-044242`
  - c1a1c→c1a1d `.ai/logs/dolphin-probe-20260810-044610` (**G337** slash landmark)
  - c1a1c→c1a2 `.ai/logs/dolphin-probe-20260810-044727`
  - **c1a2→c1a2a** `.ai/logs/dolphin-probe-20260810-044850`
    landmark `one`; ladder 10/10; G45 actions PASS; G36 WEAK (~50ms)
- G337: disc cfg allows `/` in landmark targetnames

**NEXT**:
- Prove next hop from c1a2a
- Tighten G36 frame budget / restore fuller entity spawn when MEM allows

Rules:
- Force-relink after HLSDK archive changes.
- No Zap/TE map beams from lean StrikeThink.
- No GiveNamedItem(glock/crowbar) in PutInServer under G326 — G103/G120 path.
