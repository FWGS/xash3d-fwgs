/*
zone.c - zone memory allocation from DarkPlaces
Copyright (C) 1996-1997 Id Software, Inc.
Copyright (C) 2000-2007 DarkPlaces contributors
Copyright (C) 2007 Uncle Mike
Copyright (C) 2015-2023 Xash3D FWGS contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "common.h"

#define MEMHEADER_SENTINEL_BIG   0xA1BAU
#define MEMHEADER_SENTINEL_SMALL 0xAD1EU
#define MEMHEADER_SENTINEL2      0xDFU

#define MEM_SMALL_MAX UINT8_MAX

#ifdef XASH_CUSTOM_SWAP
#include "platform/swap/swap.h"
#define Q_malloc SWAP_Malloc
#define Q_free SWAP_Free

static void *Q_realloc( void *mem, size_t size )
{
	void *newmem;

	if( mem && size == 0 )
	{
		Q_free( mem );
		return NULL;
	}

	newmem = Q_malloc( size );
	if( mem && newmem )
	{
		memcpy( newmem, mem, size );
		Q_free( mem );
	}

	return newmem;
}
#else
#define Q_malloc malloc
#define Q_free free
#define Q_realloc realloc
#endif

// big header: full debug info, used for every allocation in pools without MEM_SMALL_ALLOC_OPT,
// and for allocations >MEM_SMALL_MAX in pools that opted in.
// on ILP32 it's 24 bytes, on LP64 it's 40 bytes
typedef struct memheader_s
{
	struct memheader_s *next, *prev; // next and previous memheaders in chain belonging to pool
	const char         *filename;    // file name and line where Mem_Alloc was called
	size_t             size;         // size of the memory after the header (excluding header and sentinel2)
	poolhandle_t       poolptr;      // pool this memheader belongs to
	uint16_t           fileline;
	uint16_t           sentinel1;    // must be MEMHEADER_SENTINEL_BIG
	// immediately followed by data, which is followed by a MEMHEADER_SENTINEL2 byte
} memheader_t;

STATIC_CHECK_SIZEOF( memheader_t, 24, 40 );

// compact header: used in MEM_SMALL_ALLOC_OPT pools for allocations up to MEM_SMALL_MAX bytes.
// no filename/fileline; size is a single byte.
// on ILP32 it's 16 bytes, on LP64 it's 24 bytes
typedef struct memheader_small_s
{
	struct memheader_small_s *next, *prev;
	poolhandle_t              poolptr;
	uint8_t                   size;
	uint8_t                   pad;
	uint16_t                  sentinel1; // must be MEMHEADER_SENTINEL_SMALL
} memheader_small_t;

STATIC_CHECK_SIZEOF( memheader_small_t, 16, 24 );

typedef struct mempool_s
{
	struct memheader_s       *chain;        // big allocations
	struct memheader_small_s *chain_small;  // compact allocations (only used if MEM_SMALL_ALLOC_OPT)
	size_t             totalsize;     // total memory allocated in this pool (inside memheaders)
	size_t             realsize;      // total memory allocated in this pool (actual malloc total)
	size_t             lastchecksize; // updated each time the pool is displayed by memlist
	const char         *filename;     // file name and line where Mem_AllocPool was called
	int                fileline;
	uint               flags;         // MEM_SMALL_ALLOC_OPT, etc.
	char               name[64];      // name of the pool
} mempool_t;

static mempool_t *poolchain = NULL; // critical stuff
static size_t poolcount = 0;

// a1ba: due to mempool being passed with the model through reused 32-bit field
// which makes engine incompatible with 64-bit pointers I changed mempool type
// from pointer to 32-bit handle, thankfully mempool structure is private
static mempool_t *Mem_FindPool( poolhandle_t poolptr )
{
	if( likely( poolptr > 0 && poolptr <= poolcount ))
		return &poolchain[poolptr - 1];

	Sys_Error( "%s: not allocated or double freed pool %d", __func__, poolptr );
	return NULL;
}

static const char *Mem_PoolName( poolhandle_t poolptr )
{
	if( poolptr > 0 && poolptr <= poolcount && poolchain[poolptr - 1].filename )
		return poolchain[poolptr - 1].name;
	return "<unknown pool>";
}

static poolhandle_t Mem_PoolIndex( mempool_t *mempool )
{
	return (poolhandle_t)(mempool - poolchain) + 1;
}

static inline void Mem_PoolAdd( mempool_t *pool, size_t paysize, size_t blocksize )
{
	pool->totalsize += paysize;
	pool->realsize += blocksize;
}

static inline void Mem_PoolSubtract( mempool_t *pool, size_t paysize, size_t blocksize )
{
	pool->totalsize -= paysize;
	pool->realsize -= blocksize;
}

static inline void Mem_PoolLinkAllocBig( mempool_t *pool, memheader_t *mem )
{
	mem->next = pool->chain;
	if( mem->next ) mem->next->prev = mem;
	pool->chain = mem;
	mem->prev = NULL;
	mem->poolptr = Mem_PoolIndex( pool );
}

static inline void Mem_PoolUnlinkAllocBig( mempool_t *pool, memheader_t *mem )
{
	if( mem->next ) mem->next->prev = mem->prev;
	if( mem->prev ) mem->prev->next = mem->next;
	else pool->chain = mem->next;
	mem->poolptr = 0;
}

static inline void Mem_InitAllocBig( memheader_t *mem, size_t size, const char *filename, int fileline )
{
	mem->size = size;
	mem->filename = filename;
	mem->fileline = fileline;
	mem->sentinel1 = MEMHEADER_SENTINEL_BIG;
	*((byte *)mem + sizeof( memheader_t ) + size ) = MEMHEADER_SENTINEL2;
}

static inline void Mem_PoolLinkAllocSmall( mempool_t *pool, memheader_small_t *mem )
{
	mem->next = pool->chain_small;
	if( mem->next ) mem->next->prev = mem;
	pool->chain_small = mem;
	mem->prev = NULL;
	mem->poolptr = Mem_PoolIndex( pool );
}

static inline void Mem_PoolUnlinkAllocSmall( mempool_t *pool, memheader_small_t *mem )
{
	if( mem->next ) mem->next->prev = mem->prev;
	if( mem->prev ) mem->prev->next = mem->next;
	else pool->chain_small = mem->next;
	mem->poolptr = 0;
}

static inline void Mem_InitAllocSmall( memheader_small_t *mem, size_t size )
{
	mem->size = (uint8_t)size;
	mem->sentinel1 = MEMHEADER_SENTINEL_SMALL;
	*((byte *)mem + sizeof( memheader_small_t ) + size ) = MEMHEADER_SENTINEL2;
}

static uint16_t Mem_ReadSentinel( const void *data )
{
	return ((const uint16_t *)data)[-1];
}

static const char *Mem_CheckFilename( const char *filename )
{
	if( COM_StringEmptyOrNULL( filename ))
		return "<corrupted>";

	if( memchr( filename, '\0', MAX_OSPATH ) != NULL )
		return filename;

	return "<corrupted>";
}

static qboolean Mem_CheckAllocHeaderBig( const char *func, const memheader_t *mem, const char *filename, int fileline )
{
	if( mem->sentinel1 != MEMHEADER_SENTINEL_BIG )
	{
		const char *memfilename = Mem_CheckFilename( mem->filename );
		Sys_Error( "%s: trashed header sentinel 1 (alloc at %s:%i, check at %s:%i)\n", func, memfilename, mem->fileline, filename, fileline );
		return false;
	}

	if( *((const byte *)mem + sizeof( memheader_t ) + mem->size ) != MEMHEADER_SENTINEL2 )
	{
		const char *memfilename = Mem_CheckFilename( mem->filename );
		Sys_Error( "%s: trashed header sentinel 2 (alloc at %s:%i, check at %s:%i)\n", func, memfilename, mem->fileline, filename, fileline );
		return false;
	}

	return true;
}

static qboolean Mem_CheckAllocHeaderSmall( const char *func, const memheader_small_t *mem, const char *filename, int fileline )
{
	if( mem->sentinel1 != MEMHEADER_SENTINEL_SMALL )
	{
		Sys_Error( "%s: trashed small header sentinel 1 (pool \"%s\", check at %s:%i)\n", func, Mem_PoolName( mem->poolptr ), filename, fileline );
		return false;
	}

	if( *((const byte *)mem + sizeof( memheader_small_t ) + mem->size ) != MEMHEADER_SENTINEL2 )
	{
		Sys_Error( "%s: trashed small header sentinel 2 (pool \"%s\", check at %s:%i)\n", func, Mem_PoolName( mem->poolptr ), filename, fileline );
		return false;
	}

	return true;
}

void *_Mem_Alloc( poolhandle_t poolptr, size_t size, qboolean clear, const char *filename, int fileline )
{
	if( size <= 0 )
		return NULL;

	if( unlikely( !poolptr ))
	{
		Sys_Error( "%s: pool == NULL (alloc at %s:%i)\n", __func__, filename, fileline );
		return NULL;
	}

	mempool_t *pool = Mem_FindPool( poolptr );
	if( !pool )
		return NULL;

	if( FBitSet( pool->flags, MEM_SMALL_ALLOC_OPT ) && size <= MEM_SMALL_MAX )
	{
		size_t blocksize = sizeof( memheader_small_t ) + size + sizeof( byte );
		memheader_small_t *mem = Q_malloc( blocksize );

		if( mem == NULL )
		{
			Sys_Error( "%s: out of memory (alloc size %s at %s:%i)\n", __func__, Q_memprint( size ), filename, fileline );
			return NULL;
		}

		Mem_InitAllocSmall( mem, size );
		Mem_PoolAdd( pool, size, blocksize );
		Mem_PoolLinkAllocSmall( pool, mem );

		if( clear )
			memset((byte *)mem + sizeof( memheader_small_t ), 0, size );

		return (byte *)mem + sizeof( memheader_small_t );
	}
	else
	{
		size_t blocksize = sizeof( memheader_t ) + size + sizeof( byte );
		memheader_t *mem = Q_malloc( blocksize );

		if( mem == NULL )
		{
			Sys_Error( "%s: out of memory (alloc size %s at %s:%i)\n", __func__, Q_memprint( size ), filename, fileline );
			return NULL;
		}

		Mem_InitAllocBig( mem, size, filename, fileline );
		Mem_PoolAdd( pool, size, blocksize );
		Mem_PoolLinkAllocBig( pool, mem );

		if( clear )
			memset((byte *)mem + sizeof( memheader_t ), 0, size );

		return (byte *)mem + sizeof( memheader_t );
	}
}

static void Mem_FreeBlockBig( memheader_t *mem, const char *filename, int fileline )
{
	if( !Mem_CheckAllocHeaderBig( __func__, mem, filename, fileline ))
		return;

	mempool_t *pool = Mem_FindPool( mem->poolptr );
	if( !pool )
		return;

	if(( mem->prev ? mem->prev->next != mem : pool->chain != mem ) || ( mem->next && mem->next->prev != mem ))
	{
		Sys_Error( "%s: not allocated or double freed (free at %s:%i)\n", __func__, filename, fileline );
		return;
	}

	Mem_PoolSubtract( pool, mem->size, sizeof( memheader_t ) + mem->size + sizeof( byte ));
	Mem_PoolUnlinkAllocBig( pool, mem );

	Q_free( mem );
}

static void Mem_FreeBlockSmall( memheader_small_t *mem, const char *filename, int fileline )
{
	if( !Mem_CheckAllocHeaderSmall( __func__, mem, filename, fileline ))
		return;

	mempool_t *pool = Mem_FindPool( mem->poolptr );
	if( !pool )
		return;

	if(( mem->prev ? mem->prev->next != mem : pool->chain_small != mem ) || ( mem->next && mem->next->prev != mem ))
	{
		Sys_Error( "%s: not allocated or double freed (free at %s:%i)\n", __func__, filename, fileline );
		return;
	}

	Mem_PoolSubtract( pool, mem->size, sizeof( memheader_small_t ) + mem->size + sizeof( byte ));
	Mem_PoolUnlinkAllocSmall( pool, mem );

	Q_free( mem );
}

void _Mem_Free( void *data, const char *filename, int fileline )
{
	if( data == NULL )
		return;

	if( Mem_ReadSentinel( data ) == MEMHEADER_SENTINEL_SMALL )
		Mem_FreeBlockSmall((memheader_small_t *)((byte *)data - sizeof( memheader_small_t )), filename, fileline );
	else
		Mem_FreeBlockBig((memheader_t *)((byte *)data - sizeof( memheader_t )), filename, fileline );
}

static void Mem_MigratePoolBig( poolhandle_t newpoolptr, memheader_t *mem )
{
	mempool_t *oldpool = Mem_FindPool( mem->poolptr );
	mempool_t *newpool = Mem_FindPool( newpoolptr );
	size_t blocksize = sizeof( memheader_t ) + mem->size + sizeof( byte );

	Mem_PoolUnlinkAllocBig( oldpool, mem );
	Mem_PoolSubtract( oldpool, mem->size, blocksize );

	Mem_PoolLinkAllocBig( newpool, mem );
	Mem_PoolAdd( newpool, mem->size, blocksize );
}

static void Mem_MigratePoolSmall( poolhandle_t newpoolptr, memheader_small_t *mem )
{
	mempool_t *oldpool = Mem_FindPool( mem->poolptr );
	mempool_t *newpool = Mem_FindPool( newpoolptr );
	size_t blocksize = sizeof( memheader_small_t ) + mem->size + sizeof( byte );

	Mem_PoolUnlinkAllocSmall( oldpool, mem );
	Mem_PoolSubtract( oldpool, mem->size, blocksize );

	Mem_PoolLinkAllocSmall( newpool, mem );
	Mem_PoolAdd( newpool, mem->size, blocksize );
}

void *_Mem_Realloc( poolhandle_t poolptr, void *data, size_t size, qboolean clear, const char *filename, int fileline )
{
	if( size <= 0 )
		return data; // no need to reallocate

	if( unlikely( !poolptr ))
	{
		Sys_Error( "%s: pool == NULL (alloc at %s:%i)\n", __func__, filename, fileline );
		return NULL;
	}

	if( !data )
		return _Mem_Alloc( poolptr, size, clear, filename, fileline );

	mempool_t *pool = Mem_FindPool( poolptr );

	if( Mem_ReadSentinel( data ) == MEMHEADER_SENTINEL_SMALL )
	{
		memheader_small_t *mem = (memheader_small_t *)((byte *)data - sizeof( memheader_small_t ));

		if( !Mem_CheckAllocHeaderSmall( __func__, mem, filename, fileline ))
			return NULL;

		size_t oldsize = mem->size;

		// promote to big header if target pool doesn't opt in, or new size doesn't fit
		if( size > MEM_SMALL_MAX || !FBitSet( pool->flags, MEM_SMALL_ALLOC_OPT ))
		{
			void *newdata = _Mem_Alloc( poolptr, size, false, filename, fileline );
			if( !newdata )
				return NULL;

			memcpy( newdata, data, Q_min( oldsize, size ));
			if( clear && size > oldsize )
				memset((byte *)newdata + oldsize, 0, size - oldsize );

			Mem_FreeBlockSmall( mem, filename, fileline );
			return newdata;
		}

		if( mem->poolptr != poolptr )
			Mem_MigratePoolSmall( poolptr, mem );

		if( size == oldsize )
			return data;

		// stays small, shrink/grow within the small layout
		uintptr_t oldmem = (uintptr_t)mem;
		mem = Q_realloc( mem, sizeof( memheader_small_t ) + size + sizeof( byte ));

		if( mem == NULL )
		{
			Sys_Error( "%s: out of memory (alloc size %s at %s:%i)\n", __func__, Q_memprint( size ), filename, fileline );
			return NULL;
		}

		Mem_InitAllocSmall( mem, size );

		if( size > oldsize )
		{
			Mem_PoolAdd( pool, size - oldsize, size - oldsize );

			if( clear )
				memset((byte *)mem + sizeof( memheader_small_t ) + oldsize, 0, size - oldsize );
		}
		else Mem_PoolSubtract( pool, oldsize - size, oldsize - size );

		if( oldmem != (uintptr_t)mem )
		{
			if( mem->next ) mem->next->prev = mem;
			if( mem->prev ) mem->prev->next = mem;
			else pool->chain_small = mem;
		}

		return (byte *)mem + sizeof( memheader_small_t );
	}
	else
	{
		memheader_t *mem = (memheader_t *)((byte *)data - sizeof( memheader_t ));

		if( !Mem_CheckAllocHeaderBig( __func__, mem, filename, fileline ))
			return NULL;

		if( mem->poolptr != poolptr )
			Mem_MigratePoolBig( poolptr, mem );

		size_t oldsize = mem->size;
		if( size == oldsize )
			return data;

		uintptr_t oldmem = (uintptr_t)mem;
		mem = Q_realloc( mem, sizeof( memheader_t ) + size + sizeof( byte ));

		if( mem == NULL )
		{
			Sys_Error( "%s: out of memory (alloc size %s at %s:%i)\n", __func__, Q_memprint( size ), filename, fileline );
			return NULL;
		}

		Mem_InitAllocBig( mem, size, filename, fileline );

		if( size > oldsize )
		{
			Mem_PoolAdd( pool, size - oldsize, size - oldsize );

			if( clear )
				memset((byte *)mem + sizeof( memheader_t ) + oldsize, 0, size - oldsize );
		}
		else Mem_PoolSubtract( pool, oldsize - size, oldsize - size );

		if( oldmem != (uintptr_t)mem )
		{
			if( mem->next ) mem->next->prev = mem;
			if( mem->prev ) mem->prev->next = mem;
			else pool->chain = mem;
		}

		return (byte *)mem + sizeof( memheader_t );
	}
}

static poolhandle_t Mem_InitPool( mempool_t *pool, const char *name, unsigned int flags, const char *filename, int fileline )
{
	memset( pool, 0, sizeof( *pool ));

	pool->filename = filename;
	pool->fileline = fileline;
	pool->flags = flags;
	pool->realsize = sizeof( mempool_t );
	Q_strncpy( pool->name, name, sizeof( pool->name ));

	return Mem_PoolIndex( pool );
}

poolhandle_t _Mem_AllocPool( const char *name, unsigned int flags, const char *filename, int fileline )
{
	for( size_t i = 0; i < poolcount; i++ )
	{
		if( poolchain[i].filename == NULL )
			return Mem_InitPool( &poolchain[i], name, flags, filename, fileline );
	}

	mempool_t *pool = (mempool_t *)Q_realloc( poolchain, sizeof( *poolchain ) * ( poolcount + 1 ));
	if( pool == NULL )
	{
		Sys_Error( "%s: out of memory (allocpool at %s:%i)\n", __func__, filename, fileline );
		return 0;
	}

	poolchain = pool;
	pool = &poolchain[poolcount++];
	return Mem_InitPool( pool, name, flags, filename, fileline );
}

void _Mem_EmptyPool( poolhandle_t poolptr, const char *filename, int fileline )
{
	if( unlikely( !poolptr ))
	{
		Sys_Error( "%s: pool == NULL (emptypool at %s:%i)\n", __func__, filename, fileline );
		return;
	}

	mempool_t *pool = Mem_FindPool( poolptr );
	if( !pool )
		return;

	while( pool->chain )
		Mem_FreeBlockBig( pool->chain, filename, fileline );

	while( pool->chain_small )
		Mem_FreeBlockSmall( pool->chain_small, filename, fileline );
}

void _Mem_FreePool( poolhandle_t *poolptr, const char *filename, int fileline )
{
	if( !*poolptr )
		return;

	mempool_t *pool = Mem_FindPool( *poolptr );
	if( !pool )
		return;

	if( !pool->filename )
	{
		Sys_Error( "%s: pool already freed (freepool at %s:%i)\n", __func__, filename, fileline );
		*poolptr = 0;
		return;
	}

	_Mem_EmptyPool( *poolptr, filename, fileline );

	memset( pool, 0xBF, sizeof( mempool_t ));
	pool->chain = NULL;
	pool->chain_small = NULL;
	pool->filename = NULL;
	*poolptr = 0;
}

static qboolean Mem_CheckAlloc( mempool_t *pool, void *data )
{
	if( pool )
	{
		memheader_t *target_big = (memheader_t *)((byte *)data - sizeof( memheader_t ));
		memheader_small_t *target_small = (memheader_small_t *)((byte *)data - sizeof( memheader_small_t ));

		for( memheader_t *header = pool->chain; header; header = header->next )
		{
			if( header == target_big )
				return true;
		}

		for( memheader_small_t *header = pool->chain_small; header; header = header->next )
		{
			if( header == target_small )
				return true;
		}
	}
	else
	{
		size_t i;
		for( i = 0, pool = poolchain; i < poolcount; i++, pool++ )
		{
			if( Mem_CheckAlloc( pool, data ))
				return true;
		}
	}
	return false;
}

qboolean Mem_IsAllocatedExt( poolhandle_t poolptr, void *data )
{
	mempool_t *pool = NULL;

	if( poolptr )
		pool = Mem_FindPool( poolptr );

	return Mem_CheckAlloc( pool, data );
}

void _Mem_Check( const char *filename, int fileline )
{
	mempool_t *pool;
	size_t i;

	for( i = 0, pool = poolchain; i < poolcount; i++, pool++ )
	{
		for( memheader_t *mem = pool->chain; mem; mem = mem->next )
			Mem_CheckAllocHeaderBig( __func__, mem, filename, fileline );

		for( memheader_small_t *mem = pool->chain_small; mem; mem = mem->next )
			Mem_CheckAllocHeaderSmall( __func__, mem, filename, fileline );
	}
}

void Mem_PrintStats( void )
{
	size_t count = 0, size = 0, realsize = 0, i;
	mempool_t *pool;

	Mem_Check();
	for( i = 0, pool = poolchain; i < poolcount; i++, pool++ )
	{
		if( !pool->filename )
			continue;

		count++;
		size += pool->totalsize;
		realsize += pool->realsize;
	}

	Con_Printf( "^3%zu^7 memory pools, totalling: ^1%s\n", count, Q_memprint( size ));
	Con_Printf( "total allocated size: ^1%s\n", Q_memprint( realsize ));
}

static void Mem_PrintList( size_t minallocationsize )
{
	mempool_t *pool;
	size_t i;

	Mem_Check();

	Con_Printf( "memory pool list:\n" );
	Con_Printf( "\t^3size\t\t\t\tname\n");
	for( i = 0, pool = poolchain; i < poolcount; i++, pool++ )
	{
		long changed_size = (long)pool->totalsize - (long)pool->lastchecksize;

		if( !pool->filename )
			continue;

		if( pool->lastchecksize != 0 && changed_size != 0 )
		{
			char sign = (changed_size < 0) ? '-' : '+';

			Con_Printf( "%10s (%10s real)\t%s (^7%c%s change)\n", Q_memprint( pool->totalsize ), Q_memprint( pool->realsize ),
				pool->name, sign, Q_memprint( abs( changed_size )));
		}
		else
		{
			Con_Printf( "%10s (%10s real)\t%s\n", Q_memprint( pool->totalsize ), Q_memprint( pool->realsize ), pool->name );
		}

		pool->lastchecksize = pool->totalsize;

		for( memheader_t *mem = pool->chain; mem; mem = mem->next )
		{
			if( mem->size >= minallocationsize )
				Con_Printf( "%10s allocated at %s:%i\n", Q_memprint( mem->size ), mem->filename, mem->fileline );
		}

		for( memheader_small_t *mem = pool->chain_small; mem; mem = mem->next )
		{
			if( mem->size >= minallocationsize )
				Con_Printf( "%10s allocated at <small>\n", Q_memprint( mem->size ));
		}
	}
}

/*
===============
Mem_Stats_f
===============
*/
void Mem_Stats_f( void )
{
	switch( Cmd_Argc( ))
	{
	case 1:
		Mem_PrintList( 1<<30 );
		Mem_PrintStats();
		break;
	case 2:
		Mem_PrintList( Q_atoi( Cmd_Argv( 1 )) * 1024 );
		Mem_PrintStats();
		break;
	default:
		Con_Printf( S_USAGE "memlist <all>\n" );
		break;
	}
}

/*
========================
Memory_Init
========================
*/
void Memory_Init( void )
{
	if( poolchain )
		Q_free( poolchain );

	poolchain = NULL;
	poolcount = 0;
}
