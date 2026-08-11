Auto-port task for Xash3D GameCube
=================================

Current goal:
Tighten G36 frame budget / restore fuller entity spawn when MEM allows.

**DONE (2026-08-10/11)**:
- Campaign `CHANGELEVEL_READY` through Nihilanth (`c4a3→c5a1`)
- **TAS replay harness** (seed `gameplay-smoke.tas`)
- **G341–G344**: tram `c0a0*` retain visdata + disable G334 (full spawn)
- **G345**: reset G277/G278 static train ride across changelevel
  - Fixed `c0a0b→c0a0c` hang (stale train teleported off landmark)
  - Evidence: `.ai/logs/dolphin-probe-20260810-195608`
- **Tram chain under G344+G345** (all CHANGELEVEL_READY):
  - `c0a0→c0a0a` … `c0a0e→c1a0`
- **G346**: bootstrap pk3 BSP size cap 2→4 MiB so `c1a0.bsp` (~2.5 MiB)
  is archived; FST-only dest maps Host_Error when directory find flaps
  to pk3-only (`.ai/logs/dolphin-probe-20260810-200046`)

**NEXT**:
- Trial raise G334 on early AM (`c1a0`) if MEM allows (keep G325 vis drop)
- Investigate why `gcdisc:/xash3d/valve/` directory find flaps to pk3-only
- Author longer TAS scripts for walk-to-trigger experiments (later)

Rules:
- Force-relink after HLSDK archive changes.
- No Zap/TE map beams from lean StrikeThink.
- No GiveNamedItem(glock/crowbar) in PutInServer under G326 — G103/G120 path.
