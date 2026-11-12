/*
skin_stream.h - ARAM-backed studio skin streaming policy (platform independent)
Copyright (C) 2026 xash3d-gc contributors

Full-resolution studio skins live in the ARAM asset cache (aram_core.h). MEM1
keeps only each skin's low mip permanently, plus a bounded window holding the
full-resolution skins that were drawn recently. A draw asks for a skin each
frame: it gets the full pixels when they are in the window, otherwise the low
mip while the full skin is promoted from ARAM, at most a fixed number of bytes
per frame so a crowded scene cannot stall on DMA.

Eviction only removes skins that were not requested in the current frame, so a
pointer returned by GC_SkinStreamGet stays valid until the next BeginFrame.
No libogc or engine dependencies: tested on the host (test_gamecube_skin_stream.py).
*/
#ifndef SKIN_STREAM_H
#define SKIN_STREAM_H

#include "aram_core.h"

#define GC_SKIN_MAX        32
#define GC_SKIN_WIN_SEGS   (GC_SKIN_MAX * 2 + 2)

typedef struct
{
	char     name[GC_ARAM_NAME_LEN];
	uint16_t w, h;               /* full resolution */
	uint16_t low_w, low_h;       /* resident low mip */
	uint32_t full_bytes;
	const uint8_t *low;          /* caller memory, valid while registered */
	int      aram_index;         /* asset in the ARAM cache */
	int32_t  win_off;            /* offset in the window, -1 if not resident */
	uint32_t last_frame;
	uint8_t  bpp;                /* bytes per texel of full and low */
	uint8_t  used;
} gc_skin_t;

typedef struct
{
	uint32_t off, size;
	int      skin;               /* skin index, -1 for a free extent */
} gc_skin_seg_t;

typedef struct
{
	gc_aram_cache_t *cache;
	uint8_t *window;
	uint32_t window_bytes;
	uint32_t frame_budget;       /* max bytes promoted per frame */
	uint32_t budget_left;
	uint32_t frame;
	int      nseg;
	gc_skin_seg_t segs[GC_SKIN_WIN_SEGS]; /* sorted, contiguous cover of the window */
	gc_skin_t skins[GC_SKIN_MAX];
	uint32_t promotions, evictions, low_served, full_served, refusals;
} gc_skin_stream_t;

void GC_SkinStreamInit( gc_skin_stream_t *s, gc_aram_cache_t *cache,
	void *window, uint32_t window_bytes, uint32_t frame_budget );
/* Store the full pixels in ARAM and keep only the low mip in MEM1. Registering
 * a name again returns the existing handle. Returns a handle, or -1 when ARAM
 * is full or the table is. */
int  GC_SkinStreamRegister( gc_skin_stream_t *s, const char *name,
	uint16_t w, uint16_t h, const void *full_pixels,
	uint16_t low_w, uint16_t low_h, const void *low_pixels );
/* Same, for formats wider than a byte per texel (RGB565 tiles are 2). The low
 * mip must use the same format; w, h and the returned sizes are in texels. */
int  GC_SkinStreamRegisterBpp( gc_skin_stream_t *s, const char *name,
	uint16_t w, uint16_t h, const void *full_pixels,
	uint16_t low_w, uint16_t low_h, const void *low_pixels, unsigned bpp );
/* Release one skin (its texture was freed): unpins and drops the ARAM asset
 * and gives back its window extent. The handle may be reused afterwards. */
void GC_SkinStreamUnregister( gc_skin_stream_t *s, int handle );
/* Start a frame: resets the promotion budget and releases last frame's pins. */
void GC_SkinStreamBeginFrame( gc_skin_stream_t *s );
/* Pixels to draw this frame. Never NULL for a valid handle. w and h receive the
 * size of the returned image; is_full says whether it is full resolution. */
const uint8_t *GC_SkinStreamGet( gc_skin_stream_t *s, int handle,
	uint16_t *w, uint16_t *h, int *is_full );
/* Drop every skin (map change). Frees the ARAM assets and the window. */
void GC_SkinStreamReset( gc_skin_stream_t *s );
/* Internal consistency check for tests. */
int  GC_SkinStreamValidate( const gc_skin_stream_t *s );

#endif /* SKIN_STREAM_H */
