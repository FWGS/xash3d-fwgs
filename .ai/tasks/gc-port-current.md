Auto-port task for Xash3D GameCube
=================================

Current goal:
Campaign changelevel continuity complete through Nihilanth (c5a1).
Next focus: restore playability gates (G36 budget / fuller entity spawn).

**DONE (2026-08-10)**:
- Xen + Gonarch + Interloper + Nihilanth changelevel chain:
  - c4a1→c4a2 `.ai/logs/dolphin-probe-20260810-143428`
  - c4a2→c4a2a `.ai/logs/dolphin-probe-20260810-143543`
  - c4a2a→c4a2b `.ai/logs/dolphin-probe-20260810-144050` (**G340**)
  - c4a2b→c4a1a `.ai/logs/dolphin-probe-20260810-144143`
  - c4a1a→c4a1b `.ai/logs/dolphin-probe-20260810-144239`
  - c4a1b→c4a1c `.ai/logs/dolphin-probe-20260810-144336`
  - c4a1c→c4a1d `.ai/logs/dolphin-probe-20260810-144431`
  - c4a1d→c4a1e `.ai/logs/dolphin-probe-20260810-144741`
  - c4a1e→c4a1f `.ai/logs/dolphin-probe-20260810-144835`
  - c4a1f→c4a3 `.ai/logs/dolphin-probe-20260810-144926`
  - **c4a3→c5a1** `.ai/logs/dolphin-probe-20260810-145021`
- **G340**: denser Flipper-present skip (G335) extended through c4a/c5a
  (unblocked c4a2a→c4a2b MAP_READY; prior hang after probe gameplay)

**Prior same day**: Apprehension→…→Lambda→Xen start; G339 dest ServerFrame skip.

**NEXT**:
- Tighten G36 frame budget / restore fuller entity spawn when MEM allows
- Optional: c5a1 endgame / outro markers (no further changelevel hop)

Rules:
- Force-relink after HLSDK archive changes.
- No Zap/TE map beams from lean StrikeThink.
- No GiveNamedItem(glock/crowbar) in PutInServer under G326 — G103/G120 path.
