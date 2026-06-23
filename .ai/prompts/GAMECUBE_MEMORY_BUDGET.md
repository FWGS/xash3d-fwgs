# GameCube Memory Budget Notes

Public homebrew context only. Do not treat ARAM as ordinary heap space.

## Hard limits

| Region | Size | Use in this port |
|--------|------|------------------|
| MEM1 (main) | 24 MiB | `malloc`, zone pools, HLSDK, BSP, renderer, filesystem scratch |
| ARAM | 16 MiB | Audio DMA buffers, future streaming assets (explicit alloc only) |

Reserve headroom for libogc, GX FIFO, XFBs, and stack. Treat ~20 MiB as the
practical gameplay ceiling until hardware profiling says otherwise.

## Measured zone high-water (Dolphin, c0a0e smoke)

Source: `.ai/logs/dolphin-probe-20260623-010238/stderr.log` (`GC_MemSample`).

| Stage | Total | Delta | Notes |
|-------|-------|-------|-------|
| filesystem | 68.9 KiB | 68.9 KiB | Platform mount + DLL registration |
| searchpaths | 73.1 KiB | 4.2 KiB | ZIP/WAD index, game hierarchy |
| server progs | 1.76 MiB | 1.69 MiB | Static HLSDK server + edict tables |
| server init | 1.76 MiB | 0 | SV_Init complete |
| textures | 1.79 MiB | 26.2 KiB | Renderer image tables (pre-map) |
| models | 1.79 MiB | 0 | Studio registration (pre-map) |
| client init | 4.12 MiB | 2.34 MiB | Client HLSDK, sound tables, video |
| bsp load | 6.44 MiB | 2.32 MiB | c0a0e resident + parse peak |

Peak scratch during BSP load (same probe):

- ZIP read buffer for `maps/c0a0e.bsp`: ~1.44 MiB (freed after `Mod_LoadBrushModel`)
- Software surface cache: 8 KiB (`GC_SURFACE_CACHE_DEFAULT`, capped at 64 KiB)
- GX software buffer 320x240 RGB565: ~150 KiB (trimmed on map load via `-gcmap`)

## Category budget (target allocation)

| Category | Build-time cap | Runtime notes |
|----------|----------------|---------------|
| Engine zone pools | — | Tracked by `Mem_TotalRealSize()` / `memlist` |
| HLSDK server/client | ~2–3 MiB static | Dominated by progs init; `-gcmap` trims client entities to 64 |
| Renderer GX | ~512 KiB–1 MiB | Surface cache bounded; duplicate sw blit buffers trimmed on map load |
| Filesystem | ~100 KiB–2 MiB peak | ZIP indices + one archive read buffer per large load |
| BSP / world model | map-dependent | c0a0e ~2.3 MiB delta; full HL maps will be larger |
| Studio / sprite models | map-dependent | `-gcmap` uses stubs; full game needs streaming or caps |
| Sound precache | ~16 KiB ring + ASND | Real backend uses 2048-sample stereo DMA buffer; `-gcnullaudio` for triage |
| Save / config | minimal | No MC saves yet; avoid large host write buffers on disc boot |
| Scratch | minimize | Prefer free-after-load (BSP buffer), in-place clipnodes, lazy caches |

Compile-time limits (`--low-memory-mode=2`): `MAX_MODELS` 512, `MAX_SOUNDS` 512,
`MAX_STATIC_ENTITIES` 32, `MAX_VISIBLE_PACKET` 128, `MAX_DECALS` 256.

## Bounded GameCube modes (implemented)

### Software surface cache

- Default: `sw_surfcacheoverride` = 8192 on GameCube builds.
- Hard cap: 65536 bytes (`GC_SURFACE_CACHE_MAX` in `ref/gx/r_local.h`).
- Smoke boot (`-gcmap`) may lazy-alloc via `malloc()` when zone pool is tight.
- Replaces the old smoke-only `-sw_surfcacheoverride 131072` argv hack.

### Map-load video trim (`-gcmap`)

- Frees duplicate sw blit `vid.buffer` / `d_pzbuffer` and GX `gc.buffer` before BSP
  parse; restores after activate.

### Client entity ring (`-gcmap`)

- `cls.num_client_entities` = 64 for single-player smoke (was full backup size).

## ARAM candidates (not malloc)

Plan explicit ARAM use for future subsystems; do not route these through zone:

| Candidate | Approx size | Priority |
|-----------|-------------|----------|
| DSP/AIX streaming audio buffers | 128 KiB–1 MiB | G26 |
| Music / ambient stream double-buffer | map-dependent | G27 |
| Large texture upload staging | optional | After HUD stable |
| THP / cinematic decode | deferred | Out of scope until content policy set |

GX FIFO (256 KiB static in `vid_gamecube.c`) and XFB allocations stay in MEM1
today; revisit only with hardware measurement.

## Failure handling

- OOM in zone: `GC_MemFail()` logs pool name, size, map, file:line before fatal.
- Stage samples: `Xash3D GameCube: mem stage=…` via `GC_MemSample()`.
- Prefer one bounded reduction with probe evidence over broad cache rewrites.

## Next reductions (ordered)

1. Stream or mmap stored ZIP entries instead of full-file buffer for BSP/models.
2. Promote `-gcmap` video trim to a general `-gclowmem` gameplay mode (G24).
3. Move PCM/stream buffers to ARAM when audio backend lands (G26–G27).
4. Cap studio texture residency and particle pools with explicit quality tiers.
