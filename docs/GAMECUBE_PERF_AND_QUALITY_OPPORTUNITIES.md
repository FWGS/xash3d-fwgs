# Performance and quality: the first levels

Measured on the recorded full ride c0a0 → c0a0e in Dolphin (play disc, real
controller, camera turned every 25 s, `-gcprofile`). Nothing is removed for
speed: every item keeps all NPCs, lightmaps, sky, sounds, titles and the
LOADING overlay. Tools: `scripts/gc-profile-report.py <probe log dir>` (per-map
ms/frame table and stages), recordings in `.ai/playtests/`, the lossless 60 fps
one-frame flicker metric.

All numbers are Dolphin's emulated timing. They rank the work; hardware
numbers come only after the build is complete in the emulator.

## Where the time went (before)

The GPU was idle (`GX DrawDone wait` ~0 ms): the game was CPU bound. A frame
that missed the 16.7 ms vsync waited for the next one, so the frame rate fell
by steps. The world pass and brush entities took 11–18 ms per frame.

| Finding | Measured |
|---|---|
| World texture pool of 3 slots: nearly every visible texture reconverted on the CPU every frame | 30–110 uploads and 11–36 GP fence stalls per frame |
| Draw calls | ~2100 per frame (one per face), 3–18 state sets, 56–119 texture binds |
| Sound reloads from disc mid-frame (budget thrash) | 558 reloads on c0a0d, up to 27 ms/frame in packet parsing |
| Server frame + AI | 0.7–8.7 ms/frame; c0a0 start-up window ~50 ms/frame |
| Heap | 90–470 KB free in game, fragmented; the static map arena is free after load |

## Ranked opportunities

| # | Item | Expected | Status |
|---|---|---|---|
| 1 | Sound reload cooldown | Removes disc stalls | **Done** |
| 2 | Native GX textures in place (no pool, no conversion) | World pass 3× faster | **Done** |
| 3 | Client entity table 256 → 460 in the same memory | Tram and high edicts stop vanishing | **Done** |
| 4 | Forklift model on c0a0 (disc mirror) | Retail content | **Done** |
| 5 | Studio meshes as strips, colour cached per normal | c0a0 36 → 50 fps | **Done** |
| 6 | Studio arena: per-map model sets, no reload leak, full forklift skins, loader, G-Man | Retail cast; stops the arena filling | Next |
| 6b | Intermittent `GX DrawDone` waits of 8–25 ms | Removes run-to-run swings | Open, see below |
| 7 | Mipmaps, then full-size textures above 128 | Sharper close up, less shimmer | Needs ~1 MB per map as RGB565; CMPR or arena placement to be decided |
| 8 | Bilinear/atlas lightmap review, lightstyles via partial updates | Retail lighting | Planned |
| 9 | Dynamic lights, model lighting from the lightmap | Retail lighting | Planned |
| 10 | HUD/text at native 640×480, EFB antialiasing, deflicker | Sharper picture | Planned |
| 11 | Display lists / indexed vertices for static faces | CPU headroom | Planned, by need: the world pass is now 4–6 ms |
| 12 | Prefetch the next map during the ride | Shorter holds (2–5 s) | Planned |
| 13 | Mixing on the DSP (ASND voices) | CPU headroom | Evaluate |
| 14 | Asynchronous flip (CPU/GPU overlap, one frame more latency) | Absorbs GPU waits | Evaluate after item 6 |

## Results

Mean / p95 / worst ms per frame by map, and the first 15 s of each map.

| Map | Before | After items 1+2 | First 15 s before → after |
|---|---|---|---|
| c0a0 | 36.4 mean (27 fps) | 23.1 / 33.6 / 55.3 (43 fps) | 49.7 → 22.1 |
| c0a0a | 22.9 (44 fps) | 18.2 / 18.8 / 63.6 (55 fps) | 27.7 → 21.5 |
| c0a0b | 20.5 (49 fps) | 17.8 / 18.0 / 87.3 (56 fps) | 22.5 → 23.9 |
| c0a0c | 53.8 (19 fps) | 18.1 / 18.2 / 81.8 (55 fps) | 24.7 → 22.8 |
| c0a0d | 83.3 (12 fps) | 18.4 / 18.8 / 77.2 (54 fps) | 23.6 → 22.8 |
| c0a0e | 17.7 (56 fps) | 17.2 / 16.9 / 67.8 (58 fps) | 35.5 → 21.5 |

"Before" means are from the first ride with game sounds; the first-15-s column
compares against the ride with item 1 already in.

Texture uploads per frame 30–110 → 0; fence stalls 11–36 → 0.

Flicker (lossless 60 fps capture, spike px/frame mean, same three segments,
native textures off → on): 175 → 115, 245 → 33, 444 → 214; frames over 1%:
6 → 0. Full 256×256 without mipmaps measured worse on the 30 fps recording, so
sides above 128 are box filtered to 128 (the old path sampled them nearest)
until mipmaps land.

Checks on the after run: ladder 10/10, 40/40 titles, 10/10 announcer lines,
5/5 hops, no allocation failures, audio −18 dB mean, peak 0 dB.
Recording: `.ai/playtests/20261030-055150-menu-newgame-6a93fc553a.mp4`.
`gamecube.cfg` line `nonativetex` turns native textures off for comparison.

### Client entities, forklift, studio (items 3–5)

- `CL_DeltaEntity: invalid newnum` 6456 → 0 on the ride; the tram (edict 306
  on c0a0c) is drawn there now. `cl_entity_t` position history 64 → 16 slots
  (engine and game code, patch 0011); 460 entries in 762 KB, was 256 in 768 KB.
- Forklift loads on c0a0 instead of the next map (47 KB, studio arena).
- c0a0 with the forklifts in: mean 27.6 → 19.9 ms (36 → 50 fps), p95 34.4 → 20.6 ms.
  Recording: `.ai/playtests/20261030-065129-menu-newgame-7f64571e54.mp4`.

Run-to-run noise: identical builds move individual maps by up to 20 ms because
of the intermittent DrawDone wait (item 6), so single-map swings under that
wait are not attributed to a change. Stage tables are compared instead.

### Open: studio arena

The 1 MiB studio arena is bump allocated and only resets when every model in
it is freed, which does not happen during the ride. Barney is reloaded on later
maps and each reload consumes it again: the ride ends at 1020 of 1024 KB, and the
budget counter reads 676 of 640 KB. Missing as a result of the same limits:
`models/loader.mdl` (352 KB, c0a0b–d, over the 220 KB per-file cap) and the
c0a0d G-Man are not confirmed drawn, and the mirror strips the skins embedded in
`forklift.mdl` (194 KB with them, 46 KB without). Needs per-map model sets
with a reset at changelevel.

### Open: GX DrawDone waits

Stretches of 8–25 ms/frame in `present: GX DrawDone wait`, tens of seconds long,
on a different map each run with identical submitted work (same draws, vertices
and binds). Not the cause: host vsync (off in Dolphin and the driver: the wait
moved), the recorder (still there with recording off). Without it every map
holds 53–59 fps. Probably Dolphin/host GPU side, not shown to be game cost; an
asynchronous flip (item 14) would absorb it if it matters.
