/*
aram_core.c - GameCube ARAM allocator and asset cache (platform independent)
Copyright (C) 2026 xash3d-gc contributors

libogc's AR_Alloc is a stack with no bounds checks: every caller pushes onto
one array of block lengths and AR_Free pops whichever block is on top. The
engine had more reservations than array entries and more requested bytes
than ARAM, and two owners popped blocks they did not own. This allocator
takes the whole pool from AR_Alloc once and hands out ranges itself, with
real frees, so streamed data can come and go without disturbing the
session regions.
*/
#include "aram_core.h"
#include <string.h>

static uint32_t GC_AramAlignUp( uint32_t value )
{
	return ( value + ( GC_ARAM_ALIGN - 1u )) & ~( GC_ARAM_ALIGN - 1u );
}

static void GC_AramSetTag( gc_aram_seg_t *seg, const char *tag )
{
	size_t i = 0;

	memset( seg->tag, 0, sizeof( seg->tag ));
	if( !tag )
		return;
	for( ; tag[i] && i < sizeof( seg->tag ) - 1; i++ )
		seg->tag[i] = tag[i];
}

void GC_AramHeapInit( gc_aram_heap_t *heap, uint32_t base, uint32_t size )
{
	uint32_t start = GC_AramAlignUp( base );
	uint32_t end = ( base + size ) & ~( GC_ARAM_ALIGN - 1u );

	memset( heap, 0, sizeof( *heap ));
	if( !start || end <= start || base + size < base )
		return;
	heap->base = start;
	heap->end = end;
	heap->count = 1;
	heap->segs[0].offset = start;
	heap->segs[0].size = end - start;
}

static int GC_AramHeapInsert( gc_aram_heap_t *heap, int at, const gc_aram_seg_t *seg )
{
	if( heap->count >= GC_ARAM_MAX_SEGS )
		return 0;
	memmove( &heap->segs[at + 1], &heap->segs[at], (size_t)( heap->count - at ) * sizeof( heap->segs[0] ));
	heap->segs[at] = *seg;
	heap->count++;
	return 1;
}

static void GC_AramHeapRemove( gc_aram_heap_t *heap, int at )
{
	memmove( &heap->segs[at], &heap->segs[at + 1], (size_t)( heap->count - at - 1 ) * sizeof( heap->segs[0] ));
	heap->count--;
}

static uint32_t GC_AramHeapUsedBytes( const gc_aram_heap_t *heap )
{
	return heap->session_bytes + heap->stream_bytes;
}

uint32_t GC_AramHeapAlloc( gc_aram_heap_t *heap, uint32_t size, gc_aram_class_t cls, const char *tag )
{
	uint32_t need;
	int i, pick = -1;

	if( !heap || !heap->count || !size )
		return 0;
	need = GC_AramAlignUp( size );
	if( need < size )
	{
		heap->failures++;
		return 0;
	}

	if( cls == GC_ARAM_SESSION )
	{
		for( i = 0; i < heap->count && pick < 0; i++ )
		{
			if( !heap->segs[i].used && heap->segs[i].size >= need )
				pick = i;
		}
	}
	else
	{
		for( i = heap->count - 1; i >= 0 && pick < 0; i-- )
		{
			if( !heap->segs[i].used && heap->segs[i].size >= need )
				pick = i;
		}
	}
	if( pick < 0 )
	{
		heap->failures++;
		return 0;
	}

	if( heap->segs[pick].size > need )
	{
		gc_aram_seg_t rest = heap->segs[pick];
		gc_aram_seg_t used = heap->segs[pick];

		used.used = 1;
		used.cls = (uint8_t)cls;
		used.size = need;
		GC_AramSetTag( &used, tag );
		rest.size -= need;
		if( cls == GC_ARAM_SESSION )
		{
			rest.offset += need;
			if( !GC_AramHeapInsert( heap, pick + 1, &rest ))
			{
				heap->failures++;
				return 0;
			}
			heap->segs[pick] = used;
		}
		else
		{
			used.offset = rest.offset + rest.size;
			if( !GC_AramHeapInsert( heap, pick + 1, &used ))
			{
				heap->failures++;
				return 0;
			}
			heap->segs[pick] = rest;
			pick++;
		}
	}
	else
	{
		heap->segs[pick].used = 1;
		heap->segs[pick].cls = (uint8_t)cls;
		GC_AramSetTag( &heap->segs[pick], tag );
	}

	if( cls == GC_ARAM_SESSION )
		heap->session_bytes += need;
	else
		heap->stream_bytes += need;
	if( GC_AramHeapUsedBytes( heap ) > heap->peak_bytes )
		heap->peak_bytes = GC_AramHeapUsedBytes( heap );
	return heap->segs[pick].offset;
}

static int GC_AramHeapFind( const gc_aram_heap_t *heap, uint32_t addr )
{
	int lo = 0, hi = heap->count - 1;

	while( lo <= hi )
	{
		const int mid = ( lo + hi ) / 2;

		if( heap->segs[mid].offset == addr )
			return mid;
		if( heap->segs[mid].offset < addr )
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return -1;
}

int GC_AramHeapFree( gc_aram_heap_t *heap, uint32_t addr )
{
	int i;

	if( !heap || !addr )
		return 0;
	i = GC_AramHeapFind( heap, addr );
	if( i < 0 || !heap->segs[i].used )
		return 0;
	if( heap->segs[i].cls == GC_ARAM_SESSION )
		heap->session_bytes -= heap->segs[i].size;
	else
		heap->stream_bytes -= heap->segs[i].size;
	heap->segs[i].used = 0;
	heap->segs[i].cls = 0;
	memset( heap->segs[i].tag, 0, sizeof( heap->segs[i].tag ));

	if( i + 1 < heap->count && !heap->segs[i + 1].used )
	{
		heap->segs[i].size += heap->segs[i + 1].size;
		GC_AramHeapRemove( heap, i + 1 );
	}
	if( i > 0 && !heap->segs[i - 1].used )
	{
		heap->segs[i - 1].size += heap->segs[i].size;
		GC_AramHeapRemove( heap, i );
	}
	return 1;
}

uint32_t GC_AramHeapBlockSize( const gc_aram_heap_t *heap, uint32_t addr )
{
	const int i = heap ? GC_AramHeapFind( heap, addr ) : -1;

	return ( i >= 0 && heap->segs[i].used ) ? heap->segs[i].size : 0;
}

uint32_t GC_AramHeapFreeBytes( const gc_aram_heap_t *heap )
{
	if( !heap || !heap->count )
		return 0;
	return ( heap->end - heap->base ) - GC_AramHeapUsedBytes( heap );
}

uint32_t GC_AramHeapLargestFree( const gc_aram_heap_t *heap )
{
	uint32_t best = 0;
	int i;

	for( i = 0; heap && i < heap->count; i++ )
	{
		if( !heap->segs[i].used && heap->segs[i].size > best )
			best = heap->segs[i].size;
	}
	return best;
}

int GC_AramHeapValidate( const gc_aram_heap_t *heap )
{
	uint32_t cursor, session = 0, stream = 0;
	int i;

	if( !heap->count )
		return 1;
	cursor = heap->base;
	for( i = 0; i < heap->count; i++ )
	{
		const gc_aram_seg_t *seg = &heap->segs[i];

		if( seg->offset != cursor || !seg->size || ( seg->size % GC_ARAM_ALIGN ))
			return 0;
		if( !seg->used && i > 0 && !heap->segs[i - 1].used )
			return 0;
		if( seg->used && seg->cls == GC_ARAM_SESSION )
			session += seg->size;
		else if( seg->used )
			stream += seg->size;
		cursor += seg->size;
	}
	return cursor == heap->end && session == heap->session_bytes && stream == heap->stream_bytes;
}

/*
==============================================================================

Asset cache: whole files kept in ARAM by name, least recently used first out.

==============================================================================
*/
static int GC_AramNameEq( const char *a, const char *b )
{
	for( ;; a++, b++ )
	{
		char ca = *a, cb = *b;

		if( ca >= 'A' && ca <= 'Z' ) ca = (char)( ca - 'A' + 'a' );
		if( cb >= 'A' && cb <= 'Z' ) cb = (char)( cb - 'A' + 'a' );
		if( ca == '\\' ) ca = '/';
		if( cb == '\\' ) cb = '/';
		if( ca != cb )
			return 0;
		if( !ca )
			return 1;
	}
}

void GC_AramCacheInit( gc_aram_cache_t *cache, gc_aram_heap_t *heap, gc_aram_copy_fn copy, uint32_t budget )
{
	memset( cache, 0, sizeof( *cache ));
	cache->heap = heap;
	cache->copy = copy;
	cache->budget = budget & ~( GC_ARAM_ALIGN - 1u );
}

static int GC_AramCacheLookup( const gc_aram_cache_t *cache, const char *name )
{
	int i;

	for( i = 0; i < GC_ARAM_CACHE_MAX; i++ )
	{
		if( cache->assets[i].used && GC_AramNameEq( cache->assets[i].name, name ))
			return i;
	}
	return -1;
}

static void GC_AramCacheDrop( gc_aram_cache_t *cache, int index )
{
	gc_aram_asset_t *asset = &cache->assets[index];
	const uint32_t block = GC_AramHeapBlockSize( cache->heap, asset->aram );

	GC_AramHeapFree( cache->heap, asset->aram );
	cache->bytes -= block;
	memset( asset, 0, sizeof( *asset ));
}

static int GC_AramCacheEvictOne( gc_aram_cache_t *cache )
{
	int i, pick = -1;

	for( i = 0; i < GC_ARAM_CACHE_MAX; i++ )
	{
		const gc_aram_asset_t *asset = &cache->assets[i];

		if( !asset->used || !asset->ready || asset->pins )
			continue;
		if( pick < 0 || asset->last_use < cache->assets[pick].last_use )
			pick = i;
	}
	if( pick < 0 )
		return 0;
	GC_AramCacheDrop( cache, pick );
	cache->evictions++;
	return 1;
}

int GC_AramCacheFind( gc_aram_cache_t *cache, const char *name )
{
	const int i = ( cache && name ) ? GC_AramCacheLookup( cache, name ) : -1;

	if( !cache )
		return -1;
	if( i < 0 || !cache->assets[i].ready )
	{
		cache->misses++;
		return -1;
	}
	cache->assets[i].last_use = ++cache->clock;
	cache->hits++;
	return i;
}

int GC_AramCacheBegin( gc_aram_cache_t *cache, const char *name, uint32_t size )
{
	uint32_t need, aram;
	int i, slot = -1;

	if( !cache || !cache->heap || !name || !name[0] || !size
		|| strlen( name ) >= GC_ARAM_NAME_LEN )
		return -1;
	need = ( size + ( GC_ARAM_ALIGN - 1u )) & ~( GC_ARAM_ALIGN - 1u );
	if( need < size || need > cache->budget )
	{
		cache->refusals++;
		return -1;
	}

	i = GC_AramCacheLookup( cache, name );
	if( i >= 0 )
	{
		if( cache->assets[i].pins || !cache->assets[i].ready )
		{
			cache->refusals++;
			return -1;
		}
		GC_AramCacheDrop( cache, i );
	}

	while( cache->bytes + need > cache->budget )
	{
		if( !GC_AramCacheEvictOne( cache ))
		{
			cache->refusals++;
			return -1;
		}
	}
	for( ;; )
	{
		aram = GC_AramHeapAlloc( cache->heap, need, GC_ARAM_STREAM, "cache" );
		if( aram )
			break;
		if( !GC_AramCacheEvictOne( cache ))
		{
			cache->refusals++;
			return -1;
		}
	}
	for( i = 0; i < GC_ARAM_CACHE_MAX; i++ )
	{
		if( !cache->assets[i].used )
		{
			slot = i;
			break;
		}
	}
	if( slot < 0 )
	{
		/* Table full: reuse the least recently used unpinned entry. */
		if( !GC_AramCacheEvictOne( cache ))
		{
			GC_AramHeapFree( cache->heap, aram );
			cache->refusals++;
			return -1;
		}
		for( i = 0; i < GC_ARAM_CACHE_MAX; i++ )
		{
			if( !cache->assets[i].used )
			{
				slot = i;
				break;
			}
		}
	}

	memset( &cache->assets[slot], 0, sizeof( cache->assets[slot] ));
	memcpy( cache->assets[slot].name, name, strlen( name ) + 1 );
	cache->assets[slot].size = size;
	cache->assets[slot].aram = aram;
	cache->assets[slot].used = 1;
	cache->assets[slot].last_use = ++cache->clock;
	cache->bytes += GC_AramHeapBlockSize( cache->heap, aram );
	return slot;
}

static int GC_AramCacheValid( const gc_aram_cache_t *cache, int index )
{
	return cache && index >= 0 && index < GC_ARAM_CACHE_MAX && cache->assets[index].used;
}

int GC_AramCacheWrite( gc_aram_cache_t *cache, int index, uint32_t offset, const void *data, uint32_t len )
{
	gc_aram_asset_t *asset;

	if( !GC_AramCacheValid( cache, index ) || !data || !cache->copy )
		return 0;
	asset = &cache->assets[index];
	if( offset > asset->size || len > asset->size - offset )
		return 0;
	if( len )
		cache->copy( asset->aram + offset, (void *)data, len, 1 );
	return 1;
}

int GC_AramCacheCommit( gc_aram_cache_t *cache, int index )
{
	if( !GC_AramCacheValid( cache, index ))
		return 0;
	cache->assets[index].ready = 1;
	cache->stores++;
	return 1;
}

void GC_AramCacheAbort( gc_aram_cache_t *cache, int index )
{
	if( GC_AramCacheValid( cache, index ) && !cache->assets[index].ready )
		GC_AramCacheDrop( cache, index );
}

int GC_AramCacheStore( gc_aram_cache_t *cache, const char *name, const void *data, uint32_t size )
{
	const int index = GC_AramCacheBegin( cache, name, size );

	if( index < 0 )
		return -1;
	if( !GC_AramCacheWrite( cache, index, 0, data, size ))
	{
		GC_AramCacheAbort( cache, index );
		return -1;
	}
	GC_AramCacheCommit( cache, index );
	return index;
}

int GC_AramCacheRead( gc_aram_cache_t *cache, int index, uint32_t offset, void *dst, uint32_t len )
{
	gc_aram_asset_t *asset;

	if( !GC_AramCacheValid( cache, index ) || !dst || !cache->copy )
		return 0;
	asset = &cache->assets[index];
	if( !asset->ready || offset > asset->size || len > asset->size - offset )
		return 0;
	if( len )
		cache->copy( asset->aram + offset, dst, len, 0 );
	asset->last_use = ++cache->clock;
	return 1;
}

uint32_t GC_AramCacheSize( const gc_aram_cache_t *cache, int index )
{
	return GC_AramCacheValid( cache, index ) ? cache->assets[index].size : 0;
}

void GC_AramCachePin( gc_aram_cache_t *cache, int index )
{
	if( GC_AramCacheValid( cache, index ) && cache->assets[index].pins < 0xffffu )
		cache->assets[index].pins++;
}

void GC_AramCacheUnpin( gc_aram_cache_t *cache, int index )
{
	if( GC_AramCacheValid( cache, index ) && cache->assets[index].pins )
		cache->assets[index].pins--;
}

void GC_AramCacheEvict( gc_aram_cache_t *cache, const char *name )
{
	const int i = ( cache && name ) ? GC_AramCacheLookup( cache, name ) : -1;

	if( i >= 0 && !cache->assets[i].pins && cache->assets[i].ready )
	{
		GC_AramCacheDrop( cache, i );
		cache->evictions++;
	}
}

int GC_AramCacheEvictLRU( gc_aram_cache_t *cache )
{
	return cache ? GC_AramCacheEvictOne( cache ) : 0;
}

void GC_AramCacheFlush( gc_aram_cache_t *cache )
{
	while( cache && GC_AramCacheEvictOne( cache ))
		;
}
