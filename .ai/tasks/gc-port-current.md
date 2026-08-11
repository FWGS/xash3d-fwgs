Auto-port task for Xash3D GameCube
=================================

Current goal:
Tighten G36 frame budget / restore fuller entity spawn when MEM allows.

**DONE (2026-08-10/11)**:
- Campaign `CHANGELEVEL_READY` through Nihilanth (`c4a3→c5a1`)
- **TAS replay harness**:
  - Format: `scripts/tas/<name>.tas` (pad segments)
  - Engine: `-gctas` / `gamecube.cfg tas <name>` → synthetic pad player
  - Disc: `--probe-tas NAME`; probe: `DOLPHIN_TAS=<name>`
  - Seed: `gameplay-smoke.tas`
  - Proven: NEWGAME `c0a0` + TAS `.ai/logs/dolphin-probe-20260810-180318`
  - Proven: `c4a1→c4a2` + TAS CHANGELEVEL_READY (TAS completes on source
    before deferred G335 hop)

**NEXT**:
- Tighten G36 frame budget / restore fuller entity spawn when MEM allows
- Author longer TAS scripts for walk-to-trigger experiments (later)

Rules:
- Force-relink after HLSDK archive changes.
- No Zap/TE map beams from lean StrikeThink.
- No GiveNamedItem(glock/crowbar) in PutInServer under G326 — G103/G120 path.
