Auto-port task for Xash3D GameCube
=================================

Current goal:
Tighten G36 frame budget / restore fuller entity spawn when MEM allows.
Match retail visuals without cutting fill/spawn.

**DONE (2026-08-10/11)**:
- Campaign `CHANGELEVEL_READY` through Nihilanth (`c4a3→c5a1`)
- **TAS** + **G341–G350**: tram spawn/fill; early-AM G334@192
- **G351**: retain world visdata on early AM (`c1a0a/b/c/e`) — CapFaces **281**
- **G352**: exclude cold `c1a0` from vis retain (TryMalloc fail @ HWM≈4.7 Mb);
  keep retain on `c1a0a/b/c/e` + tram. Tram entry `c0a0e→c1a0` stays
  CHANGELEVEL_READY (G325 skip, G334@192, ents=99, HWM≈3.38 Mb).
  Evidence: `.ai/logs/dolphin-probe-g352-tram-c1a0`, `…-g352-c1a0ab`
- **G353** (in progress): retain `c1a0` vis **only** when `sv.startspot` set
  (changelevel / tram entry) so CapFaces can Capture without tipping cold NEWGAME

**NEXT**:
- Validate G353: `c0a0e→c1a0` expect retain + CapFaces ≫0; cold `c1a0` still G325
- Multi-hop toward `c1a0d` (cold single-hop unreliable)
- Restore textured DumpFrames for stills

Rules:
- Force-relink after HLSDK archive changes.
- No Zap/TE map beams from lean StrikeThink.
- No GiveNamedItem(glock/crowbar) in PutInServer under G326 — G103/G120 path.
