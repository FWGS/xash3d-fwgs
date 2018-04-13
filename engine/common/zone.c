/*
zone.c - zone memory allocation from DarkPlaces
Copyright (C) 2007 Uncle Mike

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

#define MEMUNIT		8		// smallest unit we care about is this many bytes
#define MEMCLUMPSIZE	(65536 - 1536)	// give malloc padding so we can't waste most of a page at the end
#define MEMBITS		(MEMCLUMPSIZE / MEMUNIT)
#define MEMBITINTS		(MEMBITS / 32)

#define MEMCLUMP_SENTINEL	0xABADCAFE
#define MEMHEADER_SENTINEL1	0xDEADF00D
#define MEMHEADER_SENTINEL2	0xDF

typedef struct memheader_s
{
	struct memheader_s	*next;		// next and previous memheaders in chain belonging to pool
	struct memheader_s	*prev;
	struct mempool_s	*pool;		// pool this memheader belongs to
	struct memclump_s	*clump;		// clump this memheader lives in, NULL if not in a clump
	size_t		size;		// size of the memory after the header (excluding header and sentinel2)
	const char	*filename;	// file name and line where Mem_Alloc was called
	uint		fileline;
	uint		sentinel1;	// should always be MEMHEADER_SENTINEL1

	// immediately followed by data, which is followed by a MEMHEADER_SENTINEL2 byte
} memheader_t;

typedef struct memclump_s
{
	byte		block[MEMCLUMPSIZE];// contents of the clump
	uint		sentinel1;	// should always be MEMCLUMP_SENTINEL
	int		bits[MEMBITINTS];	// if a bit is on, it means that the MEMUNIT bytes it represents are allocated, otherwise free
	uint		sentinel2;	// should always be MEMCLUMP_SENTINEL
	size_t		blocksinuse;	// if this drops to 0, the clump is freed
	size_t		largestavailable;	// largest block of memory available
	struct memclump_s	*chain;		// next clump in the chain
} memclump_t;

typedef struct mempool_s
{
	uint		sentinel1;	// should always be MEMHEADER_SENTINEL1
	struct memheader_s	*chain;		// chain of individual memory allocations
	struct memclump_s	*clumpchain;	// chain of clumps (if any)
	size_t		totalsize;	// total memory allocated in this pool (inside memheaders)
	size_t		realsize;		// total memory allocated in this pool (actual malloc total)
	size_t		lastchecksize;	// updated each time the pool is displayed by memlist
	struct mempool_s	*next;		// linked into global mempool list
	const char	*filename;	// file name and line where Mem_AllocPool was called
	int		fileline;
	char		name[64];		// name of the pool
	uint		sentinel2;	// should always be MEMHEADER_SENTINEL1
} mempool_t;

mempool_t *poolchain = NULL; // critical stuff

void *_Mem_Alloc( byte *poolptr, size_t size, const char *filename, int fileline )
{
	int		i, j, k, needed, endbit, largest;
	memclump_t	*clump, **clumpchainpointer;
	memheader_t	*mem;
	mempool_t		*pool = (mempool_t *)poolptr;

	if( size <= 0 ) return NULL;
	if( poolptr == NULL ) Sys_Error( "Mem_Alloc: pool == NULL (alloc at %s:%i)\n", filename, fileline );
	pool->totalsize += size;

	if( size < 4096 )
	{
		// clumping
		needed = ( sizeof( memheader_t ) + size + sizeof( int ) + (MEMUNIT - 1)) / MEMUNIT;
		endbit = MEMBITS - needed;
		for( clumpchainpointer = &pool->clumpchain; *clumpchainpointer; clumpchainpointer = &(*clumpchainpointer)->chain )
		{
			clump = *clumpchainpointer;
			if( clump->sentinel1 != MEMCLUMP_SENTINEL )
				Sys_Error( "Mem_Alloc: trashed clump sentinel 1 (alloc at %s:%d)\n", filename, fileline );
			if( clump->sentinel2 != MEMCLUMP_SENTINEL )
				Sys_Error( "Mem_Alloc: trashed clump sentinel 2 (alloc at %s:%d)\n", filename, fileline );
			if( clump->largestavailable >= needed )
			{
				largest = 0;
				for( i = 0; i < endbit; i++ )
				{
					if( clump->bits[i>>5] & (1 << (i & 31)))
						continue;
					k = i + needed;
					for( j = i; i < k; i++ )
						if( clump->bits[i>>5] & (1 << (i & 31)))
							goto loopcontinue;
					goto choseclump;
loopcontinue:;
					if( largest < j - i )
						largest = j - i;
				}
				// since clump falsely advertised enough space (nothing wrong
				// with that), update largest count to avoid wasting time in
				// later allocations
				clump->largestavailable = largest;
			}
		}

		pool->realsize += sizeof( memclump_t );
		clump = malloc( sizeof( memclump_t ));
		if( clump == NULL ) Sys_Error( "Mem_Alloc: out of memory (alloc at %s:%i)\n", filename, fileline );
		memset( clump, 0, sizeof( memclump_t ));
		*clumpchainpointer = clump;
		clump->sentinel1 = MEMCLUMP_SENTINEL;
		clump->sentinel2 = MEMCLUMP_SENTINEL;
		clump->chain = NULL;
		clump->blocksinuse = 0;
		clump->largestavailable = MEMBITS - needed;
		j = 0;
choseclump:
		mem = (memheader_t *)((byte *)clump->block + j * MEMUNIT );
		mem->clump = clump;
		clump->blocksinuse += needed;

		for( i = j + needed; j < i; j++ )
			clump->bits[j >> 5] |= (1 << (j & 31));
	}
	else
	{
		// big allocations are not clumped
		pool->realsize += sizeof( memheader_t ) + size + sizeof( int );
		mem = (memheader_t *)malloc( sizeof( memheader_t ) + size + sizeof( int ));
		if( mem == NULL ) Sys_Error( "Mem_Alloc: out of memory (alloc at %s:%i)\n", filename, fileline );
		mem->clump = NULL;
	}

	mem->filename = filename;
	mem->fileline = fileline;
	mem->size = size;
	mem->pool = pool;
	mem->sentinel1 = MEMHEADER_SENTINEL1;
	// we have to use only a single byte for this sentinel, because it may not be aligned
	// and some platforms can't use unaligned accesses
	*((byte *)mem + sizeof( memheader_t ) + mem->size ) = MEMHEADER_SENTINEL2;
	// append to head of list
	mem->next = pool->chain;
	mem->prev = NULL;
	pool->chain = mem;
	if( mem->next ) mem->next->prev = mem;
	memset((void *)((byte *)mem + sizeof( memheader_t )), 0, mem->size );

	return (void *)((byte *)mem + sizeof( memheader_t ));
}

static const char *Mem_CheckFilename( const char *filename )
{
	static const char	*dummy = "<corrupted>\0";
	const char	*out = filename;
	int		i;

	if( !out ) return dummy;
	for( i = 0; i < 128; i++, out++ )
		if( out == '\0' ) break; // valid name
	if( i == 128 ) return dummy;
	return filename;
}

static void Mem_FreeBlock( memheader_t *mem, const char *filename, int fileline )
{
	int		i, firstblock, endblock;
	memclump_t	*clump, **clumpchainpointer;
	mempool_t		*pool;

	if( mem->sentinel1 != MEMHEADER_SENTINEL1 )
	{
		mem->filename = Mem_CheckFilename( mem->filename ); // make sure what we don't crash var_args
		Sys_Error( "Mem_Free: trashed header sentinel 1 (alloc at %s:%i, free at %s:%i)\n", mem->filename, mem->fileline, filename, fileline );
	}

	if( *((byte *)mem + sizeof( memheader_t ) + mem->size ) != MEMHEADER_SENTINEL2 )
	{	
		mem->filename = Mem_CheckFilename( mem->filename ); // make sure what we don't crash var_args
		Sys_Error( "Mem_Free: trashed header sentinel 2 (alloc at %s:%i, free at %s:%i)\n", mem->filename, mem->fileline, filename, fileline );
	}

	pool = mem->pool;
	// unlink memheader from doubly linked list
	if(( mem->prev ? mem->prev->next != mem : pool->chain != mem ) || ( mem->next && mem->next->prev != mem ))
		Sys_Error( "Mem_Free: not allocated or double freed (free at %s:%i)\n", filename, fileline );

	if( mem->prev ) mem->prev->next = mem->next;
	else pool->chain = mem->next;

	if( mem->next )
		mem->next->prev = mem->prev;

	// memheader has been unlinked, do the actual free now
	pool->totalsize -= mem->size;

	if(( clump = mem->clump ) != NULL )
	{
		if( clump->sentinel1 != MEMCLUMP_SENTINEL )
			Sys_Error( "Mem_Free: trashed clump sentinel 1 (free at %s:%i)\n", filename, fileline );
		if( clump->sentinel2 != MEMCLUMP_SENTINEL )
			Sys_Error( "Mem_Free: trashed clump sentinel 2 (free at %s:%i)\n", filename, fileline );
		firstblock = ((byte *)mem - (byte *)clump->block );
		if( firstblock & ( MEMUNIT - 1 ))
			Sys_Error( "Mem_Free: address not valid in clump (free at %s:%i)\n", filename, fileline );
		firstblock /= MEMUNIT;
		endblock = firstblock + ((sizeof( memheader_t ) + mem->size + sizeof( int ) + (MEMUNIT - 1)) / MEMUNIT );
		clump->blocksinuse -= endblock - firstblock;

		// could use &, but we know the bit is set
		for( i = firstblock; i < endblock; i++ )
			clump->bits[i >> 5] -= (1 << (i & 31));
		if( clump->blocksinuse <= 0 )
		{
			// unlink from chain
			for( clumpchainpointer = &pool->clumpchain; *clumpchainpointer; clumpchainpointer = &(*clumpchainpointer)->chain )
			{
				if (*clumpchainpointer == clump)
				{
					*clumpchainpointer = clump->chain;
					break;
				}
			}

			pool->realsize -= sizeof( memclump_t );
			memset( clump, 0xBF, sizeof( memclump_t ));
			free( clump );
		}
		else
		{
			// clump still has some allocations
			// force re-check of largest available space on next alloc
			clump->largestavailable = MEMBITS - clump->blocksinuse;
		}
	}
	else
	{
		pool->realsize -= sizeof( memheader_t ) + mem->size + sizeof( int );
		free( mem );
	}
}

void _Mem_Free( void *data, const char *filename, int fileline )
{
	if( data == NULL ) Sys_Error( "Mem_Free: data == NULL (called at %s:%i)\n", filename, fileline );
	Mem_FreeBlock((memheader_t *)((byte *)data - sizeof( memheader_t )), filename, fileline );
}

void *_Mem_Realloc( byte *poolptr, void *memptr, size_t size, const char *filename, int fileline )
{
	memheader_t	*memhdr = NULL;
	char		*nb;

	if( size <= 0 ) return memptr; // no need to reallocate

	if( memptr )
	{
		memhdr = (memheader_t *)((byte *)memptr - sizeof( memheader_t ));
		if( size == memhdr->size ) return memptr;
	}

	nb = _Mem_Alloc( poolptr, size, filename, fileline );

	if( memptr ) // first allocate?
	{ 
		size_t newsize = memhdr->size < size ? memhdr->size : size; // upper data can be trucnated!
		memcpy( nb, memptr, newsize );
		_Mem_Free( memptr, filename, fileline ); // free unused old block
          }

	return (void *)nb;
}

byte *_Mem_AllocPool( const char *name, const char *filename, int fileline )
{
	mempool_t *pool;

	pool = (mempool_t *)malloc( sizeof( mempool_t ));
	if( pool == NULL ) Sys_Error( "Mem_AllocPool: out of memory (allocpool at %s:%i)\n", filename, fileline );
	memset( pool, 0, sizeof( mempool_t ));

	// fill header
	pool->sentinel1 = MEMHEADER_SENTINEL1;
	pool->sentinel2 = MEMHEADER_SENTINEL1;
	pool->filename = filename;
	pool->fileline = fileline;
	pool->chain = NULL;
	pool->totalsize = 0;
	pool->realsize = sizeof( mempool_t );
	Q_strncpy( pool->name, name, sizeof( pool->name ));
	pool->next = poolchain;
	poolchain = pool;

	return (byte *)pool;
}

void _Mem_FreePool( byte **poolptr, const char *filename, int fileline )
{
	mempool_t	*pool = (mempool_t *)*poolptr;
	mempool_t	**chainaddress;
          
	if( pool )
	{
		// unlink pool from chain
		for( chainaddress = &poolchain; *chainaddress && *chainaddress != pool; chainaddress = &((*chainaddress)->next));
		if( *chainaddress != pool ) Sys_Error( "Mem_FreePool: pool already free (freepool at %s:%i)\n", filename, fileline );
		if( pool->sentinel1 != MEMHEADER_SENTINEL1 ) Sys_Error( "Mem_FreePool: trashed pool sentinel 1 (allocpool at %s:%i, freepool at %s:%i)\n", pool->filename, pool->fileline, filename, fileline );
		if( pool->sentinel2 != MEMHEADER_SENTINEL1 ) Sys_Error( "Mem_FreePool: trashed pool sentinel 2 (allocpool at %s:%i, freepool at %s:%i)\n", pool->filename, pool->fileline, filename, fileline );
		*chainaddress = pool->next;

		// free memory owned by the pool
		while( pool->chain ) Mem_FreeBlock( pool->chain, filename, fileline );
		// free the pool itself
		memset( pool, 0xBF, sizeof( mempool_t ));
		free( pool );
		*poolptr = NULL;
	}
}

void _Mem_EmptyPool( byte *poolptr, const char *filename, int fileline )
{
	mempool_t *pool = (mempool_t *)poolptr;
	if( poolptr == NULL ) Sys_Error( "Mem_EmptyPool: pool == NULL (emptypool at %s:%i)\n", filename, fileline );

	if( pool->sentinel1 != MEMHEADER_SENTINEL1 ) Sys_Error( "Mem_EmptyPool: trashed pool sentinel 1 (allocpool at %s:%i, emptypool at %s:%i)\n", pool->filename, pool->fileline, filename, fileline );
	if( pool->sentinel2 != MEMHEADER_SENTINEL1 ) Sys_Error( "Mem_EmptyPool: trashed pool sentinel 2 (allocpool at %s:%i, emptypool at %s:%i)\n", pool->filename, pool->fileline, filename, fileline );

	// free memory owned by the pool
	while( pool->chain ) Mem_FreeBlock( pool->chain, filename, fileline );
}

qboolean Mem_CheckAlloc( mempool_t *pool, void *data )
{
	memheader_t *header, *target;

	if( pool )
	{
		// search only one pool
		target = (memheader_t *)((byte *)data - sizeof( memheader_t ));
		for( header = pool->chain; header; header = header->next )
			if( header == target ) return true;
	}
	else
	{
		// search all pools
		for( pool = poolchain; pool; pool = pool->next )
			if( Mem_CheckAlloc( pool, data ))
				return true;
	}
	return false;
}

/*
========================
Check pointer for memory
========================
*/
qboolean Mem_IsAllocatedExt( byte *poolptr, void *data )
{
	mempool_t	*pool = NULL;
	if( poolptr ) pool = (mempool_t *)poolptr;

	return Mem_CheckAlloc( pool, data );
}

void Mem_CheckHeaderSentinels( void *data, const char *filename, int fileline )
{
	memheader_t *mem;

	if( data == NULL )
		Sys_Error( "Mem_CheckSentinels: data == NULL (sentinel check at %s:%i)\n", filename, fileline );

	mem = (memheader_t *)((byte *) data - sizeof(memheader_t));

	if( mem->sentinel1 != MEMHEADER_SENTINEL1 )
	{
		mem->filename = Mem_CheckFilename( mem->filename ); // make sure what we don't crash var_args
		Sys_Error( "Mem_CheckSentinels: trashed header sentinel 1 (block allocated at %s:%i, sentinel check at %s:%i)\n", mem->filename, mem->fileline, filename, fileline );
	}

	if( *((byte *) mem + sizeof(memheader_t) + mem->size) != MEMHEADER_SENTINEL2 )
	{	
		mem->filename = Mem_CheckFilename( mem->filename ); // make sure what we don't crash var_args
		Sys_Error( "Mem_CheckSentinels: trashed header sentinel 2 (block allocated at %s:%i, sentinel check at %s:%i)\n", mem->filename, mem->fileline, filename, fileline );
	}
}

static void Mem_CheckClumpSentinels( memclump_t *clump, const char *filename, int fileline )
{
	// this isn't really very useful
	if( clump->sentinel1 != MEMCLUMP_SENTINEL )
		Sys_Error( "Mem_CheckClumpSentinels: trashed sentinel 1 (sentinel check at %s:%i)\n", filename, fileline );
	if( clump->sentinel2 != MEMCLUMP_SENTINEL )
		Sys_Error( "Mem_CheckClumpSentinels: trashed sentinel 2 (sentinel check at %s:%i)\n", filename, fileline );
}

void _Mem_Check( const char *filename, int fileline )
{
	memheader_t	*mem;
	mempool_t		*pool;
	memclump_t	*clump;

	for( pool = poolchain; pool; pool = pool->next )
	{
		if( pool->sentinel1 != MEMHEADER_SENTINEL1 )
			Sys_Error( "Mem_CheckSentinelsGlobal: trashed pool sentinel 1 (allocpool at %s:%i, sentinel check at %s:%i)\n", pool->filename, pool->fileline, filename, fileline );
		if( pool->sentinel2 != MEMHEADER_SENTINEL1 )
			Sys_Error( "Mem_CheckSentinelsGlobal: trashed pool sentinel 2 (allocpool at %s:%i, sentinel check at %s:%i)\n", pool->filename, pool->fileline, filename, fileline );
	}

	for( pool = poolchain; pool; pool = pool->next )
		for( mem = pool->chain; mem; mem = mem->next )
			Mem_CheckHeaderSentinels((void *)((byte *) mem + sizeof(memheader_t)), filename, fileline );

	for( pool = poolchain; pool; pool = pool->next )
		for( clump = pool->clumpchain; clump; clump = clump->chain )
			Mem_CheckClumpSentinels( clump, filename, fileline );
}

void Mem_PrintStats( void )
{
	size_t	count = 0, size = 0, realsize = 0;
	mempool_t	*pool;

	Mem_Check();
	for( pool = poolchain; pool; pool = pool->next )
	{
		count++;
		size += pool->totalsize;
		realsize += pool->realsize;
	}

	Con_Printf( "^3%lu^7 memory pools, totalling: ^1%s\n", (dword)count, Q_memprint( size ));
	Con_Printf( "total allocated size: ^1%s\n", Q_memprint( realsize ));
}

void Mem_PrintList( size_t minallocationsize )
{
	mempool_t		*pool;
	memheader_t	*mem;

	Mem_Check();

	Con_Printf( "memory pool list:\n""  ^3size                          name\n");
	for( pool = poolchain; pool; pool = pool->next )
	{
		long	changed_size = (long)pool->totalsize - (long)pool->lastchecksize;

		// poolnames can contain color symbols, make sure what color is reset
		if( changed_size != 0 )
		{
			char	sign = (changed_size < 0) ? '-' : '+';

			Con_Printf( "%10s (%10s actual) %s (^7%c%s change)\n", Q_memprint( pool->totalsize ), Q_memprint( pool->realsize ),
			pool->name, sign, Q_memprint( abs( changed_size )));
		}
		else
		{
			Con_Printf( "%5s (%5s actual) %s\n", Q_memprint( pool->totalsize ), Q_memprint( pool->realsize ), pool->name );
		}

		pool->lastchecksize = pool->totalsize;
		for( mem = pool->chain; mem; mem = mem->next )
			if( mem->size >= minallocationsize )
				Con_Printf( "%10s allocated at %s:%i\n", Q_memprint( mem->size ), mem->filename, mem->fileline );
	}
}

/*
========================
Memory_Init
========================
*/
void Memory_Init( void )
{
	poolchain = NULL; // init mem chain
}