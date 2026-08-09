Auto-port task for Xash3D GameCube
=================================

Current goal:
Reactor Host_Frame unstick + lean Decal + Swiss + c3a2d

**DONE**:
- Skip lean world SV_RunThink on non-c0a0 (hang after crowbar G105 / SCR~16)
  Probe `20260809-013402` c3a2: NEWGAME_READY + skip world think logged
- Lean DecalGunshot tip-safe (skip TE; FireBulletsPlayer calls it); G293 seed remains
- Swiss: evidence writer adds OGC stack; fixture layout + ingest COMPLETE packet
- c3a2d `20260809-013456`: NEWGAME_READY + G36 PASS + G45_ACTION PASS

Rules:
- Force-relink after HLSDK archive changes.
- No Zap/TE map beams from lean StrikeThink.
- No TriAPI beam emit inside lean DrawEntities.
- No GiveNamedItem(glock) in PutInServer — G103/G120 path only.
