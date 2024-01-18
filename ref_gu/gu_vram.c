/*
 * Helper for use with the PSP Software Development Kit - http://www.pspdev.org
 * -----------------------------------------------------------------------
 * Licensed as 'free to use and modify as long as credited appropriately'
 *
 * vram.c - Standard C high performance VRAM allocation routines.
 *
 * Copyright (c) 2007 Alexander Berl 'Raphael' <raphael@fx-world.org>
 * http://wordpress.fx-world.org
 */
#include "gu_vram.h"
#include <stdio.h>
#include <pspge.h>
#include <stdlib.h>

#define _MBEINVRAM
//#define _DEBUG

// Configure the memory to be managed
static unsigned int	__mem_size = 0;
static unsigned int	__mem_start = 0;
static int		__mem_num_blocks = 0;

static unsigned int	*__mem_blocks;

static int		__largest_update = 0;
static int		__largest_block = 0;
static int		__mem_free = 0;

// Configure the block size the memory gets subdivided into (page size)
// __mem_size/__BLOCK_SIZE may not exceed 2^16-1 = 65535
// The block size also defines the alignment of allocations
// Larger block sizes perform better, because the blocktable is smaller and therefore fits better into cache
// however the overhead is also bigger and more memory is wasted
#define __BLOCK_SIZE			512
#define __BLOCKS(x)			(( x + __BLOCK_SIZE - 1 ) / __BLOCK_SIZE )
#define __BLOCKSIZE(x)			(( x + __BLOCK_SIZE - 1 ) & ~( __BLOCK_SIZE - 1 ))


// A MEMORY BLOCK ENTRY IS MADE UP LIKE THAT:
// bit:  31     30    29 - 15    14-0
//		free   block    prev     size
//
// bit 31: free bit, indicating if block is allocated or not
// bit 30: blocked bit, indicating if block is part of a larger block (0) - used for error resilience
// bit 29-15: block index of previous block
// bit 14- 0: size of current block
//
// This management can handle a max amount of 2^16-1 = 65535 blocks, which resolves to 64MB at blocksize of 1024 bytes
//
#define __BLOCK_GET_SIZE( x )		(( x & 0x7FFF ))
#define __BLOCK_GET_PREV( x )		(( x >> 15 ) & 0x7FFF )
#define __BLOCK_GET_FREE( x )		(( x >> 31 ))
#define __BLOCK_GET_BLOCK( x )		(( x >> 30) & 0x1 )
#define __BLOCK_SET_SIZE( x, y )	x = (( x & ~0x7FFF ) | (( y ) & 0x7FFF ))
#define __BLOCK_ADD_SIZE( x, y )	x = (( x & ~0x7FFF ) | ((( x & 0x7FFF ) + (( y ) & 0x7FFF )) & 0x7FFF ))
#define __BLOCK_SET_PREV( x, y )	x = (( x & ~0x3FFF8000 ) | ((( y ) & 0x7FFF ) << 15 ))
#define __BLOCK_SET_FREE( x, y )	x = (( x & 0x7FFFFFFF ) | ((( y ) & 0x1 ) << 31 ))
#define __BLOCK_SET_BLOCK( x, y )	x = (( x & 0xBFFFFFFF ) | ((( y ) & 0x1 ) << 30 ))
#define __BLOCK_MAKE( s, p, f, n )	((( f & 0x1 ) << 31 ) | (( n & 0x1 ) << 30 ) | ((( p ) & 0x7FFF ) << 15 ) | (( s ) & 0x7FFF ))
#define __BLOCK_GET_FREEBLOCK( x )	(( x >> 30 ) & 0x3 )		// returns 11b if block is a starting block and free, 10b if block is a starting block and allocated, 0xb if it is a non-starting block (don't change)
#define __BLOCK0			(( __mem_num_blocks ) | ( 1 << 31 ) | ( 1 << 30 ))

int vinit( void )
{
	if( __mem_start ) return 0;

	__mem_size = sceGeEdramGetSize();
	__mem_start = ( unsigned int )sceGeEdramGetAddr();
#ifdef _MBEINVRAM
	__mem_num_blocks = __mem_size / ( __BLOCK_SIZE + sizeof( unsigned int ));
	__mem_size = __mem_num_blocks * __BLOCK_SIZE;
	__mem_blocks = ( unsigned int* )( __mem_start + __mem_size ); // to end
#else
	__mem_num_blocks = __mem_size / __BLOCK_SIZE;
	__mem_blocks = ( unsigned int* )calloc( __mem_num_blocks, sizeof( unsigned int ));
	if( !__mem_blocks )
	{
		__mem_size = 0;
		__mem_start = 0;
		return -1;
	}
#endif
	__largest_block = __mem_num_blocks;
	__mem_free = __mem_num_blocks;

	return 0;
}


inline void* vrelptr( void *ptr )
{
	if( !__mem_start ) return NULL;
	return ( void* )(( unsigned int )ptr & ~__mem_start );
}

inline void* vabsptr( void *ptr )
{
	if( !__mem_start ) return NULL;
	return ( void* )(( unsigned int )ptr | __mem_start );
}


static void __find_largest_block( void )
{
	int i = 0;
	__largest_block = 0;
	while( i < __mem_num_blocks )
	{
		int csize = __BLOCK_GET_SIZE( __mem_blocks[i] );
		if( __BLOCK_GET_FREEBLOCK( __mem_blocks[i] ) == 3 && csize > __largest_block )
			__largest_block = csize;
		i += csize;
	}
	__largest_update = 0;
}

#ifdef _DEBUG
void __memwalk( void )
{
	int i = 0;
	if( !__mem_start ) return;
	if( __mem_blocks[0] == 0 ) __mem_blocks[0] = __BLOCK0;
	while( i < __mem_num_blocks )
	{
		printf( "BLOCK %i:\n", i );
		printf( "  free: %i\n", __BLOCK_GET_FREEBLOCK( __mem_blocks[i] ));
		printf( "  size: %i\n", __BLOCK_GET_SIZE( __mem_blocks[i] ));
		printf( "  prev: %i\n", __BLOCK_GET_PREV( __mem_blocks[i] ));
		i += __BLOCK_GET_SIZE( __mem_blocks[i] );
	}
}
#endif

void* valloc( size_t size )
{
	if( !__mem_start ) return NULL;

	// Initialize memory block, if not yet done
	if( __mem_blocks[0] == 0 ) __mem_blocks[0] = __BLOCK0;

	int i = 0;
	int j = 0;
	int bsize = __BLOCKS( size );

	if( __largest_update == 0 && __largest_block < bsize )
	{
#ifdef _DEBUG
		printf( "Not enough memory to allocate %i bytes (largest: %i)!\n", size, vlargestblock());
#endif
		return( 0 );
	}

#ifdef _DEBUG
	printf( "allocating %i bytes, in %i blocks\n", size, bsize );
#endif
	// Find smallest block that still fits the requested size
	int bestblock = -1;
	int bestblock_prev = 0;
	int bestblock_size = __mem_num_blocks + 1;
	while( i < __mem_num_blocks )
	{
		int csize = __BLOCK_GET_SIZE( __mem_blocks[i] );
		if( __BLOCK_GET_FREEBLOCK( __mem_blocks[i] ) == 3 && csize >= bsize )
		{
			if( csize < bestblock_size )
			{
				bestblock = i;
				bestblock_prev = j;
				bestblock_size = csize;
			}

			if( csize == bsize )
				break;
		}
		j = i;
		i += csize;
	}

	if( bestblock < 0 )
	{
#ifdef _DEBUG
		printf( "Not enough memory to allocate %i bytes (largest: %i)!\n", size, vlargestblock());
#endif
		return( 0 );
	}

	i = bestblock;
	j = bestblock_prev;
	int csize = bestblock_size;
	__mem_blocks[i] = __BLOCK_MAKE( bsize, j, 0, 1 );

	int next = i + bsize;
	if( csize > bsize && next < __mem_num_blocks )
	{
		__mem_blocks[next] = __BLOCK_MAKE( csize - bsize, i, 1, 1 );
		int nextnext = i + csize;
		if ( nextnext < __mem_num_blocks )
		{
			__BLOCK_SET_PREV( __mem_blocks[nextnext], next );
		}
	}

	__mem_free -= bsize;
	if( __largest_block == csize )		// if we just allocated from one of the largest blocks
	{
		if(( csize - bsize ) > ( __mem_free / 2 ))
			__largest_block = ( csize - bsize );		// there can't be another largest block
		else
			__largest_update = 1;
	}
	return (( void* )( __mem_start + ( i * __BLOCK_SIZE )));
}


void vfree( void* ptr )
{
	if( ptr == 0 ) return;

	int block = (( unsigned int )ptr - __mem_start ) / __BLOCK_SIZE;
	if( block < 0 || block > __mem_num_blocks )
	{
#ifdef _DEBUG
		printf( "Block is out of range: %i (0x%x)\n", block, (int)ptr );
#endif
		return;
	}
	int csize = __BLOCK_GET_SIZE( __mem_blocks[block] );
#ifdef _DEBUG
	printf("freeing block %i (0x%x), size: %i\n", block, (int)ptr, csize);
#endif

	if( __BLOCK_GET_FREEBLOCK( __mem_blocks[block] ) != 1 || csize == 0 )
	{
#ifdef _DEBUG
		printf( "Block was not allocated!\n" );
#endif
		return;
	}

	// Mark block as free
	__BLOCK_SET_FREE( __mem_blocks[block], 1 );
	__mem_free += csize;

	int next = block + csize;
	// Merge with previous block if possible
	int prev = __BLOCK_GET_PREV( __mem_blocks[block] );
	if( prev < block )
	{
		if ( __BLOCK_GET_FREEBLOCK( __mem_blocks[prev] ) == 3 )
		{
			__BLOCK_ADD_SIZE( __mem_blocks[prev], csize );
			__BLOCK_SET_BLOCK( __mem_blocks[block], 0 );	// mark current block as inter block
			if ( next < __mem_num_blocks )
				__BLOCK_SET_PREV( __mem_blocks[next], prev );
			block = prev;
		}
	}

	// Merge with next block if possible
	if( next < __mem_num_blocks )
	{
		if ( __BLOCK_GET_FREEBLOCK( __mem_blocks[next] ) == 3 )
		{
			__BLOCK_ADD_SIZE( __mem_blocks[block], __BLOCK_GET_SIZE( __mem_blocks[next] ));
			__BLOCK_SET_BLOCK( __mem_blocks[next], 0 );	// mark next block as inter block
			int nextnext = next + __BLOCK_GET_SIZE( __mem_blocks[next] );
			if ( nextnext < __mem_num_blocks )
				__BLOCK_SET_PREV( __mem_blocks[nextnext], block );
		}
	}

	// Update if a new largest block emerged
	if( __largest_block < __BLOCK_GET_SIZE( __mem_blocks[block] ))
	{
		__largest_block = __BLOCK_GET_SIZE( __mem_blocks[block] );
		__largest_update = 0;		// No update necessary any more, because update only necessary when largest has shrinked at most
	}
}


size_t vmemavail( void )
{
	if( !__mem_start ) return( 0 );
	return __mem_free * __BLOCK_SIZE;
}


size_t vlargestblock( void )
{
	if( !__mem_start ) return( 0 );
	if( __largest_update ) __find_largest_block();
	return __largest_block * __BLOCK_SIZE;
}

int vchkptr( void *ptr )
{
	return ((( unsigned int )ptr >= __mem_start ) && (( unsigned int )ptr < __mem_start + __mem_size ));
}
