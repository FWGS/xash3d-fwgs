/*
wad_dir_gc.c - ARAM-resident WAD lump directory (platform independent)
Copyright (C) 2026 xash3d-gc contributors
*/
#include <string.h>
#include "wad_dir_gc.h"

/* The wscript globs every filesystem source on every platform; the header uses
 * GNU alignment attributes, so skip the file for other compilers. */
#if defined( __GNUC__ ) || defined( __clang__ )

static int WD_Lower( int c )
{
	return ( c >= 'A' && c <= 'Z' ) ? c + 32 : c;
}

uint32_t GC_WadDirAramBytes( uint32_t count )
{
	/* Entries are written in pairs, so round up to an even count. */
	return (( count + 1u ) & ~1u ) * GC_WADDIR_ENTRY_SIZE;
}

uint32_t GC_WadDirScratchBytes( uint32_t count )
{
	return count * ( sizeof( uint64_t ) + 1u );
}

int GC_WadDirInit( gc_waddir_t *dir, gc_waddir_copy_fn copy, uint32_t aram,
	uint32_t capacity, uint16_t *order, void *scratch )
{
	int i;

	if( !dir || !copy || !order || !scratch || !aram || ( aram & 31u ) || !capacity
		|| capacity > 65535u || (( uintptr_t )scratch & 7u ))
		return 0;
	memset( dir, 0, sizeof( *dir ));
	dir->copy = copy;
	dir->aram = aram;
	dir->capacity = capacity;
	dir->order = order;
	dir->keys = ( uint64_t * )scratch;
	dir->types = ( int8_t * )( dir->keys + capacity );
	for( i = 0; i < GC_WADDIR_CACHE; i++ )
		dir->cache_idx[i] = -1;
	return 1;
}

static uint64_t WD_Key( const char *name )
{
	uint64_t k = 0;
	int i;

	for( i = 0; i < 8; i++ )
	{
		const int c = WD_Lower(( unsigned char )name[i] );

		k = ( k << 8 ) | (uint64_t)c;
		if( !c )
		{
			k <<= 8 * ( 7 - i );
			break;
		}
	}
	return k;
}

int GC_WadDirAppend( gc_waddir_t *dir, const gc_waddir_entry_t *entries, uint32_t n )
{
	if( !dir || dir->sealed || !entries || n > dir->capacity - dir->count )
		return 0;
	while( n )
	{
		const uint32_t take = n < 2 ? n : 2;
		uint32_t t;

		for( t = 0; t < take; t++ )
		{
			dir->keys[dir->count + t] = WD_Key( entries[t].name );
			dir->types[dir->count + t] = entries[t].type;
		}

		memset( dir->stage, 0, sizeof( dir->stage ));
		memcpy( dir->stage, entries, take * sizeof( dir->stage[0] ));
		/* Always move whole 32-byte pairs: an odd tail also writes a zero entry
		 * past the end, which the capacity rounding leaves room for. */
		dir->copy( dir->aram + dir->count * GC_WADDIR_ENTRY_SIZE, dir->stage,
			take * GC_WADDIR_ENTRY_SIZE, 1 );
		dir->count += take;
		entries += take;
		n -= take;
	}
	return 1;
}

int GC_WadDirGet( gc_waddir_t *dir, uint32_t index, gc_waddir_entry_t *out )
{
	const uint32_t slot = index % GC_WADDIR_CACHE;

	if( !dir || index >= dir->count )
		return 0;
	if( dir->cache_idx[slot] != (int32_t)index )
	{
		dir->copy( dir->aram + index * GC_WADDIR_ENTRY_SIZE, &dir->cache[slot],
			GC_WADDIR_ENTRY_SIZE, 0 );
		dir->cache_idx[slot] = (int32_t)index;
	}
	if( out )
		*out = dir->cache[slot];
	return 1;
}

/* Case-insensitive compare of two names limited to the 16-byte field. */
static int WD_NameCmp( const char *a, const char *b )
{
	int i;

	for( i = 0; i < GC_WADDIR_NAME_LEN; i++ )
	{
		const int ca = WD_Lower(( unsigned char )a[i] );
		const int cb = WD_Lower(( unsigned char )b[i] );

		if( ca != cb )
			return ca < cb ? -1 : 1;
		if( !ca )
			return 0;
	}
	return 0;
}

/* Order of file indices a and b: name, then type, then file order. */
static int WD_Less( gc_waddir_t *dir, uint16_t a, uint16_t b )
{
	gc_waddir_entry_t ea, eb;
	int d;

	if( a == b )
		return 0;
	/* Most pairs differ in the first 8 characters: no ARAM access needed. */
	if( dir->keys[a] != dir->keys[b] )
		return dir->keys[a] < dir->keys[b];
	/* Equal prefix that ends inside it is a whole equal name: decide by type
	 * and file order. */
	if( !( dir->keys[a] & 0xffu ))
	{
		if( dir->types[a] != dir->types[b] )
			return dir->types[a] < dir->types[b];
		return a < b;
	}
	GC_WadDirGet( dir, a, &ea );
	GC_WadDirGet( dir, b, &eb );
	d = WD_NameCmp( ea.name, eb.name );
	if( !d )
		d = ea.type < eb.type ? -1 : ea.type > eb.type ? 1 : 0;
	if( !d )
		d = a < b ? -1 : 1;
	return d < 0;
}

static void WD_Sift( gc_waddir_t *dir, uint32_t root, uint32_t end )
{
	uint16_t *o = dir->order;

	for( ;; )
	{
		uint32_t child = root * 2 + 1;

		if( child >= end )
			return;
		if( child + 1 < end && WD_Less( dir, o[child], o[child + 1] ))
			child++;
		if( !WD_Less( dir, o[root], o[child] ))
			return;
		{
			const uint16_t t = o[root];

			o[root] = o[child];
			o[child] = t;
		}
		root = child;
	}
}

int GC_WadDirSeal( gc_waddir_t *dir )
{
	uint32_t i;

	if( !dir || dir->sealed || !dir->count )
		return 0;
	for( i = 0; i < dir->count; i++ )
		dir->order[i] = (uint16_t)i;
	/* Heapsort: no extra memory and no recursion on a small stack. */
	for( i = dir->count / 2; i > 0; i-- )
		WD_Sift( dir, i - 1, dir->count );
	for( i = dir->count - 1; i > 0; i-- )
	{
		const uint16_t t = dir->order[0];

		dir->order[0] = dir->order[i];
		dir->order[i] = t;
		WD_Sift( dir, 0, i );
	}
	dir->sealed = 1;
	return 1;
}

int GC_WadDirSortedIndex( const gc_waddir_t *dir, uint32_t k )
{
	return ( dir && dir->sealed && k < dir->count ) ? (int)dir->order[k] : -1;
}

int GC_WadDirFind( gc_waddir_t *dir, const char *name, int type, gc_waddir_entry_t *out )
{
	char key[GC_WADDIR_NAME_LEN];
	int left, right, found = -1;

	if( !dir || !dir->sealed || !name )
		return -1;
	memset( key, 0, sizeof( key ));
	strncpy( key, name, sizeof( key ));
	left = 0;
	right = (int)dir->count - 1;
	/* Leftmost entry that is >= (name, type); for any type, >= name. */
	while( left <= right )
	{
		const int middle = ( left + right ) / 2;
		gc_waddir_entry_t e;
		int d;

		GC_WadDirGet( dir, dir->order[middle], &e );
		d = WD_NameCmp( e.name, key );
		if( !d && type != GC_WADDIR_ANY_TYPE )
			d = e.type < type ? -1 : e.type > type ? 1 : 0;
		if( d < 0 )
			left = middle + 1;
		else
		{
			if( !d )
				found = middle;
			right = middle - 1;
		}
	}
	if( found < 0 )
		return -1;
	if( out )
		GC_WadDirGet( dir, dir->order[found], out );
	return (int)dir->order[found];
}

#endif /* __GNUC__ */
