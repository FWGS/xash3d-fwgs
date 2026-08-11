Auto-port task for Xash3D GameCube
=================================

Current goal:
Tighten G36 frame budget / restore fuller entity spawn when MEM allows.
Match retail tram visuals (Flipper fill).

**DONE (2026-08-10/11)**:
- Campaign `CHANGELEVEL_READY` through Nihilanth (`c4a3→c5a1`)
- **TAS replay harness** (seed `gameplay-smoke.tas`)
- **G341–G346**: tram full spawn + vis retain; G278 reset; bootstrap BSP 4 MiB
- **G347 video proof (c0a0 tram vs reference)**: DumpFrames EFB + side-by-side mp4
- **G348 retail Flipper fill (c0a0*)**:
  - Raise post-G36 emit: live 124→192, frame 248→280; sample 48/24→96/48
  - Allow scratch-retain fill on tram (pool 64); reserve fill slots after LM
  - Dark tunnel clear on `c0a0*` (42,46,50) instead of sky blue void
  - Evidence `.ai/logs/dolphin-probe-g348-markers` (`20260810-234715`):
    - `fill=64` capture; `G231 fill=24/192`; CapFaces sample `drawn=96`
    - **CapFaces post-G36 retail drawn=281** (live_budget=192 frame_budget=280)
    - G36 PASS avg≈17.21ms (no DumpFrames); TAS PASS + NEWGAME_READY
  - Host DumpFrames still all-black this session (use G347 EFB set / soft path)

**NEXT**:
- Trial raise G334 on early AM (`c1a0`) if MEM allows (keep G325 vis drop)
- Restore textured DumpFrames for G348 visual side-by-side (host XFB path)
- Author longer TAS scripts for walk-to-trigger experiments (later)

Rules:
- Force-relink after HLSDK archive changes.
- No Zap/TE map beams from lean StrikeThink.
- No GiveNamedItem(glock/crowbar) in PutInServer under G326 — G103/G120 path.
