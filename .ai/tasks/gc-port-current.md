Auto-port task for Xash3D GameCube
=================================

Current goal:
Tighten G36 frame budget / restore fuller entity spawn when MEM allows.
Match retail visuals without cutting fill/spawn.

**DONE (2026-08-10/11)**:
- Campaign `CHANGELEVEL_READY` through Nihilanth (`c4a3→c5a1`)
- **TAS** + **G341–G353**: tram/AM vis retain; changelevel-only `c1a0`
- **G354**: changelevel-only vis on denser `c1a0d`
  - `c1a0a→c1a0d` CapFaces **1→242**, live=75, HWM≈2.89 Mb
- **G355**: G334 128→192 on changelevel `c1a0d` (`sv.startspot`)
  - ents **84→113**; CapFaces 242 unchanged; HWM flat ≈2.89 Mb
  - Evidence: `.ai/logs/dolphin-probe-g355-c1a0d` (pre-vis: `…-g354-*`)

**NEXT**:
- Campaign path `c1a0→c1a0d` (dual-hop harness or staged probes)
- Restore textured DumpFrames for stills
- Optional: raise CapFaces live pool on `c1a0d` (Capture n=75 vs budget 192)

Rules:
- Force-relink after HLSDK archive changes.
- No Zap/TE map beams from lean StrikeThink.
- No GiveNamedItem(glock/crowbar) in PutInServer under G326 — G103/G120 path.
- Enter `c1a0`/`c1a0d` via changelevel, not cold NEWGAME.
