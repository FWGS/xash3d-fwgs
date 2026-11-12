/*
skin_stream.c - ARAM-backed studio skin streaming policy (platform independent)
Copyright (C) 2026 xash3d-gc contributors
*/
#include <string.h>
#include "skin_stream.h"

#define WIN_ALIGN 32u

static uint32_t SS_Align( uint32_t v )
{
	return ( v + WIN_ALIGN - 1u ) & ~( WIN_ALIGN - 1u );
}

static void SS_ResetWindow( gc_skin_stream_t *s )
{
	s->nseg = 1;
	s->segs[0].off = 0;
	s->segs[0].size = s->window_bytes & ~( WIN_ALIGN - 1u );
	s->segs[0].skin = -1;
}

void GC_SkinStreamInit( gc_skin_stream_t *s, gc_aram_cache_t *cache,
	void *window, uint32_t window_bytes, uint32_t frame_budget )
{
	int i;

	memset( s, 0, sizeof( *s ));
	s->cache = cache;
	s->window = (uint8_t *)window;
	s->window_bytes = window_bytes;
	s->frame_budget = frame_budget;
	s->budget_left = frame_budget;
	for( i = 0; i < GC_SKIN_MAX; i++ )
		s->skins[i].win_off = -1;
	SS_ResetWindow( s );
}

static void SS_MergeFree( gc_skin_stream_t *s )
{
	int i = 0;

	while( i + 1 < s->nseg )
	{
		if( s->segs[i].skin < 0 && s->segs[i + 1].skin < 0 )
		{
			s->segs[i].size += s->segs[i + 1].size;
			memmove( &s->segs[i + 1], &s->segs[i + 2],
				(size_t)( s->nseg - i - 2 ) * sizeof( s->segs[0] ));
			s->nseg--;
		}
		else
			i++;
	}
}

static void SS_Evict( gc_skin_stream_t *s, int skin )
{
	int i;

	for( i = 0; i < s->nseg; i++ )
	{
		if( s->segs[i].skin == skin )
		{
			s->segs[i].skin = -1;
			break;
		}
	}
	s->skins[skin].win_off = -1;
	s->evictions++;
	SS_MergeFree( s );
}

/* Find a free extent of at least size bytes; returns the segment index or -1. */
static int SS_FindFree( const gc_skin_stream_t *s, uint32_t size )
{
	int i;

	for( i = 0; i < s->nseg; i++ )
		if( s->segs[i].skin < 0 && s->segs[i].size >= size )
			return i;
	return -1;
}

/* Evict the least recently used skin not requested this frame. 0 if none. */
static int SS_EvictLRU( gc_skin_stream_t *s )
{
	int i, best = -1;

	for( i = 0; i < GC_SKIN_MAX; i++ )
	{
		const gc_skin_t *k = &s->skins[i];

		if( !k->used || k->win_off < 0 || k->last_frame == s->frame )
			continue;
		if( best < 0 || k->last_frame < s->skins[best].last_frame )
			best = i;
	}
	if( best < 0 )
		return 0;
	SS_Evict( s, best );
	return 1;
}

static int SS_Place( gc_skin_stream_t *s, int skin, uint32_t size )
{
	int i = SS_FindFree( s, size );

	while( i < 0 )
	{
		if( !SS_EvictLRU( s ))
			return -1;
		i = SS_FindFree( s, size );
	}
	if( s->segs[i].size > size )
	{
		if( s->nseg >= GC_SKIN_WIN_SEGS )
			return -1;
		memmove( &s->segs[i + 2], &s->segs[i + 1],
			(size_t)( s->nseg - i - 1 ) * sizeof( s->segs[0] ));
		s->segs[i + 1].off = s->segs[i].off + size;
		s->segs[i + 1].size = s->segs[i].size - size;
		s->segs[i + 1].skin = -1;
		s->segs[i].size = size;
		s->nseg++;
	}
	s->segs[i].skin = skin;
	return (int)s->segs[i].off;
}

int GC_SkinStreamRegister( gc_skin_stream_t *s, const char *name,
	uint16_t w, uint16_t h, const void *full_pixels,
	uint16_t low_w, uint16_t low_h, const void *low_pixels )
{
	return GC_SkinStreamRegisterBpp( s, name, w, h, full_pixels, low_w, low_h, low_pixels, 1 );
}

int GC_SkinStreamRegisterBpp( gc_skin_stream_t *s, const char *name,
	uint16_t w, uint16_t h, const void *full_pixels,
	uint16_t low_w, uint16_t low_h, const void *low_pixels, unsigned bpp )
{
	char key[GC_ARAM_NAME_LEN];
	gc_skin_t *k = NULL;
	int i, idx = -1;

	if( !name || !name[0] || !full_pixels || !low_pixels || !w || !h || !low_w || !low_h
		|| bpp < 1 || bpp > 4 )
		return -1;
	for( i = 0; i < GC_SKIN_MAX; i++ )
	{
		if( s->skins[i].used && !strncmp( s->skins[i].name, name, sizeof( s->skins[i].name ) - 1 ))
			return i;
		if( !s->skins[i].used && idx < 0 )
			idx = i;
	}
	if( idx < 0 )
		return -1;
	k = &s->skins[idx];
	memset( k, 0, sizeof( *k ));
	strncpy( k->name, name, sizeof( k->name ) - 1 );
	k->full_bytes = (uint32_t)w * h * bpp;
	k->bpp = (uint8_t)bpp;
	if( SS_Align( k->full_bytes ) > s->window_bytes )
		return -1;
	strncpy( key, name, sizeof( key ) - 1 );
	key[sizeof( key ) - 1] = '\0';
	k->aram_index = GC_AramCacheStore( s->cache, key, full_pixels, k->full_bytes );
	if( k->aram_index < 0 )
	{
		memset( k, 0, sizeof( *k ));
		k->win_off = -1;
		return -1;
	}
	GC_AramCachePin( s->cache, k->aram_index );
	k->w = w;
	k->h = h;
	k->low_w = low_w;
	k->low_h = low_h;
	k->low = (const uint8_t *)low_pixels;
	k->win_off = -1;
	k->used = 1;
	return idx;
}

void GC_SkinStreamUnregister( gc_skin_stream_t *s, int handle )
{
	gc_skin_t *k;

	if( handle < 0 || handle >= GC_SKIN_MAX || !s->skins[handle].used )
		return;
	k = &s->skins[handle];
	if( k->win_off >= 0 )
	{
		SS_Evict( s, handle );
		s->evictions--;   /* a release is not an eviction */
	}
	GC_AramCacheUnpin( s->cache, k->aram_index );
	GC_AramCacheEvict( s->cache, k->name );
	memset( k, 0, sizeof( *k ));
	k->win_off = -1;
}

void GC_SkinStreamBeginFrame( gc_skin_stream_t *s )
{
	s->frame++;
	s->budget_left = s->frame_budget;
}

const uint8_t *GC_SkinStreamGet( gc_skin_stream_t *s, int handle,
	uint16_t *w, uint16_t *h, int *is_full )
{
	gc_skin_t *k;
	uint32_t need;
	int off;

	if( handle < 0 || handle >= GC_SKIN_MAX || !s->skins[handle].used )
		return NULL;
	k = &s->skins[handle];
	if( k->win_off >= 0 )
	{
		k->last_frame = s->frame;
		s->full_served++;
		goto full;
	}
	need = SS_Align( k->full_bytes );
	if( need > s->budget_left )
		goto low;
	off = SS_Place( s, handle, need );
	if( off < 0 )
	{
		s->refusals++;
		goto low;
	}
	if( !GC_AramCacheRead( s->cache, k->aram_index, 0, s->window + off, k->full_bytes ))
	{
		/* Read failed: give the extent back and serve the low mip. */
		SS_Evict( s, handle );
		s->evictions--;
		s->refusals++;
		goto low;
	}
	k->win_off = off;
	k->last_frame = s->frame;
	s->budget_left -= need;
	s->promotions++;
	s->full_served++;
full:
	if( w ) *w = k->w;
	if( h ) *h = k->h;
	if( is_full ) *is_full = 1;
	return s->window + k->win_off;
low:
	s->low_served++;
	if( w ) *w = k->low_w;
	if( h ) *h = k->low_h;
	if( is_full ) *is_full = 0;
	return k->low;
}

void GC_SkinStreamReset( gc_skin_stream_t *s )
{
	int i;

	for( i = 0; i < GC_SKIN_MAX; i++ )
	{
		if( s->skins[i].used )
		{
			GC_AramCacheUnpin( s->cache, s->skins[i].aram_index );
			GC_AramCacheEvict( s->cache, s->skins[i].name );
		}
		memset( &s->skins[i], 0, sizeof( s->skins[i] ));
		s->skins[i].win_off = -1;
	}
	SS_ResetWindow( s );
	s->budget_left = s->frame_budget;
}

int GC_SkinStreamValidate( const gc_skin_stream_t *s )
{
	uint32_t pos = 0;
	int i;

	for( i = 0; i < s->nseg; i++ )
	{
		if( s->segs[i].off != pos || !s->segs[i].size )
			return 0;
		pos += s->segs[i].size;
		if( s->segs[i].skin >= 0 )
		{
			const gc_skin_t *k = &s->skins[s->segs[i].skin];

			if( !k->used || k->win_off != (int32_t)s->segs[i].off )
				return 0;
		}
		else if( i + 1 < s->nseg && s->segs[i + 1].skin < 0 )
			return 0; /* adjacent free extents must be merged */
	}
	return pos == ( s->window_bytes & ~( WIN_ALIGN - 1u ));
}
