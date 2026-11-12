/*
wad_dir_gc.h - ARAM-resident WAD lump directory (platform independent)
Copyright (C) 2026 xash3d-gc contributors

A WAD's lump table costs 32 bytes per lump, and the loader needs a second copy
while it sorts, so a large WAD (halflife.wad: 3116 lumps) overruns the GameCube
filesystem arena. This keeps the lump entries in ARAM in file order and holds
only a sorted 16-bit index in MEM1 (2 bytes per lump). Lookups binary search
the index and fetch the entries they compare from ARAM through a small cache.

No libogc or engine dependencies: tested on the host (tests/test_gamecube_wad_dir.py).
*/
#ifndef WAD_DIR_GC_H
#define WAD_DIR_GC_H

#include <stdint.h>

#define GC_WADDIR_NAME_LEN   16
#define GC_WADDIR_ENTRY_SIZE 32
#define GC_WADDIR_CACHE      4
#define GC_WADDIR_ANY_TYPE   (-1)

/* Same layout as dlumpinfo_t in filesystem.h. */
typedef struct
{
	int32_t filepos;
	int32_t disksize;
	int32_t size;
	int8_t  type;
	int8_t  attribs;
	int8_t  pad0, pad1;
	char    name[GC_WADDIR_NAME_LEN];
} gc_waddir_entry_t;

/* Byte mover. On GameCube this is GC_AramCopy. len is a multiple of 32 and
 * both the ARAM address and the buffer are 32-byte aligned. */
typedef void (*gc_waddir_copy_fn)( uint32_t aram, void *buf, uint32_t len, int write );

typedef struct
{
	gc_waddir_copy_fn copy;
	uint32_t aram;         /* 32-aligned base of count * 32 bytes */
	uint32_t count;        /* entries written so far (capacity when sealed) */
	uint32_t capacity;
	uint16_t *order;       /* capacity entries, owned by the caller */
	uint64_t *keys;        /* build-time scratch: lowercase 8-char name prefix */
	int8_t   *types;       /* build-time scratch, follows keys */
	int      sealed;
	int32_t  cache_idx[GC_WADDIR_CACHE];
	gc_waddir_entry_t cache[GC_WADDIR_CACHE] __attribute__(( aligned( 32 )));
	gc_waddir_entry_t stage[2] __attribute__(( aligned( 32 )));
} gc_waddir_t;

/* Bytes of ARAM a directory of count entries needs. */
uint32_t GC_WadDirAramBytes( uint32_t count );
/* Scratch bytes the sort needs while the directory is built (an 8-byte name
 * prefix and the type per lump). Free it after GC_WadDirSeal; lookups do not use it. */
uint32_t GC_WadDirScratchBytes( uint32_t count );
/* capacity <= 65535. aram is the caller's block of GC_WadDirAramBytes; scratch
 * is GC_WadDirScratchBytes( capacity ) bytes, 8-byte aligned. */
int  GC_WadDirInit( gc_waddir_t *dir, gc_waddir_copy_fn copy, uint32_t aram,
	uint32_t capacity, uint16_t *order, void *scratch );
/* Append entries in file order; names must already be lowercased. 1 on success. */
int  GC_WadDirAppend( gc_waddir_t *dir, const gc_waddir_entry_t *entries, uint32_t n );
/* Sort the index by (name, type, file order). Call once after the last append. */
int  GC_WadDirSeal( gc_waddir_t *dir );
/* Fetch entry by file index. */
int  GC_WadDirGet( gc_waddir_t *dir, uint32_t index, gc_waddir_entry_t *out );
/* Find a lump by case-insensitive name and type (GC_WADDIR_ANY_TYPE for any).
 * Returns its file index or -1. Of several matches the first in sorted order. */
int  GC_WadDirFind( gc_waddir_t *dir, const char *name, int type, gc_waddir_entry_t *out );
/* File index of the k-th lump in sorted order, or -1. */
int  GC_WadDirSortedIndex( const gc_waddir_t *dir, uint32_t k );

#endif /* WAD_DIR_GC_H */
