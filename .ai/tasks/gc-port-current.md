Auto-port task for Xash3D GameCube
=================================

Current goal:
Swiss hardware (G38); optional further Lambda hops

**DONE (2026-08-09)**:
- Skip lean world SV_RunThink on non-c0a0 (SCR~16 hang)
- Lean DecalGunshot / TEXTURETYPE / BubbleTrail tip-safe
- Reactor crowbar combat + SCR 32; beams blit on c3a2
- Lambda maps + changelevel hops through c3a2c→c3a2d/f
- c0a0 G36 face-cap tram-only (`GC_IsG36SampleFaceCap`): PASS
  `20260809-130441` (~12.7ms); sticky Prepare draw-cap removed
- c3a2 regression clear `20260809-130544` NEWGAME_READY + SCR32 + beams

**Next open**:
- Swiss / hardware G38 (needs physical SD)
- Optional: more campaign changelevel samples beyond Lambda

Rules:
- Force-relink after HLSDK archive changes.
- No Zap/TE map beams from lean StrikeThink.
- No TriAPI beam emit inside lean DrawEntities.
- No GiveNamedItem(glock) in PutInServer — G103/G120 path only.
