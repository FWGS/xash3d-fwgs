# Shelved: CPU/GPU overlap at present (-gcoverlap)

Status: not applied. Patch: `gamecube-cpu-gpu-overlap-shelved.patch`
(against master 4e8d6a5).

## Idea

The retail Flipper present is serial: `GC_GXDrawDoneTimed` (CPU waits for
Flipper to finish frame N), `GX_CopyDisp`, `VIDEO_SetNextFramebuffer`,
`VIDEO_WaitVSync`. Frame N+1's game logic only starts once Flipper and the VI
are done with frame N.

The patch queues frame N's `GX_CopyDisp` plus a draw-sync token
(`GC_GXFenceIssue`) and returns. The next present, or any synchronous present
path through `GC_OverlapDrain`, waits for that token, shows the XFB on the next
vsync, and only then copies into the other XFB. A buffer on screen is never
overwritten, at the cost of one frame of latency. `GC_GXOverlapWaitGpu` runs
before the hot texture ring and light-style atlas are rewritten at world start.

## What happened in Dolphin

Route: `DOLPHIN_NEWGAME=1 DOLPHIN_FULLPHYSICS=1`, c0a0e, `-gcprofile -gcoverlap`.

- Version 1 flipped from a VI pre-retrace callback
  (`VIDEO_SetPreRetraceCallback`). The first flip happened, then the guest froze
  one frame later with no watchdog, fence-timeout or DrawDone-timeout output.
- Version 2 (in the patch) has no callback and flips at the next present. The
  guest froze the frame after the first queued copy, same as version 1.
- Gating overlap to presents > 64, past the `-gcfullphysics` early-present EXI
  workaround: it activated at present 65 and froze on the next frame, again
  with no further log.

This fits the existing note in `GC_PresentBuffer` (c5a1): an OSReport issued
while a CopyDisp is still in flight never returns. With overlap every frame
runs CPU work, including OSReport, during the copy. It is unverified whether
this is a Dolphin EXI artefact or would also happen on hardware.

## Also

Dolphin reports the `GX DrawDone wait` stage at ~0 ms because it does not model
Flipper timing, so no gain can be measured there. Try on hardware with
OSReport disabled during gameplay, or find the EXI/OSReport interaction first.
