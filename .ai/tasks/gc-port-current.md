Auto-port task for Xash3D GameCube
=================================

Current goal:
Tighten G36 frame budget / restore fuller entity spawn when MEM allows.
Match retail visuals without cutting fill/spawn.

**DONE (2026-08-10/11)**:
- Campaign `CHANGELEVEL_READY` through Nihilanth (`c4a3→c5a1`)
- **G341–G355**: tram/AM vis + denser `c1a0d` tip-safe hop
- **G356**: campaign dual-hop harness `c0a0e→c1a0→c1a0d`
  - Disc `changelevel2` / `-gcchangelevel2` argv
  - Deferred hop2 until after `MAP_READY` (nested G95 Prepare hung)
  - Probe: G68 `c0a0e→c1a0` then `c1a0→c1a0d`; CHANGELEVEL_READY
  - Evidence: `.ai/logs/dolphin-probe-g356-dual-c1a0d`
- **G357**: dual-hop CapFaces on final denser dest
  - Block denser G335 reverse-hop when already on probe changelevel dest
    (`GC_IsProbeChangelevelDest` → mark `gc_g335_cl_queued`)
  - Bounded Flipper CapFaces sample after G335 on denser dests
  - Probe: CHANGELEVEL_READY; no `c1a0d→c1a0` reverse; `G357 CapFaces sample ok
    map=c1a0d live=75`
  - Evidence: `.ai/logs/dolphin-probe-g357-dual-c1a0d`
- **G358**: report CapFaces `drawn=` on dual-hop denser dest
  - `GC_NoteCapFacesDrawn` / `GC_LastCapFacesDrawn` (framecount>3 suppresses begin/end)
  - Probe: `G358 CapFaces sample ok map=c1a0d live=75 drawn=280`, CHANGELEVEL_READY
  - Evidence: `.ai/logs/dolphin-probe-g358-dual-c1a0d`
- **G359**: restore textured DumpFrames stills (tram source)
  - Soft latch on non-denser source only; skip denser AM + probe changelevel dests
  - DumpFrames host: `DisableCopyToVRAM=True`; default video **OpenGL** when dumping
    (Null TGAs all-black)
  - Probe: NEWGAME_READY `c0a0e`; G191 soft dump; stills `framedump_31–33` uniq≈218
  - Evidence: `.ai/logs/dolphin-probe-g359-dumpframes-c0a0e`

**NEXT**:
- DumpFrames stills on denser changelevel dest (`c1a0d`) without soft-latch hang
- Optional: raise CapFaces live pool on `c1a0d` (Capture n=75 vs budget 192)
- Optional: CapFaces begin/end once-per-map (not only drawn stash)

Rules:
- Force-relink after HLSDK archive changes.
- No Zap/TE map beams from lean StrikeThink.
- No GiveNamedItem(glock/crowbar) in PutInServer under G326 — G103/G120 path.
- Enter `c1a0`/`c1a0d` via changelevel, not cold NEWGAME.
- Dual-hop: `DOLPHIN_CHANGELEVEL` + `DOLPHIN_CHANGELEVEL2` (rebuild disc).
