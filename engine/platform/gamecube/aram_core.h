/*
aram_core.h - GameCube ARAM allocator and asset cache (platform independent)
Copyright (C) 2026 xash3d-gc contributors

This file has no libogc or engine dependencies so the allocator and cache
policy can be tested on the host (tests/test_gamecube_aram.py). The GameCube
glue that owns the real 16 MiB of ARAM lives in mem_gamecube.c.
*/
#ifndef ARAM_CORE_H
#define ARAM_CORE_H

#include <stddef.h>
#include <stdint.h>

#define GC_ARAM_ALIGN      32u
#define GC_ARAM_MAX_SEGS   64
#define GC_ARAM_TAG_LEN    12
#define GC_ARAM_CACHE_MAX  48
#define GC_ARAM_NAME_LEN   48

/* Session regions live for the whole boot (audio, transition saves, the
 * texture store) and are carved from the bottom of the pool. Stream blocks
 * can be freed (video prefetch, overflow tables, cached assets) and are
 * carved from the top, so freeing them never strands a session region. */
typedef enum
{
	GC_ARAM_SESSION = 0,
	GC_ARAM_STREAM = 1
} gc_aram_class_t;

typedef struct
{
	uint32_t offset;   /* absolute ARAM address */
	uint32_t size;     /* multiple of GC_ARAM_ALIGN */
	uint8_t  used;
	uint8_t  cls;
	char     tag[GC_ARAM_TAG_LEN];
} gc_aram_seg_t;

typedef struct
{
	uint32_t base, end;
	int count;
	gc_aram_seg_t segs[GC_ARAM_MAX_SEGS]; /* sorted by offset, cover [base,end) */
	uint32_t session_bytes, stream_bytes, peak_bytes;
	uint32_t failures;
} gc_aram_heap_t;

void     GC_AramHeapInit( gc_aram_heap_t *heap, uint32_t base, uint32_t size );
/* Returns the ARAM address, or 0 on failure (ARAM address 0 is DSP-reserved). */
uint32_t GC_AramHeapAlloc( gc_aram_heap_t *heap, uint32_t size, gc_aram_class_t cls, const char *tag );
/* Frees a block by its address. Returns 1 if a used block started there. */
int      GC_AramHeapFree( gc_aram_heap_t *heap, uint32_t addr );
uint32_t GC_AramHeapBlockSize( const gc_aram_heap_t *heap, uint32_t addr );
uint32_t GC_AramHeapFreeBytes( const gc_aram_heap_t *heap );
uint32_t GC_AramHeapLargestFree( const gc_aram_heap_t *heap );
/* Internal consistency check for tests: segments sorted, contiguous, merged. */
int      GC_AramHeapValidate( const gc_aram_heap_t *heap );

/* Byte mover. On GameCube this is GC_AramCopy (bounce-buffered DMA). */
typedef void (*gc_aram_copy_fn)( uint32_t aram, void *buf, uint32_t len, int write );

typedef struct
{
	char     name[GC_ARAM_NAME_LEN];
	uint32_t size;      /* payload bytes */
	uint32_t aram;      /* heap block address */
	uint32_t last_use;
	uint16_t pins;
	uint8_t  used;
	uint8_t  ready;     /* payload fully written */
} gc_aram_asset_t;

typedef struct
{
	gc_aram_heap_t *heap;
	gc_aram_copy_fn copy;
	uint32_t budget;    /* max bytes the cache may hold in the heap */
	uint32_t bytes;
	uint32_t clock;
	uint32_t hits, misses, stores, evictions, refusals;
	gc_aram_asset_t assets[GC_ARAM_CACHE_MAX];
} gc_aram_cache_t;

void GC_AramCacheInit( gc_aram_cache_t *cache, gc_aram_heap_t *heap, gc_aram_copy_fn copy, uint32_t budget );
/* Index of a ready asset, or -1. Counts a hit or a miss and refreshes LRU. */
int  GC_AramCacheFind( gc_aram_cache_t *cache, const char *name );
/* Store a whole payload already in MEM1. Evicts unpinned LRU assets to fit.
 * Returns the asset index, or -1 if it cannot fit without touching pins. */
int  GC_AramCacheStore( gc_aram_cache_t *cache, const char *name, const void *data, uint32_t size );
/* Streamed store for payloads that never exist whole in MEM1: Begin reserves,
 * Write fills (any offsets), Commit makes it findable, Abort releases it. */
int  GC_AramCacheBegin( gc_aram_cache_t *cache, const char *name, uint32_t size );
int  GC_AramCacheWrite( gc_aram_cache_t *cache, int index, uint32_t offset, const void *data, uint32_t len );
int  GC_AramCacheCommit( gc_aram_cache_t *cache, int index );
void GC_AramCacheAbort( gc_aram_cache_t *cache, int index );
int  GC_AramCacheRead( gc_aram_cache_t *cache, int index, uint32_t offset, void *dst, uint32_t len );
uint32_t GC_AramCacheSize( const gc_aram_cache_t *cache, int index );
void GC_AramCachePin( gc_aram_cache_t *cache, int index );
void GC_AramCacheUnpin( gc_aram_cache_t *cache, int index );
void GC_AramCacheEvict( gc_aram_cache_t *cache, const char *name );
/* Drop the least recently used unpinned asset. Returns 0 if none could go. */
int  GC_AramCacheEvictLRU( gc_aram_cache_t *cache );
/* Drop every unpinned asset (e.g. when a subsystem needs the pool back). */
void GC_AramCacheFlush( gc_aram_cache_t *cache );

#endif /* ARAM_CORE_H */
