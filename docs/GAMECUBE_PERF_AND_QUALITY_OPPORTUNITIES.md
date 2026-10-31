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
| 6 | Studio arena: per-map model sets, no reload leak, full forklift skins, loader, G-Man | Retail cast; stops the arena filling | **Done** |
| 6b | Intermittent `GX DrawDone` waits of 8–25 ms | Removes run-to-run swings | Open, see below |
| 7 | Mipmaps, then full-size textures above 128 | Sharper close up, less shimmer | **Done**, as CMPR packs in ARAM with a MEM1 hot window: docs/GAMECUBE_ARAM_STREAMING_DESIGN.md |
| 8 | Lightstyles via partial atlas updates, samples in ARAM (lightmaps were already bilinear) | Retail lighting | **Done** |
| 9 | Dynamic lights, model lighting from the lightmap | Retail lighting | Planned |
| 10 | EFB 24-bit colour and depth (was RGB565/Z16). No cost measured; banding and depth precision by design, not measured from the compressed recording | Quality | **Done** |
| 10b | Text: already the full 256-wide font at 1:1 texels, magnified 2× by request; no gain available without a new font | - | Skipped |
| 10c | EFB antialiasing | Sharper picture | Not viable: Flipper MSAA needs RGB565_Z16 and a half-height EFB, giving up 24-bit colour and depth |
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
Recording: `.ai/playtests/20261030-153838-menu-newgame-ce647e2767.mp4`.
`gamecube.cfg` line `nonativetex` turns native textures off for comparison.

### Client entities, forklift, studio (items 3–5)

- `CL_DeltaEntity: invalid newnum` 6456 → 0 on the ride; the tram (edict 306
  on c0a0c) is drawn there now. `cl_entity_t` position history 64 → 16 slots
  (engine and game code, patch 0011); 460 entries in 762 KB, was 256 in 768 KB.
- Forklift loads on c0a0 instead of the next map (47 KB, studio arena).
- c0a0 with the forklifts in: mean 27.6 → 19.9 ms (36 → 50 fps), p95 34.4 → 20.6 ms.
  Recording: `.ai/playtests/20261030-153838-menu-newgame-ce647e2767.mp4`.

Run-to-run noise: identical builds move individual maps by up to 20 ms because
of the intermittent DrawDone wait (item 6), so single-map swings under that
wait are not attributed to a change. Stage tables are compared instead.

### Studio arena and the full cast (item 6)

The arena is released at each changelevel and each map loads its own set. Peak
on the ride 925 KB of 1024, per map (it used to climb to 1020 and never free).
The forklift keeps its embedded skins, and the loader (c0a0b–d) and G-Man (c0a0d)
load; `monster_gman` had been inhibited. Each model logs `studio skinned` on
its first skinned draw: scientist, barney, forklift, loader, gman.
`Gibbed monster is thinking` 3273 → 0.

c0a0c/d: 55 → 42 fps with the loader in view (p95 34 ms: the frame just misses
vsync). Studio stage 3.6 ms; this is the added content, and the next target.
Recording: `.ai/playtests/20261030-153838-menu-newgame-ce647e2767.mp4`.

The filtered classes (`cycler`, `env_sprite`, `env_explosion`, `infodecal`,
`speaker`, weapons, ammo, items) now spawn on the menu route. Admitting every
model by default failed: precaches flooded the arena and squeezed out the cast,
so models are still named in `Mod_GCStudioNameAllowed`.

### Light styles (item 8)

The intro maps have no flickering styles; they use switchable ones (32+), and
the bake only held style 0, so switched lights were missing. Every style's
samples now stay in ARAM (1 MiB reserved; 380 KB on c0a0c, 1399 faces) and faces
are recomposed into the atlas only when a style changes, at most 256 faces a
frame. No MEM1 cost beyond a small face table. The 3-slot world texture pool,
idle since native textures, is 1 slot: 64 KB back, which the boot needed (2 KB
of new static data had tipped HUD init at c0a0 into a silent failure, heap free
there is about 50 KB). Recording:
`.ai/playtests/20261030-153838-menu-newgame-ce647e2767.mp4`.

Lightning beams now draw: the renderer's model lookup was bounded by a client
count this route never fills, so every `env_beam` was dropped
(`G291 Flipper EFX tris=504` on c0a0c).

### Studio frustum culling (item 5b)

`R_CullModel` was a stub here: every model in the PVS was skinned and drawn,
even behind the view. A sphere test against the view frustum, with a margin,
puts the studio stage at c0a0 6.3 → 1.5, c0a0c 3.5 → 0.9, c0a0d 3.0 → 1.4 ms/frame.
Mean c0a0 19.2 ms (52 fps), c0a0c 20.6 (49), c0a0d 20.2 (50), the rest 17–18.

### Flicker after items 6 and 8

Lossless 60 fps segments on the probe disc: 104 and 257 spike px/frame on the
two comparable segments (115 and 214 before; frames over 1%: 0). The third
(33 → 294) is not comparable: the segment is time based and now lands in the
bright c0a0 warehouse rather than a dark tunnel; its heat map follows moving
edges of the rails and walls. A same-scene comparison is still to do.

Same-scene check on one build, native textures off → on: 121 → 109, 511 → 233,
304 → 467 spike px/frame. The runs still do not line up frame for frame (the
faster build sees more distinct frames), so this is mixed, not a clear win: the
earlier 175/245/444 → 115/33/214 overstated it.

### NPC speech and heap

Sentence groups run from GameDLLInit (patch 0012). The engine's table is in ARAM
with 16-bit offsets. Video buffers (430 KB of static memory unused without
startup videos) are allocated on first use: unclaimed heap at c0a0 48 → 444 KB,
which pays for the speech and a 256 KB sound budget. The WAV loader's 16 KB
limit, which had been silently refusing the tram's sounds, now matches 48 KB.

### c0a0c/d pacing

Both maps hold 17 ms except while the loader is in view: it costs 6.9 ms and
pushes work just over one vsync, so those stretches run at 33 ms.

### World textures: CMPR, ARAM store, hot window (item 7)

Retail-size textures with mip chains, trilinear filtering, 2× anisotropy and
cut-out alpha, in a fixed 640 KB of MEM1, with no conversion at load. Design:
docs/GAMECUBE_ARAM_STREAMING_DESIGN.md.

Clean ride: ladder 10/10, 40/40 titles, 10/10 announcer lines, 5/5 hops, 0 failed
allocations. Recording: `.ai/playtests/20261030-214054-menu-newgame-85d2cafd38.mp4`.

| Map | Mean ms | p95 ms | From ARAM | From disc | Texture load |
|---|---|---|---|---|---|
| c0a0 | 19.0 | 46.6 | 0 | 629 KB | 303 ms |
| c0a0a | 17.9 | 18.7 | 435 KB | 249 KB | 168 ms |
| c0a0b | 17.4 | 17.5 | 413 KB | 640 KB | 368 ms |
| c0a0c | 19.5 | 31.0 | 443 KB | 236 KB | 168 ms |
| c0a0d | 19.1 | 33.4 | 579 KB | 160 KB | 124 ms |
| c0a0e | 17.0 | 16.9 | 413 KB | 137 KB | 91 ms |

Before, same maps, mean ms: 19.2, 19.0, 17.5, 20.5, 21.3, 17.1. c0a0 p95 was 19.8
and is now 46.6 in this ride (38.5 in the one before it): open, not yet explained.

Tried and rejected:

- 1.2 MB static pool: starved boot and hung at c0a0c (9,418 failed allocations).
- Heap pool as large as the largest block (832 KB): left 32 KB and client init
  shut the game down.
- Whole packs in MEM1: a 631 KB pack left c0a0's heap 350 KB worse off than the
  capped textures, so the hot window took its place.

Dolphin does not model the fill cost of trilinear, anisotropy or the alpha test.

### Sound: ARAM store, files outside the heap

Evicting harder made it worse: the budget is smaller than the sounds in use, so
each eviction became a disc read (128 evictions, 27 failed allocations). Sounds
now stay in ARAM once loaded, so an eviction is a DMA, idle sounds go before a
changelevel so model skin files have room, and sound files read into the model
skin scratch, which is idle during play: a 38 KB file buffer was failing with
300 KB free on a fragmented heap. Budget 256 → 320 KB.

Same route into c1a0, one ride each, so these vary:

| | Refused | Failed allocations | Evictions | From ARAM |
|---|---|---|---|---|
| Before | ~50 | 12 | | 0 |
| After | 1 | 0 | 74 | 37 |

`gmanT.mdl` (144 KB) fits no block at a changelevel and a static scratch 16 KB
larger breaks boot, even with the same memory taken off the texture pool: it
borrows the texture hot window while it is read. All 5 of the cast skinned.

### Open: GX DrawDone waits

Stretches of 8–25 ms/frame in `present: GX DrawDone wait`, tens of seconds long,
on a different map each run with identical submitted work (same draws, vertices
and binds). Not the cause: host vsync (off in Dolphin and the driver: the wait
moved), the recorder (still there with recording off). Without it every map
holds 53–59 fps. Not seen in the last dozen rides. Probably Dolphin/host GPU side, not shown to be game cost; an
asynchronous flip (item 14) would absorb it if it matters.
