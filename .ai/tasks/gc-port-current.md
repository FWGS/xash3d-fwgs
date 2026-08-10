Auto-port task for Xash3D GameCube
=================================

Current goal:
Anomalous Materials continuity (c1a0 → c1a0d → …)

**DONE (2026-08-09/10)**:
- Tip-safe PM bad-node; owned clipnodes remap + hull clear
- Tram hops through c0a0d CHANGELEVEL_READY (prior)
- c0a0d→c0a0e CHANGELEVEL_READY `20260809-231158` (landmark c0a0dtoe)
- c0a0e→c1a0 CHANGELEVEL_READY `20260809-231716` (landmark c0a0toc1a0)
  G45 actions PASS, G506 PASS (G36 WEAK ~19ms on c1a0)
- G321 lean admit caps (scripted≤12, scientist≤4, sitting≤2, barney≤2)
  — fixed c1a0 `ent private data malloc failed` on monster_barney
  — MAP_READY c1a0 on `20260809-232253` / `20260809-233017`
- Landmark defaults: c1a0→c1a0d / c1a0d→c1a0a / c1a0a→c1a0b

**IN PROGRESS (G323)**:
- c1a0 cold New Game skipped visdata (mempool OOM ~25KiB) → Capture FatPVS
  skipped → prepare re-entered Capture and hung (20260809-233017)
- Fix: libc-pin visdata on TryMalloc fail; Capture novis reentry skip;
  world-center-only capture eye (no edict walk)
- Probe under test: c1a0→c1a0d after rebuild

**Deferred**:
- Swiss / hardware G38 (physical SD)

Rules:
- Force-relink after HLSDK archive changes.
- No Zap/TE map beams from lean StrikeThink.
- No TriAPI beam emit inside lean DrawEntities.
- No GiveNamedItem(glock) in PutInServer — G103/G120 path only.
