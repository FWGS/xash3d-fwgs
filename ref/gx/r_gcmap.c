/*
r_gcmap.c - GameCube smoke boot memory trims
*/
#include "r_local.h"

#if XASH_GAMECUBE
static qboolean gc_renderer_trimmed;
static qboolean gc_static_map_arena_in_use;
static qboolean gc_static_map_arena_dirty;
/* Sized for New Game GX presents (and smaller gcmap probes).
 * G93: 320×240 is the default New Game world path; -gcnewgame160 keeps 160×120. */
#define GC_GCMAP_STATIC_MAX_W 320
#define GC_GCMAP_STATIC_MAX_H 240
static short gc_gcmap_static_zbuffer[GC_GCMAP_STATIC_MAX_W * GC_GCMAP_STATIC_MAX_H];
static pixel_t gc_gcmap_static_viewbuffer[GC_GCMAP_STATIC_MAX_W * GC_GCMAP_STATIC_MAX_H];

void R_GcmapTrimScreenBuffers( void );
void R_GcmapTrimSurfaceCache( void );
qboolean R_TryInitGcmapSurfaceCache( void );
qboolean R_AllocScreen( void );
void R_GCRebuildBlendMaps( void );

qboolean R_GcmapOwnsStaticScreenBuffers( void )
{
	return d_pzbuffer == gc_gcmap_static_zbuffer || vid.buffer == gc_gcmap_static_viewbuffer;
}

qboolean R_GcmapOwnsStaticZBuffer( void )
{
	return d_pzbuffer == gc_gcmap_static_zbuffer;
}

qboolean R_GcmapOwnsStaticViewBuffer( void )
{
	return vid.buffer == gc_gcmap_static_viewbuffer;
}

/*
===========
R_GcmapBindStaticScreenBuffers

Tip-safe Pure Flipper path: bind BSS 320×240 Z/view scratch instead of malloc
native 640×480 soft rasters (~1.2 MiB MEM1 tip on real hardware).
===========
*/
qboolean R_GcmapBindStaticScreenBuffers( int logical_w, int logical_h )
{
	const int soft_w = ( logical_w > 0 && logical_w <= GC_GCMAP_STATIC_MAX_W )
		? logical_w : GC_GCMAP_STATIC_MAX_W;
	const int soft_h = ( logical_h > 0 && logical_h <= GC_GCMAP_STATIC_MAX_H )
		? logical_h : GC_GCMAP_STATIC_MAX_H;
	const size_t soft_pixels = (size_t)soft_w * (size_t)soft_h;

	R_GcmapReleaseDynamicScreenBuffers();
	d_pzbuffer = gc_gcmap_static_zbuffer;
	vid.buffer = gc_gcmap_static_viewbuffer;
	memset( d_pzbuffer, 0xff, soft_pixels * sizeof( d_pzbuffer[0] ));
	memset( vid.buffer, 0, soft_pixels * sizeof( vid.buffer[0] ));
	gEngfuncs.Con_Reportf(
		"Xash3D GameCube: hardware Flipper screen soft=%dx%d logical=%dx%d (static BSS)\n",
		soft_w, soft_h, logical_w, logical_h );
	return true;
}

void R_GcmapReleaseDynamicScreenBuffers( void )
{
	if( d_pzbuffer )
	{
		if( d_pzbuffer != gc_gcmap_static_zbuffer )
			free( d_pzbuffer );
		d_pzbuffer = NULL;
	}

	if( vid.buffer )
	{
		if( vid.buffer != gc_gcmap_static_viewbuffer )
			free( vid.buffer );
		vid.buffer = NULL;
	}
}

qboolean R_GcmapGetViewport( int *width, int *height )
{
	if( !vid.width || !vid.height )
		return false;

	if( width )
		*width = vid.width;
	if( height )
		*height = vid.height;
	return true;
}

void *R_GCBorrowMapLoadStaticArena( size_t size, size_t *capacity )
{
	byte *base = (byte *)&vid.colormap[0];
	size_t arena_size = sizeof( vid.colormap ) + sizeof( vid.screen ) + sizeof( vid.screen32 )
		+ sizeof( vid.addmap ) + sizeof( vid.modmap ) + sizeof( vid.alphamap )
		+ sizeof( vid.mapload_pad );

	if( capacity )
		*capacity = arena_size;
	if( !gc_renderer_trimmed || gc_static_map_arena_in_use || size == 0 )
		return NULL;
	if( size > arena_size )
	{
		gEngfuncs.Con_Reportf( "Xash3D GameCube: map-load arena too small need=%s have=%s\n",
			Q_memprint( size ), Q_memprint( arena_size ));
		return NULL;
	}

	gc_static_map_arena_in_use = true;
	gEngfuncs.Con_Reportf( "Xash3D GameCube: map-load buffer using renderer static arena %s/%s\n",
		Q_memprint( size ), Q_memprint( arena_size ));
	return base;
}

qboolean R_GCReleaseMapLoadStaticArena( void *ptr )
{
	if( !ptr || ptr != (void *)&vid.colormap[0] || !gc_static_map_arena_in_use )
		return false;

	gc_static_map_arena_in_use = false;
	gc_static_map_arena_dirty = true;
	return true;
}

qboolean R_GCIsMapLoadStaticArena( const void *ptr )
{
	return ptr != NULL && ptr == (const void *)&vid.colormap[0];
}

void R_GcmapTrimForMapLoad( void )
{
	if( gc_renderer_trimmed )
		return;

	R_GcmapTrimSurfaceCache();
	R_GcmapTrimScreenBuffers();
	gc_renderer_trimmed = true;
	gEngfuncs.Con_Reportf( "Xash3D GameCube: renderer trim for map load\n" );
}

void R_GcmapRestoreAfterMapLoad( void )
{
	if( !gc_renderer_trimmed )
		return;

	if( gc_static_map_arena_dirty )
	{
		R_GCRebuildBlendMaps();
		gc_static_map_arena_dirty = false;
		gEngfuncs.Con_Reportf( "Xash3D GameCube: renderer lookup tables rebuilt after map load\n" );
	}

	if( gEngfuncs.Sys_CheckParm( "-gcmap" ) || gEngfuncs.Sys_CheckParm( "-gcnewgame" ))
	{
		if( !R_TryInitGcmapSurfaceCache() )
			gEngfuncs.Con_Reportf( "Xash3D GameCube: surface cache restore deferred after map load\n" );
		gEngfuncs.Con_Reportf( "Xash3D GameCube: renderer screen restore deferred for GameCube low-memory route\n" );
	}
	else
	{
		R_InitCaches();
		R_AllocScreen();
	}

	gc_renderer_trimmed = false;
	gEngfuncs.Con_Reportf( "Xash3D GameCube: renderer restore after map load\n" );
}

void R_GcmapMarkMapLoadComplete( void )
{
	gc_renderer_trimmed = false;
}

qboolean R_GcmapEnsureSurfaceCache( void )
{
	if( !gEngfuncs.Sys_CheckParm( "-gcmap" ))
		return false;

	return R_TryInitGcmapSurfaceCache();
}

qboolean R_GcmapPrepareWorldRender( void )
{
	if( !gEngfuncs.Sys_CheckParm( "-gcmap" ) && !gEngfuncs.Sys_CheckParm( "-gcnewgame" ))
		return false;

	if( vid.buffer && d_pzbuffer && vid.width > 0 && vid.height > 0 )
	{
		int target_w = 320;
		int target_h = 240;

		if( gEngfuncs.Sys_CheckParm( "-gcnewgame160" )
			|| gEngfuncs.Sys_CheckParm( "-gcworldrender" ))
		{
			target_w = 160;
			target_h = 120;
		}
		/* G129: only reuse when retained screens match the lean present size. */
		if( vid.width == target_w && vid.height == target_h )
		{
			gpGlobals->width = vid.width;
			gpGlobals->height = vid.height;
			gEngfuncs.Con_Reportf( "Xash3D GameCube: gcmap world render reusing retained screen buffers\n" );
			return true;
		}
		gEngfuncs.Con_Reportf( "Xash3D GameCube: G129 drop retained screen %dx%d (want %dx%d)\n",
			vid.width, vid.height, target_w, target_h );
		R_GcmapReleaseDynamicScreenBuffers();
	}

	if( R_GcmapAllocMinimalScreen() )
		return true;

	return R_AllocScreen();
}

qboolean R_GcmapAllocMinimalScreen( void )
{
	int w, h;
	size_t pixels;
	qboolean use_static_screen = false;

	if( !gEngfuncs.Sys_CheckParm( "-gcmap" ) && !gEngfuncs.Sys_CheckParm( "-gcnewgame" ))
		return false;

	if( vid.buffer && d_pzbuffer && vid.width > 0 && vid.height > 0 )
	{
		int want_w = GC_GCMAP_STATIC_MAX_W;
		int want_h = GC_GCMAP_STATIC_MAX_H;

		if( gEngfuncs.Sys_CheckParm( "-gcworldrender" )
			|| gEngfuncs.Sys_CheckParm( "-gcnewgame160" ))
		{
			want_w = 160;
			want_h = 120;
		}
		if( vid.width == want_w && vid.height == want_h )
		{
			gpGlobals->width = vid.width;
			gpGlobals->height = vid.height;
			return true;
		}
		R_GcmapReleaseDynamicScreenBuffers();
	}

	w = gpGlobals->width > 0 ? gpGlobals->width : 320;
	h = gpGlobals->height > 0 ? gpGlobals->height : 240;
	if( gEngfuncs.Sys_CheckParm( "-gcworldrender" ))
	{
		/* Probe route: keep the world pass visible but tighter than the
		 * generic half-res smoke path so software BSP fill fits GameCube CPU. */
		w = 160;
		h = 120;
		use_static_screen = ( w <= GC_GCMAP_STATIC_MAX_W && h <= GC_GCMAP_STATIC_MAX_H ) ? true : false;
	}
	else if( gEngfuncs.Sys_CheckParm( "-gcnewgame" ))
	{
		/* G93: default 320×240; -gcnewgame160 restores the G36-safe fallback. */
		if( gEngfuncs.Sys_CheckParm( "-gcnewgame160" ))
		{
			w = 160;
			h = 120;
		}
		else
		{
			w = GC_GCMAP_STATIC_MAX_W;
			h = GC_GCMAP_STATIC_MAX_H;
		}
		use_static_screen = true;
	}
	if( w < 160 )
		w = 160;
	if( h < 120 )
		h = 120;
	else if( !gEngfuncs.Sys_CheckParm( "-gcworldrender" ) && !gEngfuncs.Sys_CheckParm( "-gcnewgame" ) && h < 128 )
		h = 128;

	if( use_static_screen )
	{
		if( w > GC_GCMAP_STATIC_MAX_W )
			w = GC_GCMAP_STATIC_MAX_W;
		if( h > GC_GCMAP_STATIC_MAX_H )
			h = GC_GCMAP_STATIC_MAX_H;
	}

	vid.width = w;
	vid.height = h;
	vid.rowbytes = w;
	pixels = (size_t)w * (size_t)h;

	if( gEngfuncs.Sys_CheckParm( "-gcworldrender" ) || gEngfuncs.Sys_CheckParm( "-gcnewgame" ))
	{
		gpGlobals->width = w;
		gpGlobals->height = h;
	}

	R_GcmapReleaseDynamicScreenBuffers();

	if( use_static_screen )
	{
		d_pzbuffer = gc_gcmap_static_zbuffer;
		vid.buffer = gc_gcmap_static_viewbuffer;
		memset( d_pzbuffer, 0xff, pixels * sizeof( d_pzbuffer[0] ));
		memset( vid.buffer, 0, pixels * sizeof( vid.buffer[0] ));
		gEngfuncs.Con_Reportf( "Xash3D GameCube: gcmap minimal screen ready %dx%d (static)\n", w, h );
		return true;
	}

	d_pzbuffer = malloc( pixels * sizeof( d_pzbuffer[0] ));
	if( !d_pzbuffer )
	{
		gEngfuncs.Con_Reportf( "Xash3D GameCube: gcmap minimal screen missing z buffer\n" );
		return false;
	}

	vid.buffer = malloc( pixels * sizeof( pixel_t ));
	if( !vid.buffer )
	{
		gEngfuncs.Con_Reportf( "Xash3D GameCube: gcmap minimal screen missing colormap buffer\n" );
		free( d_pzbuffer );
		d_pzbuffer = NULL;
		return false;
	}

	gEngfuncs.Con_Reportf( "Xash3D GameCube: gcmap minimal screen ready %dx%d\n", w, h );
	return true;
}

/*
=============
R_GcmapShadeDumpFromDepth

G130/G131: paint a readable depth-shaded RGB565 preview into the present buffer
for Dolphin DumpFrames. Soft-edge color rasters speckled; 1/z from d_pzbuffer
shows room structure (near=warm wall, far=sky).

G131: store is short, but izi>>16 often lands in the high half for near
surfaces (zi≥1 → negative as signed). Treat samples as unsigned and only skip
the 0xFFFF clear value.
=============
*/
unsigned R_GcmapShadeDumpFromDepth( unsigned short *dst, int dst_w, int dst_h, int dst_stride )
{
	int x, y;
	unsigned zmin = 0xFFFF;
	unsigned zmax = 0;
	unsigned valid = 0;
	const unsigned short sky = 0x5ADB;
	unsigned hist[256];
	unsigned cum;
	unsigned target_lo, target_hi;
	unsigned zlo = 0, zhi = 0xFFFF;
	int bi;
	qboolean zlo_set = false;

	if( !dst || !d_pzbuffer || vid.width <= 0 || vid.height <= 0 || d_zwidth <= 0 )
		return 0;
	if( dst_w <= 0 || dst_h <= 0 || dst_stride < dst_w )
		return 0;

	memset( hist, 0, sizeof( hist ));

	for( y = 0; y < vid.height; y++ )
	{
		const short *zrow = d_pzbuffer + y * (int)d_zwidth;

		for( x = 0; x < vid.width; x++ )
		{
			unsigned z = (unsigned short)zrow[x];

			if( z == 0xFFFF )
				continue;
			valid++;
			hist[z >> 8]++;
			if( z < zmin )
				zmin = z;
			if( z > zmax )
				zmax = z;
		}
	}

	if( valid < 64 || zmax <= zmin )
	{
		gEngfuncs.Con_Reportf( "Xash3D GameCube: G131 depth dump skip valid=%u z=%u..%u\n",
			valid, zmin, zmax );
		return valid;
	}

	/* Percentile stretch — a few outlier zi values were collapsing the whole
	 * frame to one mid-tone. Use ~5th/95th percentile of the high byte. */
	target_lo = valid / 20;
	target_hi = valid - target_lo;
	if( target_hi <= target_lo )
		target_hi = target_lo + 1;
	cum = 0;
	zlo = zmin;
	zhi = zmax;
	for( bi = 0; bi < 256; bi++ )
	{
		cum += hist[bi];
		if( !zlo_set && cum >= target_lo )
		{
			zlo = (unsigned)bi << 8;
			zlo_set = true;
		}
		if( cum >= target_hi )
		{
			zhi = ((unsigned)bi << 8 ) | 0xFF;
			break;
		}
	}
	if( zhi <= zlo )
	{
		zlo = zmin;
		zhi = zmax;
	}
	/* If percentiles collapsed (near-wall spawn), fall back to full range. */
	if(( zhi - zlo ) < 512 )
	{
		zlo = zmin;
		zhi = zmax;
	}

	for( y = 0; y < dst_h; y++ )
	{
		int sy = ( y * vid.height ) / dst_h;
		unsigned short *drow;
		const short *zrow;

		if( sy >= vid.height )
			sy = vid.height - 1;
		drow = dst + y * dst_stride;
		zrow = d_pzbuffer + sy * (int)d_zwidth;

		for( x = 0; x < dst_w; x++ )
		{
			int sx = ( x * vid.width ) / dst_w;
			unsigned z;
			unsigned t;
			int r, g, b;

			if( sx >= vid.width )
				sx = vid.width - 1;
			z = (unsigned short)zrow[sx];
			if( z == 0xFFFF )
			{
				drow[x] = sky;
				continue;
			}
			if( z <= zlo )
				t = 0;
			else if( z >= zhi )
				t = 255;
			else
				t = (( z - zlo ) * 255u ) / ( zhi - zlo );
			if( t < 128 )
			{
				r = 90 + (int)(( 164 - 90 ) * t / 128);
				g = 89 + (int)(( 161 - 89 ) * t / 128);
				b = 222 + (int)(( 164 - 222 ) * t / 128);
			}
			else
			{
				unsigned u = t - 128;
				r = 164 + (int)(( 213 - 164 ) * u / 127);
				g = 161 + (int)(( 182 - 161 ) * u / 127);
				b = 164 + (int)(( 0 - 164 ) * u / 127);
			}
			if( r < 0 ) r = 0;
			if( g < 0 ) g = 0;
			if( b < 0 ) b = 0;
			if( r > 255 ) r = 255;
			if( g > 255 ) g = 255;
			if( b > 255 ) b = 255;
			drow[x] = (unsigned short)((( r >> 3 ) << 11 ) | (( g >> 2 ) << 5 ) | ( b >> 3 ));
		}
	}

	gEngfuncs.Con_Reportf( "Xash3D GameCube: G131 depth dump shade valid=%u/%u z=%u..%u p=%u..%u %dx%d\n",
		valid, (unsigned)vid.width * (unsigned)vid.height, zmin, zmax, zlo, zhi, dst_w, dst_h );
	return valid;
}

/*
=============
R_GcmapPosterizeDumpFromDepth

G136: paint a 3-plane room silhouette from zi percentiles directly. Color
posterize after continuous depth shade collapsed to flat sky — near shade is
sky-blue, so the G130 blue→sky heuristic ate the whole frame.
=============
*/
unsigned R_GcmapPosterizeDumpFromDepth( unsigned short *dst, int dst_w, int dst_h, int dst_stride )
{
	int x, y;
	unsigned zmin = 0xFFFF;
	unsigned zmax = 0;
	unsigned valid = 0;
	const unsigned short sky = 0x5ADB;
	const unsigned short wall = 0xA514;
	const unsigned short near_wall = 0x6B4D;
	unsigned hist[256];
	unsigned cum;
	unsigned target_a, target_b;
	unsigned za = 0, zb = 0xFFFF;
	int bi;
	qboolean za_set = false;
	unsigned nsky = 0, nwall = 0, nnear = 0;

	if( !dst || !d_pzbuffer || vid.width <= 0 || vid.height <= 0 || d_zwidth <= 0 )
		return 0;
	if( dst_w <= 0 || dst_h <= 0 || dst_stride < dst_w )
		return 0;

	memset( hist, 0, sizeof( hist ));

	for( y = 0; y < vid.height; y++ )
	{
		const short *zrow = d_pzbuffer + y * (int)d_zwidth;

		for( x = 0; x < vid.width; x++ )
		{
			unsigned z = (unsigned short)zrow[x];

			if( z == 0xFFFF )
				continue;
			valid++;
			hist[z >> 8]++;
			if( z < zmin )
				zmin = z;
			if( z > zmax )
				zmax = z;
		}
	}

	if( valid < 64 || zmax <= zmin )
	{
		gEngfuncs.Con_Reportf( "Xash3D GameCube: G136 depth posterize skip valid=%u z=%u..%u\n",
			valid, zmin, zmax );
		return valid;
	}

	/* ~33rd / ~66th percentile of high byte → near / mid / far bands. */
	target_a = valid / 3;
	target_b = ( valid * 2 ) / 3;
	if( target_b <= target_a )
		target_b = target_a + 1;
	cum = 0;
	za = zmin;
	zb = zmax;
	for( bi = 0; bi < 256; bi++ )
	{
		cum += hist[bi];
		if( !za_set && cum >= target_a )
		{
			za = (unsigned)bi << 8;
			za_set = true;
		}
		if( cum >= target_b )
		{
			zb = ((unsigned)bi << 8 ) | 0xFF;
			break;
		}
	}
	if( zb <= za )
	{
		za = zmin + ( zmax - zmin ) / 3;
		zb = zmin + ( 2 * ( zmax - zmin ) ) / 3;
	}

	for( y = 0; y < dst_h; y++ )
	{
		int sy = ( y * vid.height ) / dst_h;
		unsigned short *drow;
		const short *zrow;

		if( sy >= vid.height )
			sy = vid.height - 1;
		drow = dst + y * dst_stride;
		zrow = d_pzbuffer + sy * (int)d_zwidth;

		for( x = 0; x < dst_w; x++ )
		{
			int sx = ( x * vid.width ) / dst_w;
			unsigned z;

			if( sx >= vid.width )
				sx = vid.width - 1;
			z = (unsigned short)zrow[sx];
			if( z == 0xFFFF )
			{
				drow[x] = sky;
				nsky++;
			}
			else if( z <= za )
			{
				drow[x] = near_wall;
				nnear++;
			}
			else if( z <= zb )
			{
				drow[x] = wall;
				nwall++;
			}
			else
			{
				drow[x] = sky;
				nsky++;
			}
		}
	}

	gEngfuncs.Con_Reportf( "Xash3D GameCube: G136 depth posterize valid=%u near=%u wall=%u sky=%u z=%u..%u bands=%u..%u %dx%d\n",
		valid, nnear, nwall, nsky, zmin, zmax, za, zb, dst_w, dst_h );
	return valid;
}
#endif
