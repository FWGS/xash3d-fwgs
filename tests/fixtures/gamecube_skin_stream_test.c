/* Host test for engine/platform/gamecube/skin_stream.c. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "skin_stream.h"

#define BASE 0x4000u
#define SIZE ( 256u * 1024u )
static unsigned char fake_aram[SIZE];
static unsigned char window[40 * 1024] __attribute__(( aligned( 32 )));
static unsigned char full[4][128 * 128];
static unsigned char low[4][32 * 32];

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
static gc_skin_stream_t ss;

static void setup( uint32_t frame_budget )
{
	int i;

	GC_AramHeapInit( &heap, BASE, SIZE );
	GC_AramCacheInit( &cache, &heap, fake_copy, SIZE );
	GC_SkinStreamInit( &ss, &cache, window, sizeof( window ), frame_budget );
	for( i = 0; i < 4; i++ )
	{
		memset( full[i], 0x10 + i, sizeof( full[i] ));
		memset( low[i], 0xa0 + i, sizeof( low[i] ));
	}
}

static int reg( int i, const char *name )
{
	return GC_SkinStreamRegister( &ss, name, 128, 128, full[i], 32, 32, low[i] );
}

/* 128x128 = 16 KiB, so the 40 KiB window holds two full skins. */
static void test_low_then_full( void )
{
	uint16_t w, h;
	int is_full, a;
	const uint8_t *p;

	setup( 32 * 1024 );
	a = reg( 0, "garand" );
	assert( a >= 0 && reg( 0, "garand" ) == a );   /* idempotent */
	GC_SkinStreamBeginFrame( &ss );
	p = GC_SkinStreamGet( &ss, a, &w, &h, &is_full );
	assert( is_full && w == 128 && h == 128 && p[0] == 0x10 && p[16383] == 0x10 );
	assert( GC_SkinStreamValidate( &ss ));
	assert( ss.promotions == 1 );
	GC_SkinStreamBeginFrame( &ss );
	(void)GC_SkinStreamGet( &ss, a, &w, &h, &is_full );
	assert( is_full && ss.promotions == 1 );        /* stays resident */
}

static void test_budget_serves_low_mip( void )
{
	uint16_t w, h;
	int is_full, a, b;
	const uint8_t *p;

	setup( 16 * 1024 );                             /* one promotion per frame */
	a = reg( 0, "a" );
	b = reg( 1, "b" );
	GC_SkinStreamBeginFrame( &ss );
	(void)GC_SkinStreamGet( &ss, a, &w, &h, &is_full );
	assert( is_full );
	p = GC_SkinStreamGet( &ss, b, &w, &h, &is_full );
	assert( !is_full && w == 32 && h == 32 && p == low[1] );   /* low mip until budget refills */
	GC_SkinStreamBeginFrame( &ss );
	p = GC_SkinStreamGet( &ss, b, &w, &h, &is_full );
	assert( is_full && p[0] == 0x11 );
	assert( GC_SkinStreamValidate( &ss ));
}

static void test_lru_eviction_keeps_current_frame( void )
{
	uint16_t w, h;
	int is_full, a, b, c;
	const uint8_t *pa, *pb, *pc;

	setup( 64 * 1024 );
	a = reg( 0, "a" );
	b = reg( 1, "b" );
	c = reg( 2, "c" );
	GC_SkinStreamBeginFrame( &ss );
	pa = GC_SkinStreamGet( &ss, a, &w, &h, &is_full );
	pb = GC_SkinStreamGet( &ss, b, &w, &h, &is_full );
	assert( is_full && pa[0] == 0x10 && pb[0] == 0x11 );
	/* Window is full of this frame's skins: c must not evict them. */
	pc = GC_SkinStreamGet( &ss, c, &w, &h, &is_full );
	assert( !is_full && pc == low[2] && pa[0] == 0x10 && pb[0] == 0x11 );
	assert( ss.refusals == 1 );
	/* Next frame only b and c are drawn: a is the LRU and goes. */
	GC_SkinStreamBeginFrame( &ss );
	(void)GC_SkinStreamGet( &ss, b, &w, &h, &is_full );
	pc = GC_SkinStreamGet( &ss, c, &w, &h, &is_full );
	assert( is_full && pc[0] == 0x12 && ss.evictions == 1 );
	assert( GC_SkinStreamValidate( &ss ));
	/* a comes back by evicting the older of b/c (b was touched first). */
	GC_SkinStreamBeginFrame( &ss );
	(void)GC_SkinStreamGet( &ss, c, &w, &h, &is_full );
	pa = GC_SkinStreamGet( &ss, a, &w, &h, &is_full );
	assert( is_full && pa[0] == 0x10 && ss.evictions == 2 );
	assert( GC_SkinStreamValidate( &ss ));
}

static void test_reset_frees_aram( void )
{
	int a;
	uint32_t before;

	setup( 32 * 1024 );
	before = GC_AramHeapFreeBytes( &heap );
	a = reg( 0, "a" );
	assert( a >= 0 && GC_AramHeapFreeBytes( &heap ) < before );
	GC_SkinStreamReset( &ss );
	assert( GC_AramHeapFreeBytes( &heap ) == before );
	assert( GC_SkinStreamGet( &ss, a, NULL, NULL, NULL ) == NULL );
	assert( GC_SkinStreamValidate( &ss ));
	assert( reg( 0, "a" ) >= 0 );
}

/* RGB565 tiles: 2 bytes per texel. 64x64 = 8 KiB. */
static void test_bytes_per_texel( void )
{
	uint16_t w, h;
	int is_full, a;
	const uint8_t *p;

	setup( 32 * 1024 );
	a = GC_SkinStreamRegisterBpp( &ss, "tiled", 64, 64, full[0], 16, 16, low[0], 2 );
	assert( a >= 0 );
	GC_SkinStreamBeginFrame( &ss );
	p = GC_SkinStreamGet( &ss, a, &w, &h, &is_full );
	assert( is_full && w == 64 && h == 64 );
	assert( p[0] == 0x10 && p[8191] == 0x10 );     /* all 8192 bytes copied */
	assert( ss.window + ss.skins[a].win_off == p );
	/* 128x128x2 = 32 KiB needs 32 KiB budget, then fits the 40 KiB window once. */
	assert( GC_SkinStreamRegisterBpp( &ss, "big", 128, 128, full[1], 32, 32, low[1], 2 ) >= 0 );
	assert( GC_SkinStreamRegisterBpp( &ss, "bad", 8, 8, full[1], 4, 4, low[1], 0 ) < 0 );
	assert( GC_SkinStreamRegisterBpp( &ss, "bad", 8, 8, full[1], 4, 4, low[1], 5 ) < 0 );
	assert( GC_SkinStreamValidate( &ss ));
}

static void test_unregister( void )
{
	uint16_t w, h;
	int is_full, a, b;
	uint32_t before;

	setup( 64 * 1024 );
	before = GC_AramHeapFreeBytes( &heap );
	a = reg( 0, "a" );
	b = reg( 1, "b" );
	GC_SkinStreamBeginFrame( &ss );
	(void)GC_SkinStreamGet( &ss, a, &w, &h, &is_full );
	(void)GC_SkinStreamGet( &ss, b, &w, &h, &is_full );
	assert( ss.promotions == 2 );
	GC_SkinStreamUnregister( &ss, a );
	assert( GC_SkinStreamGet( &ss, a, NULL, NULL, NULL ) == NULL );
	assert( ss.evictions == 0 && GC_SkinStreamValidate( &ss ));
	/* b is untouched and still resident. */
	assert( GC_SkinStreamGet( &ss, b, &w, &h, &is_full ) != NULL && is_full && ss.promotions == 2 );
	/* The freed window extent and handle are reusable. */
	assert( reg( 2, "c" ) == a );
	GC_SkinStreamUnregister( &ss, a );
	GC_SkinStreamUnregister( &ss, b );
	GC_SkinStreamUnregister( &ss, b );             /* idempotent */
	GC_SkinStreamUnregister( &ss, -1 );
	GC_SkinStreamUnregister( &ss, GC_SKIN_MAX );
	assert( GC_AramHeapFreeBytes( &heap ) == before );
	assert( GC_SkinStreamValidate( &ss ));
}

static void test_rejects_bad_input( void )
{
	setup( 1024 );
	assert( GC_SkinStreamRegister( &ss, "", 8, 8, full[0], 4, 4, low[0] ) < 0 );
	assert( GC_SkinStreamRegister( &ss, "x", 0, 8, full[0], 4, 4, low[0] ) < 0 );
	assert( GC_SkinStreamGet( &ss, -1, NULL, NULL, NULL ) == NULL );
	assert( GC_SkinStreamGet( &ss, GC_SKIN_MAX, NULL, NULL, NULL ) == NULL );
	/* Larger than the whole window can never become resident. */
	assert( GC_SkinStreamRegister( &ss, "huge", 512, 512, window, 4, 4, low[0] ) < 0 );
}

int main( void )
{
	test_low_then_full();
	test_budget_serves_low_mip();
	test_lru_eviction_keeps_current_frame();
	test_reset_frees_aram();
	test_bytes_per_texel();
	test_unregister();
	test_rejects_bad_input();
	puts( "skin stream ok" );
	return 0;
}
