/* Host test for filesystem/wad_dir_gc.c. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wad_dir_gc.h"

#define BASE 0x4000u
static unsigned char fake_aram[512 * 1024];
static unsigned fetches;

static void fake_copy( uint32_t aram, void *buf, uint32_t len, int write )
{
	assert( !( aram & 31u ) && !( len & 31u ) && !((uintptr_t)buf & 31u ));
	assert( aram >= BASE && aram + len <= BASE + sizeof( fake_aram ));
	if( write )
		memcpy( fake_aram + ( aram - BASE ), buf, len );
	else
	{
		fetches++;
		memcpy( buf, fake_aram + ( aram - BASE ), len );
	}
}

static gc_waddir_t dir;
static uint16_t order[4096];
static uint64_t scratch[4096 * 2];

static void make_name( char *dst, unsigned i )
{
	memset( dst, 0, GC_WADDIR_NAME_LEN );
	/* Scrambled order so the file is far from sorted. */
	snprintf( dst, GC_WADDIR_NAME_LEN, "t%05u", ( i * 7919u ) % 4001u );
}

static void test_build_and_find( unsigned n )
{
	unsigned i;
	gc_waddir_entry_t e;

	assert( GC_WadDirInit( &dir, fake_copy, BASE, n, order, scratch ));
	for( i = 0; i < n; i++ )
	{
		memset( &e, 0, sizeof( e ));
		make_name( e.name, i );
		e.type = 67;
		e.filepos = (int32_t)( i * 100 );
		e.size = (int32_t)i;
		assert( GC_WadDirAppend( &dir, &e, 1 ));
	}
	assert( !GC_WadDirAppend( &dir, &e, 1 ));         /* over capacity */
	assert( GC_WadDirFind( &dir, "t00000", 67, NULL ) < 0 ); /* not sealed */
	assert( GC_WadDirSeal( &dir ));
	for( i = 0; i + 1 < n; i++ )                       /* sorted */
	{
		gc_waddir_entry_t a, b;

		GC_WadDirGet( &dir, GC_WadDirSortedIndex( &dir, i ), &a );
		GC_WadDirGet( &dir, GC_WadDirSortedIndex( &dir, i + 1 ), &b );
		assert( strcmp( a.name, b.name ) < 0 );
	}
	for( i = 0; i < n; i++ )
	{
		char nm[GC_WADDIR_NAME_LEN], up[GC_WADDIR_NAME_LEN];
		int idx, j;

		make_name( nm, i );
		memcpy( up, nm, sizeof( up ));
		for( j = 0; up[j]; j++ )
			up[j] = (char)( up[j] >= 'a' && up[j] <= 'z' ? up[j] - 32 : up[j] );
		idx = GC_WadDirFind( &dir, up, 67, &e );        /* case-insensitive */
		assert( idx == (int)i && e.filepos == (int32_t)( i * 100 ));
		assert( GC_WadDirFind( &dir, nm, GC_WADDIR_ANY_TYPE, NULL ) == (int)i );
		assert( GC_WadDirFind( &dir, nm, 68, NULL ) < 0 ); /* wrong type */
	}
	assert( GC_WadDirFind( &dir, "nothere", 67, NULL ) < 0 );
}

static void test_duplicates_and_types( void )
{
	gc_waddir_entry_t e[4];
	gc_waddir_entry_t out;

	memset( e, 0, sizeof( e ));
	strcpy( e[0].name, "same" ); e[0].type = 67; e[0].filepos = 10;
	strcpy( e[1].name, "same" ); e[1].type = 67; e[1].filepos = 20;   /* duplicate */
	strcpy( e[2].name, "same" ); e[2].type = 64; e[2].filepos = 30;
	memcpy( e[3].name, "a_long_name_16ch", 16 ); e[3].type = 67; e[3].filepos = 40; /* exactly 16 chars */
	assert( GC_WadDirInit( &dir, fake_copy, BASE, 4, order, scratch ));
	assert( GC_WadDirAppend( &dir, e, 3 ));
	assert( GC_WadDirAppend( &dir, e + 3, 1 ));                        /* odd tail pair */
	assert( GC_WadDirSeal( &dir ));
	assert( GC_WadDirFind( &dir, "same", 67, &out ) == 0 && out.filepos == 10 ); /* first wins */
	assert( GC_WadDirFind( &dir, "same", 64, &out ) == 2 && out.filepos == 30 );
	assert( GC_WadDirFind( &dir, "a_long_name_16ch", 67, &out ) == 3 );
	assert( GC_WadDirFind( &dir, "a_long_name_16chXYZ", 67, &out ) == 3 ); /* field is 16 bytes */
}

/* Names sharing the 8-character sort prefix must still order by the rest. */
static void test_long_prefix_ties( void )
{
	gc_waddir_entry_t e[4];
	static const char *const names[4] = { "abcdefgh9", "abcdefgh1", "abcdefgh", "abcdefgh15" };
	int i;

	memset( e, 0, sizeof( e ));
	for( i = 0; i < 4; i++ )
	{
		strcpy( e[i].name, names[i] );
		e[i].type = 67;
		e[i].filepos = i;
	}
	assert( GC_WadDirInit( &dir, fake_copy, BASE, 4, order, scratch ));
	assert( GC_WadDirAppend( &dir, e, 4 ));
	assert( GC_WadDirSeal( &dir ));
	/* abcdefgh < abcdefgh1 < abcdefgh15 < abcdefgh9 */
	assert( GC_WadDirSortedIndex( &dir, 0 ) == 2 && GC_WadDirSortedIndex( &dir, 1 ) == 1 );
	assert( GC_WadDirSortedIndex( &dir, 2 ) == 3 && GC_WadDirSortedIndex( &dir, 3 ) == 0 );
	for( i = 0; i < 4; i++ )
		assert( GC_WadDirFind( &dir, names[i], 67, NULL ) == i );
}

static void test_bad_args( void )
{
	assert( !GC_WadDirInit( &dir, fake_copy, BASE + 1, 4, order, scratch ));    /* unaligned */
	assert( !GC_WadDirInit( &dir, fake_copy, BASE, 0, order, scratch ));
	assert( !GC_WadDirInit( &dir, fake_copy, BASE, 70000, order, scratch ));
	assert( GC_WadDirAramBytes( 3 ) == 128 && GC_WadDirAramBytes( 4 ) == 128 );
}

int main( void )
{
	test_build_and_find( 1 );
	test_build_and_find( 2 );
	test_build_and_find( 3 );
	test_build_and_find( 3116 );                       /* halflife.wad */
	printf( "fetches for 3116 lumps: %u\n", fetches );
	test_duplicates_and_types();
	test_long_prefix_ties();
	test_bad_args();
	puts( "wad dir ok" );
	return 0;
}
