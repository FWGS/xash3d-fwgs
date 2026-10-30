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
| 3 | Mipmaps, then full-size textures above 128 | Sharper close up, less shimmer | Next |
| 4 | Remaining spikes (60–90 ms worst blocks), c0a0 AI start-up | Steady 60 | Next |
| 5 | Bilinear/atlas lightmap review, lightstyles via partial updates | Retail lighting | Planned |
| 6 | Dynamic lights, model lighting from the lightmap | Retail lighting | Planned |
| 7 | HUD/text at native 640×480, EFB antialiasing, deflicker | Sharper picture | Planned |
| 8 | Display lists / indexed vertices for static faces | CPU headroom | Planned, by need: the world pass is now 4–6 ms |
| 9 | Prefetch the next map during the ride | Shorter holds (2–5 s) | Planned |
| 10 | Mixing on the DSP (ASND voices) | CPU headroom | Evaluate |

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
