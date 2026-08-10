Auto-port task for Xash3D GameCube
=================================

Current goal:
Campaign continuity beyond Lambda (Swiss/G38 deferred — needs physical SD)

**DONE (2026-08-09)**:
- Tip-safe PM bad-node (no Host_Error); owned clipnodes remap + hull clear
- c0a0→c0a0a CHANGELEVEL_READY `20260809-131124`
- c3a1 NEWGAME_READY `20260809-131232`
- c3a1→c3a1a CHANGELEVEL_READY `20260809-131328` (landmark a1a1a)
- c4a1 Xen NEWGAME_READY `20260809-184328` — scratch-retain no live edge-walk post-G283
- c0a0a→c0a0b CHANGELEVEL_READY `20260809-184625` — planes via malloc / map-load arena tail
- c0a0 G36 regression PASS `20260809-184736` (avg=12.75ms)
- c0a0b→c0a0c CHANGELEVEL_READY `20260809-205421` (landmark c0a0btoc)
- c4a1→c4a2 CHANGELEVEL_READY `20260809-205526` (landmark c4a2)
- c0a0c→c0a0d CHANGELEVEL_READY `20260809-205629` (landmark c0a0ctod)
- Probe defaults: c0a0b→c / c0a0c→d / c0a0d→e / c0a0e→c1a0 / c4a1→c4a2 / c4a2→c4a2a

**Open (non-physical)**:
- Further tram hops past c0a0d (c0a0d→c0a0e→c1a0) / deeper Xen past c4a2 (optional)

**Deferred**:
- Swiss / hardware G38 (physical SD)

Rules:
- Force-relink after HLSDK archive changes.
- No Zap/TE map beams from lean StrikeThink.
- No TriAPI beam emit inside lean DrawEntities.
- No GiveNamedItem(glock) in PutInServer — G103/G120 path only.
