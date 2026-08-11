Auto-port task for Xash3D GameCube
=================================

Current goal:
Tighten G36 frame budget / restore fuller entity spawn when MEM allows.

**DONE (2026-08-10/11)**:
- Campaign `CHANGELEVEL_READY` through Nihilanth (`c4a3→c5a1`)
- **TAS replay harness** (seed `gameplay-smoke.tas`)
- **G341–G346**: tram full spawn + vis retain; G278 reset; bootstrap BSP 4 MiB
- **G347 video proof (c0a0 tram vs reference)**:
  - Bake `dumpframes` → `-gcdumpframes`; DumpFrames EFB with CopyToVRAM
  - Live capture `.ai/logs/dolphin-probe-g347-efbdump` (251 frames, 250 textured)
  - Gates: NEWGAME_READY + TAS PASS + CapFaces drawn=48 + G278 tram ride
    (`.ai/logs/dolphin-probe-20260810-204052`)
  - Artifacts under `.ai/screenshots/video-proof-g347/`:
    - `c0a0-tram-gameplay.mp4` (live)
    - `c0a0-tram-reference.mp4` (demo-stage G281 ride)
    - `c0a0-tram-reference-vs-live.mp4` (side-by-side)
    - `comparisons/reference-vs-live-tram.png`
  - Structural match to HL1 tram intro: monorail + hazard-stripe tunnel

**NEXT**:
- Trial raise G334 on early AM (`c1a0`) if MEM allows (keep G325 vis drop)
- Improve Flipper draw fill (lean CapFaces still shows blue void gaps)
- Author longer TAS scripts for walk-to-trigger experiments (later)

Rules:
- Force-relink after HLSDK archive changes.
- No Zap/TE map beams from lean StrikeThink.
- No GiveNamedItem(glock/crowbar) in PutInServer under G326 — G103/G120 path.
