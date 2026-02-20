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

#define MEMHEADER_SENTINEL1	0xA1BAU
#define MEMHEADER_SENTINEL2	0xDFU

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

// keep this structure as compact as possible while keeping it aligned
// on ILP32 it's 24 bytes, which is aligned to 8 byte boundary
// on LP64 it's 40 bytes, which is also aligned to 8 byte boundary
typedef struct memheader_s
{
	struct memheader_s *next, *prev; // next and previous memheaders in chain belonging to pool
	const char         *filename;    // file name and line where Mem_Alloc was called
	size_t             size;         // size of the memory after the header (excluding header and sentinel2)
	poolhandle_t       poolptr;      // pool this memheader belongs to
	uint16_t           fileline;
	uint16_t           sentinel1;    // must be equal to MEMHEADER_SENTINEL1
	// immediately followed by data, which is followed by a MEMHEADER_SENTINEL2 byte
} memheader_t;

STATIC_CHECK_SIZEOF( memheader_t, 24, 40 );

typedef struct mempool_s
{
	struct memheader_s *chain;        // chain of individual memory allocations
	size_t             totalsize;     // total memory allocated in this pool (inside memheaders)
	size_t             realsize;      // total memory allocated in this pool (actual malloc total)
	size_t             lastchecksize; // updated each time the pool is displayed by memlist
	const char         *filename;     // file name and line where Mem_AllocPool was called
	int                fileline;
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

static poolhandle_t Mem_PoolIndex( mempool_t *mempool )
{
	return (poolhandle_t)(mempool - poolchain) + 1;
}

static inline void Mem_PoolAdd( mempool_t *pool, size_t size )
{
	pool->totalsize += size;
	pool->realsize += sizeof( memheader_t ) + size + sizeof( byte );
}

static inline void Mem_PoolSubtract( mempool_t *pool, size_t size )
{
	pool->totalsize -= size;
	pool->realsize -= sizeof( memheader_t ) + size + sizeof( byte );
}

static inline void Mem_PoolLinkAlloc( mempool_t *pool, memheader_t *mem )
{
	mem->next = pool->chain;
	if( mem->next ) mem->next->prev = mem;
	pool->chain = mem;
	mem->prev = NULL;
	mem->poolptr = Mem_PoolIndex( pool );
}

static inline void Mem_PoolUnlinkAlloc( mempool_t *pool, memheader_t *mem )
{
	if( mem->next ) mem->next->prev = mem->prev;
	if( mem->prev ) mem->prev->next = mem->next;
	else pool->chain = mem->next;
	mem->poolptr = 0;
}

static inline void Mem_InitAlloc( memheader_t *mem, size_t size, const char *filename, int fileline )
{
	mem->size = size;
	mem->filename = filename;
	mem->fileline = fileline;
	mem->sentinel1 = MEMHEADER_SENTINEL1;
	*((byte *)mem + sizeof( memheader_t ) + mem->size ) = MEMHEADER_SENTINEL2;
}

static const char *Mem_CheckFilename( const char *filename )
{
	static const char *dummy = "<corrupted>\0";

	if( COM_StringEmptyOrNULL( filename ))
		return dummy;

	if( memchr( filename, '\0', MAX_OSPATH ) != NULL )
		return filename;

	return dummy;
}

static qboolean Mem_CheckAllocHeader( const char *func, const memheader_t *mem, const char *filename, int fileline )
{
	const char *memfilename;

	if( mem->sentinel1 != MEMHEADER_SENTINEL1 )
	{
		memfilename = Mem_CheckFilename( mem->filename );
		Sys_Error( "%s: trashed header sentinel 1 (alloc at %s:%i, check at %s:%i)\n", func, memfilename, mem->fileline, filename, fileline );
		return false;
	}

	if( *((byte *)mem + sizeof( memheader_t ) + mem->size ) != MEMHEADER_SENTINEL2 )
	{
		memfilename = Mem_CheckFilename( mem->filename ); // make sure what we don't crash var_args
		Sys_Error( "%s: trashed header sentinel 2 (alloc at %s:%i, check at %s:%i)\n", func, memfilename, mem->fileline, filename, fileline );
		return false;
	}

	return true;
}

void *_Mem_Alloc( poolhandle_t poolptr, size_t size, qboolean clear, const char *filename, int fileline )
{
	memheader_t *mem;
	mempool_t   *pool;

	if( size <= 0 )
		return NULL;

	if( unlikely( !poolptr ))
	{
		Sys_Error( "%s: pool == NULL (alloc at %s:%i)\n", __func__, filename, fileline );
		return NULL;
	}

	pool = Mem_FindPool( poolptr );
	if( !pool )
		return NULL;

	mem = (memheader_t *)Q_malloc( sizeof( memheader_t ) + size + sizeof( byte ));
	if( mem == NULL )
	{
		Sys_Error( "%s: out of memory (alloc size %s at %s:%i)\n", __func__, Q_memprint( size ), filename, fileline );
		return NULL;
	}

	Mem_InitAlloc( mem, size, filename, fileline );

	Mem_PoolAdd( pool, size );
	Mem_PoolLinkAlloc( pool, mem );

	if( clear )
		memset((void *)((byte *)mem + sizeof( memheader_t )), 0, mem->size );

	return (void *)((byte *)mem + sizeof( memheader_t ));
}

static void Mem_FreeBlock( memheader_t *mem, const char *filename, int fileline )
{
	mempool_t		*pool;

	if( !Mem_CheckAllocHeader( __func__, mem, filename, fileline ))
		return;

	pool = Mem_FindPool( mem->poolptr );
	if( !pool )
		return;

	// unlink memheader from doubly linked list
	if(( mem->prev ? mem->prev->next != mem : pool->chain != mem ) || ( mem->next && mem->next->prev != mem ))
	{
		Sys_Error( "%s: not allocated or double freed (free at %s:%i)\n", __func__, filename, fileline );
		return;
	}

	Mem_PoolSubtract( pool, mem->size );
	Mem_PoolUnlinkAlloc( pool, mem );

	Q_free( mem );
}

void _Mem_Free( void *data, const char *filename, int fileline )
{
	if( data == NULL )
		return;

	Mem_FreeBlock((memheader_t *)((byte *)data - sizeof( memheader_t )), filename, fileline );
}

static void Mem_MigratePool( poolhandle_t newpoolptr, memheader_t *mem, const char *filename, int fileline )
{
	mempool_t *oldpool = Mem_FindPool( mem->poolptr );
	mempool_t *newpool = Mem_FindPool( newpoolptr );

	// dettach allocation from one pool and reattach it to new pool
	// might be made into public function at some point

	Mem_PoolUnlinkAlloc( oldpool, mem );
	Mem_PoolSubtract( oldpool, mem->size );

	Mem_PoolLinkAlloc( newpool, mem );
	Mem_PoolAdd( newpool, mem->size );
}

void *_Mem_Realloc( poolhandle_t poolptr, void *data, size_t size, qboolean clear, const char *filename, int fileline )
{
	memheader_t *mem;
	uintptr_t oldmem;
	mempool_t *pool;
	size_t oldsize;

	if( size <= 0 )
		return data; // no need to reallocate

	if( unlikely( !poolptr ))
	{
		Sys_Error( "%s: pool == NULL (alloc at %s:%i)\n", __func__, filename, fileline );
		return NULL;
	}

	if( !data )
		return _Mem_Alloc( poolptr, size, clear, filename, fileline );

	mem = (memheader_t *)((byte *)data - sizeof( memheader_t ));

	if( !Mem_CheckAllocHeader( __func__, mem, filename, fileline ))
		return NULL;

	// migrate pool if requested, even if no reallocation needed
	if( mem->poolptr != poolptr )
		Mem_MigratePool( poolptr, mem, filename, fileline );

	oldsize = mem->size;
	if( size == oldsize )
		return data;

	pool = Mem_FindPool( poolptr );

	oldmem = (uintptr_t)mem;
	mem = Q_realloc( mem, sizeof( memheader_t ) + size + sizeof( byte ));

	if( mem == NULL )
	{
		Sys_Error( "%s: out of memory (alloc size %s at %s:%i)\n", __func__, Q_memprint( size ), filename, fileline );
		return NULL;
	}

	// Con_Printf( S_NOTE "%s: mem %s oldmem, size before %zu now %zu (alloc at %s:%i)\n",
	// __func__, (uintptr_t)mem != oldmem ? "!=" : "==", oldsize, size, filename, fileline );

	Mem_InitAlloc( mem, size, filename, fileline );

	if( size > oldsize )
	{
		Mem_PoolAdd( pool, size - oldsize );

		if( clear )
			memset((byte *)mem + sizeof( memheader_t ) + oldsize, 0, size - oldsize );
	}
	else Mem_PoolSubtract( pool, oldsize - size );

	if( oldmem != (uintptr_t)mem ) // just relink pointers
	{
		if( mem->next ) mem->next->prev = mem;
		if( mem->prev ) mem->prev->next = mem;
		else pool->chain = mem;
	}

	return (void *)((byte *)mem + sizeof( memheader_t ));
}

static poolhandle_t Mem_InitPool( mempool_t *pool, const char *name, const char *filename, int fileline )
{
	memset( pool, 0, sizeof( *pool ));

	// fill header
	pool->filename = filename;
	pool->fileline = fileline;
	pool->realsize = sizeof( mempool_t );
	Q_strncpy( pool->name, name, sizeof( pool->name ));

	return Mem_PoolIndex( pool );
}

poolhandle_t _Mem_AllocPool( const char *name, const char *filename, int fileline )
{
	mempool_t *pool;
	size_t i;

	for( i = 0, pool = poolchain; i < poolcount; i++, pool++ )
	{
		if( pool->filename == NULL )
			return Mem_InitPool( pool, name, filename, fileline );
	}

	pool = (mempool_t *)Q_realloc( poolchain, sizeof( *poolchain ) * ( poolcount + 1 ));
	if( pool == NULL )
	{
		Sys_Error( "%s: out of memory (allocpool at %s:%i)\n", __func__, filename, fileline );
		return 0;
	}

	poolchain = pool;
	pool = &poolchain[poolcount++];
	return Mem_InitPool( pool, name, filename, fileline );
}

void _Mem_FreePool( poolhandle_t *poolptr, const char *filename, int fileline )
{
	mempool_t	*pool;

	if( *poolptr && ( pool = Mem_FindPool( *poolptr )))
	{
		if( !pool->filename )
		{
			Sys_Error( "%s: pool already free (freepool at %s:%i)\n", __func__, filename, fileline );
			*poolptr = 0;
			return;
		}

		// free memory owned by the pool
		while( pool->chain )
			Mem_FreeBlock( pool->chain, filename, fileline );

		// free the pool itself
		memset( pool, 0xBF, sizeof( mempool_t ));
		pool->chain = NULL;
		pool->filename = NULL; // mark as reusable
		*poolptr = 0;
	}
}

void _Mem_EmptyPool( poolhandle_t poolptr, const char *filename, int fileline )
{
	mempool_t *pool;
	if( unlikely( !poolptr ))
	{
		Sys_Error( "%s: pool == NULL (emptypool at %s:%i)\n", __func__, filename, fileline );
		return;
	}

	pool = Mem_FindPool( poolptr );
	if( !pool )
		return;

	// free memory owned by the pool
	while( pool->chain ) Mem_FreeBlock( pool->chain, filename, fileline );
}

static qboolean Mem_CheckAlloc( mempool_t *pool, void *data )
{
	memheader_t *header, *target;

	if( pool )
	{
		// search only one pool
		target = (memheader_t *)((byte *)data - sizeof( memheader_t ));
		for( header = pool->chain; header; header = header->next )
		{
			if( header == target )
				return true;
		}
	}
	else
	{
		// search all pools
		size_t i;
		for( i = 0, pool = poolchain; i < poolcount; i++, pool++ )
		{
			if( Mem_CheckAlloc( pool, data ))
				return true;
		}
	}
	return false;
}

/*
========================
Check pointer for memory
========================
*/
qboolean Mem_IsAllocatedExt( poolhandle_t poolptr, void *data )
{
	mempool_t	*pool = NULL;

	if( poolptr )
		pool = Mem_FindPool( poolptr );

	return Mem_CheckAlloc( pool, data );
}

void _Mem_Check( const char *filename, int fileline )
{
	memheader_t *mem;
	mempool_t   *pool;
	size_t i;

	for( i = 0, pool = poolchain; i < poolcount; i++, pool++ )
		for( mem = pool->chain; mem; mem = mem->next )
			Mem_CheckAllocHeader( __func__, mem, filename, fileline );
}

void Mem_PrintStats( void )
{
	size_t    count = 0, size = 0, realsize = 0, i;
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

void Mem_PrintList( size_t minallocationsize )
{
	mempool_t		*pool;
	memheader_t	*mem;
	size_t i;

	Mem_Check();

	Con_Printf( "memory pool list:\n" );
	Con_Printf( "\t^3size\t\t\t\tname\n");
	for( i = 0, pool = poolchain; i < poolcount; i++, pool++ )
	{
		long	changed_size = (long)pool->totalsize - (long)pool->lastchecksize;

		if( !pool->filename )
			continue;

		// poolnames can contain color symbols, make sure what color is reset
		if( pool->lastchecksize != 0 && changed_size != 0 )
		{
			char	sign = (changed_size < 0) ? '-' : '+';

			Con_Printf( "%10s (%10s real)\t%s (^7%c%s change)\n", Q_memprint( pool->totalsize ), Q_memprint( pool->realsize ),
				pool->name, sign, Q_memprint( abs( changed_size )));
		}
		else
		{
			Con_Printf( "%10s (%10s real)\t%s\n", Q_memprint( pool->totalsize ), Q_memprint( pool->realsize ), pool->name );
		}

		pool->lastchecksize = pool->totalsize;
		for( mem = pool->chain; mem; mem = mem->next )
		{
			if( mem->size >= minallocationsize )
				Con_Printf( "%10s allocated at %s:%i\n", Q_memprint( mem->size ), mem->filename, mem->fileline );
		}
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
	{
		Q_free( poolchain );
	}
	poolchain = NULL; // init mem chain
	poolcount = 0;
}
