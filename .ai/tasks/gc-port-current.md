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
- **G371**: lean NPC studios on denser AM
  - Promote scientist/barney/headcrab at present≥24; studio budget 400→640 KiB
  - Rebind `sv.models[]` after promote; EmitBrush admits NPC meshes
  - Disc smoke/gc_studio stage scientist+barney+roach
  - Tip-safe `c1a0a→c1a0d`: CHANGELEVEL_READY; real studio loaded ×3 NPC;
    EmitBrush `studios=3 studio_seen=3`; Flipper draw `scientist.mdl gx_tris=740`
  - Evidence: `.ai/logs/dolphin-probe-g371-npc-studios`
- **G372**: denser tip-safe `c1a0d` full G334 spawn (disable essentials-only)
  - `G343 G334 disabled (full spawn) map=c1a0d`; inhibited **55→45**;
    EmitBrush ents **154→164**; HWM flat ≈2.90 Mb; G371 studios still draw
  - Evidence: `.ai/logs/dolphin-probe-g372-c1a0d-fullspawn`
- **G373**: denser tip-safe lean admit — scripted **12→20**, scientists **4→6**
  - inhibited **45→40**; ents **164→169**; G321 admit n=16; HWM≈2.90 Mb
  - Evidence: `.ai/logs/dolphin-probe-g373-c1a0d-admit`
- **G374**: denser G36 sample hygiene
  - Extend `GC_IsG36SampleFaceCap` to tip-safe AM dests (CapFaces sample
    drawn **249→89**, then retail 250)
  - Arm G36 only on final `-gcchangelevel` dest (not source c1a0a)
  - Denser warmup **2→4** presents
  - Tip-safe: G36 avg **≈61ms→38ms** (still WEAK vs 16.67; steady sample
    frames ≈8–37ms); CHANGELEVEL_READY; studios still draw
  - Evidence: `.ai/logs/dolphin-probe-g374-g36-denser`
- **G375**: DumpFrames video evidence — denser NPC studios on CapFaces world
  - Force `Mod_GCPromoteLeanNpcStudios` before G362 EFB dump (present often <24)
  - End G36 sample face-cap before dump so CapFaces use retail budget
  - Tip-safe DumpFrames `c1a0a→c1a0d`: CHANGELEVEL_READY; G375 dump NPC
    `npc=4`; EmitBrush `studios=4`; Flipper `scientist.mdl gx_tris=740`;
    CapFaces dump **drawn=281**; late stills uniq≈103k center_void≈0.1%
  - Stills show console/hallway + white NPC studio meshes (untextured /
    transform glitch — next studio polish)
  - Evidence: `.ai/logs/dolphin-probe-g375-npc-dumpframes`
  - Stills: `.ai/screenshots/g375-dumpframes/`
  - Video: `.ai/screenshots/video-proof-g375/c1a0d-npc-studios-dumpframes.mp4`
- **G376**: NPC T.mdl skins + rest-pose + DumpFrames humanoid proof
  - T.mdl merge + soft skins: scientist **31/31**, barney **26/26**, headcrab **5/5**
  - Disc stages `scientistt`/`barneyt`/`headcrabt`/`zombiet`
  - Flipper: `R_GXStudioRebindPending` after ForceBegin; **GX bind ok**
    `#models/scientist/Sci3(Back2).mdl 48x41`
  - CPU/emit bbox compact ~27×13×60u at entity origin (not exploded verts)
  - Hallway-filling white planes were the **lean viewmodel** (`lean_vm`
    ignored `r_drawviewmodel 0`); skip VM during EFB dump hold
  - DumpFrames: one CapFaces+studio emit then Present-only under EFB hold
  - Lean NPC SetupBones: group-0 idle frame 0 + **rest-pose** from `bone.value`
  - Studio TEV REPLACE; GX studio stream matches world LIT (POS+CLR+TEX0+TEX1)
  - Standoff dump eye at hull-valid 128u (`-1712,528,-220`) without unlocking
    G212 (PreferOutdoor restream hung). World PVS stays wall-aim → grey void
    + NPC. Scientist is a textured humanoid (face, glasses, coat, shoes)
  - CapFaces dump **drawn=281**; `gx_tris=740`; CHANGELEVEL_READY
  - Evidence: `.ai/logs/dolphin-probe-20260812-060912`
  - Stills: `.ai/screenshots/g376p-standoff/` (viewmodel-off hallway:
    `.ai/screenshots/g376o-novm/`)
  - In-room CapFaces+NPC: mid-frame surfbits restream hung Host_Frame
    (probe 20260812-061959 after dump aim). G212 stays locked.
  - Dump eye at NPC **before** G364 ride restream (hallway wall-aim as
    picker only). Skip cluster-0 / outdoor portal-OR. Prefer scientist.
    Probe `20260812-225922`: aim scientist org=(-1840,528,-256)
    eye=(-1712,528,-220) cl=111; CapFaces **drawn=277**; gx_tris=740;
    CHANGELEVEL_READY. Stills: textured humanoid scientist + some world
    brushes (far industrial slabs still rank over nearby hallway).
  - Evidence: `.ai/logs/dolphin-probe-20260812-225922`
  - Stills: `.ai/screenshots/g376w-hallway-npc/`
- **G377**: group-0 studio animation (lean NPCs)
  - SetupBones uses `R_StudioEstimateFrame` + `CalcRotations` (no rest-pose
    stomp). Seqgroup 1+ / OOB remap to an in-base `idle*` (GetAnim stays in
    the .mdl). Skip latched seq crossfade into seqgroup 1+.
  - Bone AABB >220u → rest-pose fallback that frame (explode guard)
  - EmitBrush: do not snap `animtime` to `cl.time` when edict animtime is 0
    (that froze `dfdt` at 0)
  - DumpFrames: clear+CapFaces+studio each step, +0.20s, hold+2 Present
  - Probe `20260812-232325`: CHANGELEVEL_READY; scientist seq=0 `'walk'`
    f=9→15/17 fps=20 loop; `gx_tris=740`; CapFaces **drawn=277**; compact
    bbox ~32×17×60u; no explode fallback. Dump TGA **15 unique** hashes;
    stills 25 vs 34 differ **7.8%** pixels (pose change)
  - Evidence: `.ai/logs/dolphin-probe-20260812-232325`
  - Stills: `.ai/screenshots/g377-anim/`
- **G378**: dump-eye centroid rank (drop far slabs)
  - Cap keep-score + live rerank use baked centroid vs NPC dump eye
    (plane dist treated coplanar industrial walls as near)
  - Skip Flipper emit if centroid >768u (`GC_G378DumpSkipFarPoint`)
  - G212 stays locked — re-score already-baked slots after dump aim
  - Probe `20260812-232949`: CHANGELEVEL_READY; rank n=284 **near=102
    far=182** mind=271 maxd=1467 eye=(-1712,528,-220); dump CapFaces
    **drawn=98** (was 277 far-slab fill); `gx_tris=740`; walk f advances
  - Stills: hallway strip (blue-lit baseboard) + walking scientist, not
    distant industrial slabs
  - Evidence: `.ai/logs/dolphin-probe-20260812-232949`
  - Stills: `.ai/screenshots/g378-hallway/`
- **G379**: dump-eye AABB + ST keep (hallway floor)
  - Skip/rank use face AABB-to-eye, not centroid (large floors pass under
    the NPC but have a far centroid)
  - Also keep a CapFace if the dump eye projects into its ST/lightmap bounds
  - Probe `20260813-071312`: CHANGELEVEL_READY; AABB rank n=284 **near=122
    far=162 horiz=65** mind=237 maxd=1448; dump CapFaces **drawn=113**
    (G378 was near=102 drawn=98); `gx_tris=740`
  - Stills: hallway strip + walking scientist; floor underfoot still thin
    (horiz kept in the 284 but not filling the view — likely not the NPC
    room slab, or 5-vert bake AABB)
  - Evidence: `.ai/logs/dolphin-probe-20260813-071312`
  - Stills: `.ai/screenshots/g379-hallway/`
- **G380**: NPC-room floor into CapFaces (G212 stays locked)
  - ST-boost at restream cand + keep-score; `prefer_walls` does not punch
    ST floors during NPC dump; reset restream/rank statics per map
  - Walking `wm->surfaces` / OR NPC-cluster surfbits hung Host_Frame
    (scratch msurface_t). Inject a 4-vert floor quad under the NPC instead
  - Probe `20260813-075444`: CHANGELEVEL_READY; floor quad slot=284 **n=285
    near=123 far=162 horiz=66 mind=60**; dump CapFaces **drawn=114**;
    `gx_tris=740`. Quad AABB is at the dump eye (mind 237→60)
  - World-space mixed-Z floor tris collapse to a 1px edge (GX T&L). Quake
    pitch **+** looks down. Flipper fills constant-Z only (G200 path).
    No-Z-write pads are erased by later draws; NDC trapezoids line.
  - Thin far-edge constant-Z strip from projected pad + dump pitch **20**
    + lean studio Z ALWAYS while `DumpSkipViewmodel` so NPC composites
  - Probe `20260813-163840`: CHANGELEVEL_READY; CapFaces **drawn=115**;
    `gx_tris=740`; pad `y=2..16 x=±56`; cyan ~52k; full scientist +
    hallway strip + cyan underfoot
  - Evidence: `.ai/logs/dolphin-probe-20260813-163840`
  - Stills: `.ai/screenshots/g380-hallway/`

**NEXT**:
- True mixed-Z / textured hallway slab (Flipper constant-Z workaround)
- Real denser CapFaces CPU after sample flush (~33ms retail)
- Optional: G506 HUD sheets (missing=1); dump cyan edge seams
- DumpFrames: prefer `DOLPHIN_TIMEOUT=300`

Rules:
- Force-relink after HLSDK archive changes.
- No Zap/TE map beams from lean StrikeThink.
- No GiveNamedItem(glock/crowbar) in PutInServer under G326 — G103/G120 path.
- Enter `c1a0`/`c1a0d` via changelevel, not cold NEWGAME.
- Dual-hop: `DOLPHIN_CHANGELEVEL` + `DOLPHIN_CHANGELEVEL2` (rebuild disc).
