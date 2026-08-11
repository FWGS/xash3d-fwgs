Auto-port task for Xash3D GameCube
=================================

Current goal:
Tighten G36 frame budget / restore fuller entity spawn when MEM allows.
Match retail visuals without cutting fill/spawn.

**DONE (2026-08-10/11)**:
- Campaign `CHANGELEVEL_READY` through Nihilanth (`c4a3→c5a1`)
- **G341–G355**: tram/AM vis + denser `c1a0d` tip-safe hop
- **G356–G358**: dual-hop harness + CapFaces sample/`drawn=` on denser dest
- **G359–G360**: DumpFrames tram soft latch + denser Flipper EFB path
- **G361**: denser live steal LM-caps → Capture live **75→248**
  - Evidence: `.ai/logs/dolphin-probe-g361-c1a0d-live`
- **G362**: denser DumpFrames show CapFaces world (not HUD/sky)
  - Wall-aim despite G212; always CapFaces before dump Present
  - Probe: CapFaces `drawn=250`; late stills uniq≈53k
  - Evidence: `.ai/logs/dolphin-probe-g362-c1a0d-dumpframes`
- **G363**: denser CapFaces floor/void seam reduction
  - `GC_CapFaceIsLive` / `LiveFaceEmitsGeom` only claim live emit window (192)
  - Fill reserve = actual `fill_n` (was starving ~48 LM slots)
  - Denser floors/ceilings stay LM-cap owned (no live overflow dead zone)
  - Floor LiveViewScore half-boost under overlap
  - Probe: CapFaces `drawn=280` (full budget); fill **0→11**; center
    floorish **0.8%→12.3%**; uniq_center **15k→25k**; CHANGELEVEL_READY
  - Evidence: `.ai/logs/dolphin-probe-g363-c1a0d-seams`
  - Stills: `.ai/screenshots/g363-dumpframes/`

**NEXT**:
- Optional: CapFaces begin/end once-per-map (not only drawn stash)
- Optional: dual-hop DumpFrames (I/O-heavy; tip-safe single hop proven)
- Optional: remaining denser vertical portal/vis seam (center slice)

Rules:
- Force-relink after HLSDK archive changes.
- No Zap/TE map beams from lean StrikeThink.
- No GiveNamedItem(glock/crowbar) in PutInServer under G326 — G103/G120 path.
- Enter `c1a0`/`c1a0d` via changelevel, not cold NEWGAME.
- Dual-hop: `DOLPHIN_CHANGELEVEL` + `DOLPHIN_CHANGELEVEL2` (rebuild disc).
