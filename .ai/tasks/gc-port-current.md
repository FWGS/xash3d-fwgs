Auto-port task for Xash3D GameCube
=================================

Current goal:
Tighten G36 frame budget / restore fuller entity spawn when MEM allows.
Match retail visuals without cutting fill/spawn.

**DONE (2026-08-10/11/12)**:
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
  - Fill reserve = actual `fill_n`; denser floors stay LM-owned
  - Probe: CapFaces `drawn=280`; fill **0→11**; CHANGELEVEL_READY
  - Evidence: `.ai/logs/dolphin-probe-g363-c1a0d-seams`
- **G364**: denser portal/vis center seam
  - Skip G281 tram −X restream on denser dest; restream along dump forward
  - OR portal-neighbor cluster into cap/live (`cl=0 neigh=77`)
  - In-room dump-eye standoff 224+72 → 128+24 (was outside hull)
  - Probe: center_void **22.5%→0.1%**; grey **18.8%→0.8%**; CapFaces `drawn=280`
  - Evidence: `.ai/logs/dolphin-probe-g364-c1a0d-portal`
  - Stills: `.ai/screenshots/g364-dumpframes/`
- **G365**: denser dump-eye FOV — hull-walk along room-side wall normal
  (AABB visleaf, not live PointInLeaf); no lateral offset; stand **128→192**
  - Probe: hull=1 flip=0 area=33840; CapFaces `drawn=280`; uniq **54k→61k**;
    center_void **7.8%→1.7%**; stills show console room + hallway around bulkhead
  - Evidence: `.ai/logs/dolphin-probe-g365-c1a0d-fov`
  - Stills: `.ai/screenshots/g365-dumpframes/`
- **G367**: CapFaces begin/end once-per-map (not `tr.framecount<=3`)
  - Dual-hop `c0a0e→c1a0→c1a0d`: begin/end on **c1a0 f=12**, **c1a0d f=24**
    drawn=280 (old gate would skip both hops)
  - Tip-safe DumpFrames `c1a0a→c1a0d` still CHANGELEVEL_READY, drawn=280
  - Evidence: `.ai/logs/dolphin-probe-g367-capfaces-map`
- **G368**: dual-hop DumpFrames — dump only on final `-gcchangelevel2` dest;
  skip soft latch when hop2 set; hop1 takes G335 short path under DumpFrames;
  skip lean EFX during EFB dump hold
  - Tip-safe denser `c1a0a→c1a0b→c1a0c`: G368 defer on hop1; CapFaces
    `drawn=280` on c1a0c; `G362 Flipper EFB dump presents`; CHANGELEVEL_READY;
    late stills uniq≈12.8k center_void=0%
  - Evidence: `.ai/logs/dolphin-probe-g368-dual-dumpframes`
  - Stills: `.ai/screenshots/g368-dumpframes/`
- **G369**: lean FatPVS + post-spawn CapFaces so tram BSP finishes entity spawn
  - Skip packed all-cluster nodebits; lean-first under `-gcnewgame`
  - Defer CapFaces / leafboxes / surf-cache until after entity lump
  - Prefer static CSoundEnt (2176) before libc malloc at HWM tip
  - Cold `c0a0e` NEWGAME_READY; tip-safe `c1a0a→c1a0d` CHANGELEVEL_READY;
    tram dual-hop `c0a0e→c1a0→c1a0d` CHANGELEVEL_READY, CapFaces drawn=280
  - Evidence: `.ai/logs/dolphin-probe-g369-c0a0e-spawn`,
    `.ai/logs/dolphin-probe-g369-tipsafe-c1a0a-c1a0d`,
    `.ai/logs/dolphin-probe-g369-tram-dual`
- **G370**: tram DumpFrames dual-hop re-validated after G369
  - `c0a0e→c1a0→c1a0d` + `DOLPHIN_DUMP_FRAMES=1`: CHANGELEVEL_READY
  - G368 defer on c1a0; G362 Flipper EFB dump on c1a0d; CapFaces drawn=280
  - G365 dump-eye hull-walk; late stills uniq≈52k, center_void≈0.6%
    (console room + hallway around bulkhead)
  - Evidence: `.ai/logs/dolphin-probe-g370-tram-dumpframes`
  - Stills: `.ai/screenshots/g370-dumpframes/`

**NEXT**:
- Optional: dump yaw / eye pick where denser dump stills show edge cyan/sky seams
- Optional: tighten G36 frame budget / restore fuller entity spawn when MEM allows

Rules:
- Force-relink after HLSDK archive changes.
- No Zap/TE map beams from lean StrikeThink.
- No GiveNamedItem(glock/crowbar) in PutInServer under G326 — G103/G120 path.
- Enter `c1a0`/`c1a0d` via changelevel, not cold NEWGAME.
- Dual-hop: `DOLPHIN_CHANGELEVEL` + `DOLPHIN_CHANGELEVEL2` (rebuild disc).
