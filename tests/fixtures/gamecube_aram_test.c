/* Host test for engine/platform/gamecube/aram_core.c. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "aram_core.h"

#define BASE 0x4000u
#define SIZE ( 1024u * 1024u )
static unsigned char fake_aram[SIZE];

static void fake_copy( uint32_t aram, void *buf, uint32_t len, int write )
{
	assert( aram >= BASE && aram + len <= BASE + SIZE );
	if( write )
		memcpy( fake_aram + ( aram - BASE ), buf, len );
	else
		memcpy( buf, fake_aram + ( aram - BASE ), len );
}

static gc_aram_heap_t heap;
static gc_aram_cache_t cache;

static void test_heap_classes_and_merge( void )
{
	uint32_t s1, s2, t1, t2, t3;

	GC_AramHeapInit( &heap, BASE, SIZE );
	assert( GC_AramHeapValidate( &heap ));
	assert( GC_AramHeapFreeBytes( &heap ) == SIZE );

	s1 = GC_AramHeapAlloc( &heap, 100, GC_ARAM_SESSION, "audio" );
	s2 = GC_AramHeapAlloc( &heap, 64, GC_ARAM_SESSION, "saves" );
	t1 = GC_AramHeapAlloc( &heap, 1000, GC_ARAM_STREAM, "video" );
	t2 = GC_AramHeapAlloc( &heap, 33, GC_ARAM_STREAM, "fill" );
	assert( s1 == BASE && s2 == BASE + 128 );           /* session from the bottom, 32-aligned */
	assert( t1 == BASE + SIZE - 1024 );                 /* stream from the top */
	assert( t2 == t1 - 64 );
	assert( GC_AramHeapBlockSize( &heap, t2 ) == 64 );
	assert( GC_AramHeapValidate( &heap ));

	/* Freeing a stream block in the middle never moves session regions. */
	assert( GC_AramHeapFree( &heap, t1 ));
	assert( !GC_AramHeapFree( &heap, t1 ));             /* double free refused */
	assert( !GC_AramHeapFree( &heap, t2 + 32 ));        /* interior address refused */
	assert( GC_AramHeapValidate( &heap ));
	t3 = GC_AramHeapAlloc( &heap, 1024, GC_ARAM_STREAM, "video" );
	assert( t3 == t1 );                                 /* top hole is reused */
	assert( GC_AramHeapFree( &heap, t2 ) && GC_AramHeapFree( &heap, t3 ));
	assert( GC_AramHeapFree( &heap, s2 ) && GC_AramHeapFree( &heap, s1 ));
	assert( heap.count == 1 && GC_AramHeapValidate( &heap ));
	assert( GC_AramHeapLargestFree( &heap ) == SIZE );
}

static void test_heap_refuses_overcommit( void )
{
	GC_AramHeapInit( &heap, BASE, SIZE );
	/* AR_Alloc would hand out an address past the end of ARAM here. */
	assert( GC_AramHeapAlloc( &heap, SIZE + 32, GC_ARAM_SESSION, "big" ) == 0 );
	assert( GC_AramHeapAlloc( &heap, 0xffffffffu, GC_ARAM_STREAM, "wrap" ) == 0 );
	assert( heap.failures == 2 );
	assert( GC_AramHeapAlloc( &heap, SIZE, GC_ARAM_SESSION, "all" ) == BASE );
	assert( GC_AramHeapAlloc( &heap, 32, GC_ARAM_STREAM, "none" ) == 0 );
	assert( GC_AramHeapFreeBytes( &heap ) == 0 && GC_AramHeapValidate( &heap ));
}

static void test_heap_random_soak( void )
{
	uint32_t live[64] = { 0 };
	unsigned seed = 12345;
	int step;

	GC_AramHeapInit( &heap, BASE, SIZE );
	for( step = 0; step < 20000; step++ )
	{
		int slot;
		seed = seed * 1103515245u + 12345u;
		slot = (int)(( seed >> 16 ) % 64 );
		if( live[slot] )
		{
			assert( GC_AramHeapFree( &heap, live[slot] ));
			live[slot] = 0;
		}
		else
		{
			uint32_t size = 32 + (( seed >> 3 ) % ( 48 * 1024 ));
			live[slot] = GC_AramHeapAlloc( &heap, size,
				( seed & 1 ) ? GC_ARAM_STREAM : GC_ARAM_SESSION, "soak" );
			if( live[slot] )
				assert( live[slot] % 32 == 0 && live[slot] >= BASE && live[slot] + size <= BASE + SIZE );
		}
		assert( GC_AramHeapValidate( &heap ));
	}
	for( step = 0; step < 64; step++ )
		if( live[step] )
			assert( GC_AramHeapFree( &heap, live[step] ));
	assert( heap.count == 1 && GC_AramHeapFreeBytes( &heap ) == SIZE );
}

static void fill( unsigned char *buf, uint32_t len, unsigned char seed )
{
	uint32_t i;
	for( i = 0; i < len; i++ )
		buf[i] = (unsigned char)( seed + i * 7u );
}

static void test_cache_roundtrip_and_lru( void )
{
	static unsigned char a[40000], b[40000], c[40000], out[40000];
	int ia, ib, ic;

	GC_AramHeapInit( &heap, BASE, SIZE );
	GC_AramCacheInit( &cache, &heap, fake_copy, 96 * 1024 );
	fill( a, sizeof( a ), 1 );
	fill( b, sizeof( b ), 2 );
	fill( c, sizeof( c ), 3 );

	assert( GC_AramCacheFind( &cache, "models/v_garand.mdl" ) < 0 );
	ia = GC_AramCacheStore( &cache, "models/v_garand.mdl", a, sizeof( a ));
	ib = GC_AramCacheStore( &cache, "models/v_mp40.mdl", b, sizeof( b ));
	assert( ia >= 0 && ib >= 0 );
	assert( GC_AramCacheFind( &cache, "MODELS\\V_GARAND.MDL" ) == ia );  /* case and slash insensitive */
	assert( GC_AramCacheSize( &cache, ia ) == sizeof( a ));
	memset( out, 0, sizeof( out ));
	assert( GC_AramCacheRead( &cache, ia, 0, out, sizeof( a )) && !memcmp( out, a, sizeof( a )));
	assert( GC_AramCacheRead( &cache, ia, 1000, out, 5 ) && !memcmp( out, a + 1000, 5 ));
	assert( !GC_AramCacheRead( &cache, ia, sizeof( a ) - 4, out, 5 ));   /* past the end */

	/* Budget is 96 KiB: a third 40 KB asset evicts the least recently used (mp40). */
	ic = GC_AramCacheStore( &cache, "models/v_98k.mdl", c, sizeof( c ));
	assert( ic >= 0 );
	assert( GC_AramCacheFind( &cache, "models/v_mp40.mdl" ) < 0 );
	assert( GC_AramCacheFind( &cache, "models/v_garand.mdl" ) == ia );
	assert( cache.evictions == 1 && cache.bytes <= cache.budget );
	assert( GC_AramCacheRead( &cache, ic, 0, out, sizeof( c )) && !memcmp( out, c, sizeof( c )));
	assert( GC_AramHeapValidate( &heap ));
}

static void test_cache_pins_block_eviction( void )
{
	static unsigned char a[60000], b[60000];
	int ia;

	GC_AramHeapInit( &heap, BASE, SIZE );
	GC_AramCacheInit( &cache, &heap, fake_copy, 96 * 1024 );
	fill( a, sizeof( a ), 4 );
	fill( b, sizeof( b ), 5 );
	ia = GC_AramCacheStore( &cache, "a", a, sizeof( a ));
	GC_AramCachePin( &cache, ia );
	assert( GC_AramCacheStore( &cache, "b", b, sizeof( b )) < 0 );   /* would need the pinned bytes */
	assert( cache.refusals == 1 && GC_AramCacheFind( &cache, "a" ) == ia );
	GC_AramCacheUnpin( &cache, ia );
	assert( GC_AramCacheStore( &cache, "b", b, sizeof( b )) >= 0 );
	assert( GC_AramCacheFind( &cache, "a" ) < 0 );
}

static void test_cache_streamed_store_and_heap_pressure( void )
{
	static unsigned char a[50000], out[50000];
	uint32_t session;
	int i;

	GC_AramHeapInit( &heap, BASE, SIZE );
	GC_AramCacheInit( &cache, &heap, fake_copy, SIZE );
	fill( a, sizeof( a ), 6 );

	i = GC_AramCacheBegin( &cache, "seq", sizeof( a ));
	assert( i >= 0 );
	assert( GC_AramCacheFind( &cache, "seq" ) < 0 );                 /* not ready until commit */
	assert( GC_AramCacheWrite( &cache, i, 30000, a + 30000, 20000 ));
	assert( GC_AramCacheWrite( &cache, i, 0, a, 30000 ));
	assert( !GC_AramCacheWrite( &cache, i, 49999, a, 2 ));
	assert( GC_AramCacheCommit( &cache, i ));
	assert( GC_AramCacheFind( &cache, "seq" ) == i );
	assert( GC_AramCacheRead( &cache, i, 0, out, sizeof( a )) && !memcmp( out, a, sizeof( a )));

	/* An aborted streamed store gives its bytes back. */
	{
		const uint32_t before = cache.bytes;
		const int j = GC_AramCacheBegin( &cache, "partial", 4096 );
		assert( j >= 0 );
		GC_AramCacheAbort( &cache, j );
		assert( cache.bytes == before && GC_AramCacheFind( &cache, "partial" ) < 0 );
	}

	/* Session regions win over cached assets: fill the heap with session data,
	 * then the next cache store evicts what it must instead of failing. */
	session = GC_AramHeapAlloc( &heap, SIZE - 64 * 1024, GC_ARAM_SESSION, "texstore" );
	assert( session );
	assert( GC_AramCacheStore( &cache, "next", a, 40000 ) >= 0 );
	assert( GC_AramCacheFind( &cache, "seq" ) < 0 );
	assert( GC_AramHeapValidate( &heap ));

	GC_AramCacheFlush( &cache );
	assert( cache.bytes == 0 );
	assert( GC_AramHeapFree( &heap, session ) && heap.count == 1 );
}

int main( void )
{
	test_heap_classes_and_merge();
	test_heap_refuses_overcommit();
	test_heap_random_soak();
	test_cache_roundtrip_and_lru();
	test_cache_pins_block_eviction();
	test_cache_streamed_store_and_heap_pressure();
	puts( "aram ok" );
	return 0;
}
