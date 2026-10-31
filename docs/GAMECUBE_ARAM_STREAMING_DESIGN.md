# ARAM streaming: design

ARAM is the working set. MEM1 is a set of small hot windows in front of it, one
per class of data, each with its own budget. This describes what the code does
today and what comes next, stage by stage. Numbers are Dolphin's: they rank the
work. Dolphin models neither Flipper fill rate nor real disc seek and DMA timing,
so anything that depends on those is marked **unverified**.

## Why windows, not whole-map pools

The first version loaded a whole map's CMPR pack into one MEM1 block.

- A 1.2 MB static pool took that much from the heap before the menu existed:
  fonts failed at boot, zone allocations at c0a0b, the lightmap atlas had no gap,
  and c0a0c hung (9,418 failed allocations).
- A heap pool sized to the largest free block took 832 KB and left 32 KB, so
  client init shut the game down.
- Measured on c0a0, a 631 KB pack left the heap 350 KB worse off than the old
  128-pixel-capped textures, not better. The arithmetic from pack sizes alone had
  promised a saving.

So MEM1 size is a budget chosen from measured headroom, and it does not grow with
the map.

## MEM1 budgets

Memory at c0a0, the tightest point: heap about 5.4 MB used, 22 KB free plus 24 KB
unclaimed, of which the texture pool is 640 KB.

| Class | MEM1 window | State |
|---|---|---|
| World textures | 640 KB pool: table, low mips of every texture, then the hot ring | Done |
| Studio models and skins | 1 MB static arena, per-map sets | Stage 2 makes it a window over ARAM |
| Static world geometry | Display lists, budget set from measurement | Stage 4 |
| Sound | 256 KB cache, 128 KB heap floor | Stage 3 |
| Lightmaps | 800 KB atlas in BSP scratch | Stays: the GPU samples it every frame. Style samples are already in ARAM |

## ARAM layout

16 MiB in all, split with `GC_AramReserve`. `AR_Init` now gets 8 blocks; it had
4, and a fifth reservation would have overrun the array.

| Region | Size | Contents |
|---|---|---|
| Audio use | Reserved by the sound path | Unchanged |
| Transition saves | 4 MB | The changelevel save bank |
| Light styles | 1 MB | Per-style lightmap samples |
| Sentences | ~52 KB | `sentences.txt` |
| Texture store | 6 MB | CMPR chains by name and content tag |
| Free | ~5 MB | Models, sound banks and next-map prefetch, from stages 2-5 |

All unique world textures from c0a0 to c1a0 come to about 2.8 MB, so 6 MB holds the
whole intro and first chapter.

## Stage 1: textures (implemented)

Build: `build_world_texture_packs` in `scripts/build-gamecube-disc.py` writes
`media/wt_<map>.gct`. Each texture is CMPR with principal-axis endpoints, 1-bit
alpha for `{` textures, sides rounded up to powers of two (`GX_REPEAT` needs them,
and UVs are normalised so nothing moves), and a mip chain down to 8 texels. Colours
come from the 24-bit palette, not the soft renderer's 16-bit pixels. Each entry has
a 16-bit content tag, so two maps' textures of the same name but different pixels
stay distinct. Water and sky keep the engine's own path.

Load, `Mod_GCWorldPackLoad` in `engine/common/mod_bmodel.c`:

1. Read the table into the pool.
2. For each texture, find it in the ARAM store by name, tag and size. On a miss,
   read it from disc through the top of the pool and write it to ARAM.
3. DMA the low mips from ARAM into the pool. A texture over 64 texels a side
   skips level 0; smaller ones come whole.
4. What's left of the pool is the hot window.

Integration: `Mod_LoadTextureData` asks the pack before the WAD path.
`R_GCPackedTexture` creates an `image_t` with no soft pixels, so
`GL_FreeTexture` has nothing to free. `R_GXBindTexnum` in `ref/gx/r_gx_world.c`
sends packed images to `R_GXPackedTexture`.

Streaming: the first bind of a split texture queues it, and it draws from its low
mips until the chain lands, so a slow DMA is a soft picture for a moment, never a
stall. `R_GXHotService` runs between frames, when the GPU has finished last frame's
draws. It moves up to 96 KB per frame into the ring, then calls
`GX_InvalidateTexAll`, because a chain may land where another one was.

Eviction today: a ring with hysteresis. A chain unused for 90 frames can be
overwritten; one in use is stepped over; if nothing can go, the request waits a
frame and the low mips keep drawing.

## Priority, next for stage 1

The queue is first come, first served. To add:

- **Score:** screen-space size over distance, from the face's extent and distance
  during the world pass, kept per texture as the frame's maximum. Requests go by
  score and eviction takes the lowest.
- **Prediction:** before a face is drawn, walk the PVS of the leaf ahead of the
  player, using velocity and view direction, and request those textures with a
  low score.
- **Next map:** see stage 5.

## Stage 2: models

`Mod_GCStudioBssAlloc` is a bump arena released per map (`Mod_GCReleaseStudioArena`)
and models are admitted by name in `Mod_GCStudioNameAllowed`, because allowing all
of them flooded it. c0a0 sits at 993 of 1,024 KB when weapon skins are added.

Plan: every model of the map goes to ARAM at precache. The arena becomes a window
of whole models with a per-model pin count from the entities using them in the
current PVS. The studio header pointer must stay valid while an entity holds it,
so models become handles resolved at draw time, with eviction only between frames.
The server only needs bones and sequences, which stay in MEM1 as today's
`keep_skins` split shows is possible.

Sequence groups (`gc_seqgroup_slot`) and skins are the first candidates, because
meshes are needed on every draw and skins can use the low-mip idea.

## Stage 3: sound

`GC_SFX_MENU_HEAP_FLOOR` and the 256 KB budget refuse sounds once full: the log
shows `gameplay sfx budget refused` for `barney/c1a0_ba_late`, doors and the
loader. Plan: hottest samples stay in the MEM1 cache, the rest in ARAM. Long speech
streams in small chunks, the way `S_GCStreamVoice` already streams the announcer
from disc.

## Stage 4: overlapped DMA

`GC_AramTransfer` spins on `AR_GetDMAStatus`, so today's streaming costs CPU while
it copies. Plan: a queue, started between frames and polled during the frame, with
a texture's state moving from queued to in-flight to resident. An image is only
bound hot once its DMA is finished. Eviction must skip in-flight targets. Disc reads
go on a low-priority thread into ARAM, so the CPU keeps working while the disc does.

## Stage 5: next-map prefetch

During the ride, the thread reads the next pack, named by the map's
`trigger_changelevel`, into the texture store, then the BSP lumps and models into
the free ARAM. The changelevel then finds everything in ARAM. How much of the 2-5 s
hold this removes depends on the per-hop log: entity spawn, the lightmap bake and
the save take time that no prefetch helps.

## Stage 6: display lists, packing, TEV

- Display lists per texture and leaf for static faces, in the budget above,
  themselves in ARAM when out of the PVS. Rebuilt only when a lightmap page moves.
- Packed positions: int16 with a per-model or per-leaf scale, and quantised UVs.
- Hardware matrix skinning: the 10 position matrices, with meshes split by bone
  palette, in place of the loop in `R_StudioDrawPoints`.
- TEV: the world is two stages today (texture, lightmap). Audit anything above.

## Gekko and DMA rules

- Everything that DMAs is 32-byte aligned and a multiple of 32 bytes. Pack entries
  are padded to guarantee it.
- `DCFlushRange` before MEM1 to ARAM. `DCInvalidateRange` before and after ARAM to
  MEM1, so no dirty line lands on the new data.
- Texture memory changed under the GPU needs `GX_InvalidateTexAll` or
  `GX_InvalidateTexRegion`.
- Never free or overwrite texture memory while the GPU may read it: service runs
  between frames, after the present's `GX_DrawDone`.

## Stats

`-gcprofile` logs per map load:
`world textures <map> aram=<n>/<KB> disc=<n>/<KB> low=<KB> hot=<KB> in <ms>`, and
every 64 frames:
`hot window <KB> resident= queued= uploads= bytes= evictions= deferred=`.

## Not doing

- **Virtual texturing:** per-texture mip streaming gets the benefit for stock
  content at a fraction of the complexity.
- **Deferred rendering:** no gain on GoldSrc's lighting model.
- **Software occlusion:** the BSP's PVS is already there and free.

## What Dolphin cannot validate

- Trilinear filtering, 2× anisotropy and alpha-test fill cost.
- Real DMA and disc speeds: the size of the streaming budget per frame, and every
  hold length.
- Whether `GX_InvalidateTexAll` on streaming frames shows up on Flipper.
