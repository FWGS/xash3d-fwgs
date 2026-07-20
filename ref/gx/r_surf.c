/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// r_surf.c: surface-related refresh code

#include <stdlib.h>
#include "r_local.h"
#include "mod_local.h"

drawsurf_t r_drawsurf;

static uint       lightleft, blocksize, sourcetstep;
static uint       lightright, lightleftstep, lightrightstep, blockdivshift;
static unsigned   blockdivmask;
static void       *prowdestbase;
static pixel_t    *pbasesource;
static int        surfrowbytes;                        // used by ASM files
static unsigned   *r_lightptr;
static int        r_stepback;
static int        r_lightwidth;
static int        r_numhblocks, r_numvblocks;
static pixel_t    *r_source, *r_sourcemax;

void R_DrawSurfaceBlock8_mip0( void );
void R_DrawSurfaceBlock8_mip1( void );
void R_DrawSurfaceBlock8_mip2( void );
void R_DrawSurfaceBlock8_mip3( void );
void R_DrawSurfaceBlock8_Generic( void );
void R_DrawSurfaceBlock8_World( void );
static void     R_DrawSurfaceDecals( void );

static float    worldlux_s, worldlux_t;

static void     (*surfmiptable[4])( void ) = {
	R_DrawSurfaceBlock8_mip0,
	R_DrawSurfaceBlock8_mip1,
	R_DrawSurfaceBlock8_mip2,
	R_DrawSurfaceBlock8_mip3
};

// void R_BuildLightMap (void);
static unsigned blocklights[10240]; // allow some very large lightmaps

static float           surfscale;
static qboolean        r_cache_thrash;         // set if surface cache is thrashing

static int sc_size;
surfcache_t     *sc_rover;
static surfcache_t *sc_base;
#if XASH_GAMECUBE
static int r_gc_surface_cache_skip_reports;
/* Static BSS so New Game textured spans never malloc after present buffers. */
static byte gc_lowres_surfcache_store[GC_SURFACE_CACHE_LOWRES + 32]
	__attribute__((aligned( 32 )));
static qboolean gc_sc_static;
static qboolean gc_sc_heap;
static void R_FreeGameCubeSurfaceCache( void );
static qboolean R_TryInitGameCubeSurfaceCacheSized( int size, qboolean allow_lowres_static );
static void R_GCEnsureLowResSoftSurfaceCache( const surfcache_t *cache );
static void R_GCConvertLowResSurfaceCacheToRGB565( const surfcache_t *cache );
/* G140: set when BLEND_LM writes display RGB565 into the surfcache. */
static qboolean r_gc_surf_cache_rgb565;
#endif

#if XASH_GAMECUBE
static void R_GCEnsureLowResSoftSurfaceCache( const surfcache_t *cache )
{
	unsigned nz = 0;
	int n, i, lim;

	if( !GC_UseLowResWorldProbe() || !cache || !cache->data || !r_drawsurf.image
		|| !r_drawsurf.image->pixels[0] || r_drawsurf.surfwidth <= 0
		|| r_drawsurf.surfheight <= 0 )
		return;

	n = cache->width * r_drawsurf.surfheight;
	lim = n < 256 ? n : 256;
	for( i = 0; i < lim; i++ )
	{
		if( ((pixel_t *)cache->data)[i] )
			nz++;
	}

	/* Block drawers often leave the cache empty on lean extents
	 * (r_numhblocks=0 / bad lightwalk). Tile fullbright soft texels before
	 * decals so later soft-space blends still work. */
	if( nz < 8 )
	{
		const image_t *img = r_drawsurf.image;
		const int tw = img->width > 0 ? img->width : 1;
		const int th = img->height > 0 ? img->height : 1;
		const pixel_t *src = img->pixels[0];
		pixel_t *dst = (pixel_t *)cache->data;
		int y, x;
		static qboolean g134_tile_logged;

		for( y = 0; y < r_drawsurf.surfheight; y++ )
		{
			pixel_t *row = dst + y * cache->width;
			const pixel_t *srow = src + ( y % th ) * tw;
			for( x = 0; x < r_drawsurf.surfwidth; x++ )
				row[x] = srow[x % tw];
		}
		if( !g134_tile_logged )
		{
			gEngfuncs.Con_Reportf( "Xash3D GameCube: G134 tile soft tex into cache %s %dx%d\n",
				img->name[0] ? img->name : "?", r_drawsurf.surfwidth, r_drawsurf.surfheight );
			g134_tile_logged = true;
		}
		/* Tiled soft still needs convert — clear RGB565 flag. */
		r_gc_surf_cache_rgb565 = false;
	}
}

/* Inverse of GL_UploadTexture 565→332+minor packing (same as R_BuildScreenMap). */
static pixel_t R_GCSoftMajorMinorToRGB565( pixel_t soft )
{
	unsigned major = ( soft >> 8 ) & 0xFFu;
	unsigned minor = soft & 0xFFu;
	unsigned r, g, b;

	r = (( major >> 5 ) & 7u ) << 2;
	g = (( major >> 2 ) & 7u ) << 3;
	b = ( major & 3u ) << 3;
	r |= MOVE_BIT( minor, 5, 1 ) | MOVE_BIT( minor, 2, 0 );
	g |= MOVE_BIT( minor, 7, 2 ) | MOVE_BIT( minor, 4, 1 ) | MOVE_BIT( minor, 1, 0 );
	b |= MOVE_BIT( minor, 6, 2 ) | MOVE_BIT( minor, 3, 1 ) | MOVE_BIT( minor, 0, 0 );
	return (pixel_t)(( r << 11 ) | ( g << 5 ) | b );
}

/* G140: soft→RGB565 with Quake light grade (no colormap). light>>8 is 0..31 dark. */
static pixel_t R_GCBlendSoftToRGB565( pixel_t soft, unsigned light )
{
	pixel_t rgb;
	unsigned scale, r, g, b;

	if( soft == TRANSPARENT_COLOR )
		return 0;

	rgb = R_GCSoftMajorMinorToRGB565( soft );
	scale = 31u - (( light >> 8 ) & 0x1fu );
	if( scale < 8u )
		scale = 8u;
	r = ((( rgb >> 11 ) & 0x1fu ) * scale ) / 31u;
	g = ((( rgb >> 5 ) & 0x3fu ) * scale ) / 31u;
	b = (( rgb & 0x1fu ) * scale ) / 31u;
	r_gc_surf_cache_rgb565 = true;
	return (pixel_t)(( r << 11 ) | ( g << 5 ) | b );
}

static void R_GCConvertLowResSurfaceCacheToRGB565( const surfcache_t *cache )
{
	pixel_t *dst;
	int y, x;
	unsigned uniq = 0;
	unsigned short seen[64];
	static qboolean g140_conv_logged;

	if( !GC_UseLowResWorldProbe() || !cache || !cache->data || !r_drawsurf.image
		|| !r_drawsurf.image->pixels[0] || r_drawsurf.surfwidth <= 0
		|| r_drawsurf.surfheight <= 0 )
		return;

	dst = (pixel_t *)cache->data;

	/* G140: BLEND_LM already wrote lit RGB565 — scrub soft transparent leftovers. */
	if( r_gc_surf_cache_rgb565 )
	{
		for( y = 0; y < r_drawsurf.surfheight; y++ )
		{
			pixel_t *row = dst + y * cache->width;
			for( x = 0; x < r_drawsurf.surfwidth; x++ )
			{
				if( row[x] == TRANSPARENT_COLOR )
					row[x] = 0;
			}
		}
		if( !g140_conv_logged )
		{
			gEngfuncs.Con_Reportf( "Xash3D GameCube: G140 lit RGB565 cache (scrub transparent) %dx%d\n",
				r_drawsurf.surfwidth, r_drawsurf.surfheight );
			g140_conv_logged = true;
		}
		return;
	}

	for( y = 0; y < r_drawsurf.surfheight; y++ )
	{
		pixel_t *row = dst + y * cache->width;
		for( x = 0; x < r_drawsurf.surfwidth; x++ )
		{
			pixel_t soft = row[x];
			pixel_t rgb;
			unsigned u;
			qboolean found = false;

			/* G140: do not leave soft TRANSPARENT_COLOR in an RGB565 cache. */
			if( soft == TRANSPARENT_COLOR )
			{
				row[x] = 0;
				continue;
			}
			rgb = R_GCSoftMajorMinorToRGB565( soft );
			row[x] = rgb;
			if( !g140_conv_logged && uniq < 64 )
			{
				for( u = 0; u < uniq; u++ )
				{
					if( seen[u] == rgb )
					{
						found = true;
						break;
					}
				}
				if( !found )
					seen[uniq++] = rgb;
			}
		}
	}

	if( !g140_conv_logged )
	{
		gEngfuncs.Con_Reportf( "Xash3D GameCube: G140 soft->RGB565 cache uniq=%u %dx%d\n",
			uniq, r_drawsurf.surfwidth, r_drawsurf.surfheight );
		g140_conv_logged = true;
	}
}
#endif

static void R_BuildLightMap( void );
/*
===============
R_AddDynamicLights
===============
*/
static void R_AddDynamicLights( const msurface_t *surf )
{
	const mextrasurf_t *info = surf->info;
	int sample_frac = 1.0;

	// no dlighted surfaces here
	if( !surf->dlightbits )
		return;

	float      sample_size = gEngfuncs.Mod_SampleSizeForFace( surf );
	int        smax = ( info->lightextents[0] / sample_size ) + 1;
	int        tmax = ( info->lightextents[1] / sample_size ) + 1;
	mtexinfo_t *tex = surf->texinfo;

	if( FBitSet( tex->flags, TEX_WORLD_LUXELS ))
	{
		if( surf->texinfo->faceinfo )
			sample_frac = surf->texinfo->faceinfo->texture_step;
		else if( FBitSet( surf->texinfo->flags, TEX_EXTRA_LIGHTMAP ))
			sample_frac = LM_SAMPLE_EXTRASIZE;
		else
			sample_frac = LM_SAMPLE_SIZE;
	}

	for( int lnum = 0; lnum < MAX_DLIGHTS; lnum++ )
	{
		vec3_t impact, origin_l;
		float  dist;

		if( !FBitSet( surf->dlightbits, BIT( lnum )))
			continue; // not lit by this light

		dlight_t *dl = &gp_dlights[lnum];

		// transform light origin to local bmodel space
		if( !tr.modelviewIdentity )
			Matrix4x4_VectorITransform( RI.objectMatrix, dl->origin, origin_l );
		else
			VectorCopy( dl->origin, origin_l );

		float rad = dl->radius;
		dist = PlaneDiff( origin_l, surf->plane );
		rad -= fabs( dist );

		// rad is now the highest intensity on the plane
		float minlight = dl->minlight;
		if( rad < minlight )
			continue;

		minlight = rad - minlight;

		if( surf->plane->type < 3 )
		{
			VectorCopy( origin_l, impact );
			impact[surf->plane->type] -= dist;
		}
		else
			VectorMA( origin_l, -dist, surf->plane->normal, impact );

		float sl = DotProduct( impact, info->lmvecs[0] ) + info->lmvecs[0][3] - info->lightmapmins[0];
		float tl = DotProduct( impact, info->lmvecs[1] ) + info->lmvecs[1][3] - info->lightmapmins[1];

		int monolight = LightToTexGamma(( dl->color.r + dl->color.g + dl->color.b ) / 3 * 4 ) * 3;

		for( int t = 0; t < tmax; t++ )
		{
			int td = ( tl - sample_size * t ) * sample_frac;

			if( td < 0 )
				td = -td;

			for( int s = 0; s < smax; s++ )
			{
				int   sd = ( sl - sample_size * s ) * sample_frac;
				float dist;

				if( sd < 0 )
					sd = -sd;

				if( sd > td )
					dist = sd + ( td >> 1 );
				else
					dist = td + ( sd >> 1 );

				if( dist < minlight )
				{
					blocklights[( s + ( t * smax ))] += ((int)(( rad - dist ) * 256 ) * monolight ) / 256;
				}
			}
		}
	}
}


/*
=================
R_BuildLightmap

Combine and scale multiple lightmaps into the floating
format in r_blocklights
=================
*/
static void R_BuildLightMap( void )
{
	const msurface_t   *surf = r_drawsurf.surf;
	const mextrasurf_t *info = surf->info;
	const int          sample_size = gEngfuncs.Mod_SampleSizeForFace( surf );
	int                smax = ( info->lightextents[0] / sample_size ) + 1;
	int                tmax = ( info->lightextents[1] / sample_size ) + 1;
	int                size = smax * tmax;

	if( size <= 0 )
		size = 1;
	if( size > (int)( sizeof( blocklights ) / sizeof( blocklights[0] )))
		size = (int)( sizeof( blocklights ) / sizeof( blocklights[0] ));

	if( FBitSet( surf->flags, SURF_CONVEYOR ))
	{
		smax = ( info->lightextents[0] * 3 / sample_size ) + 1;
		size = smax * tmax;
		if( size <= 0 )
			size = 1;
		if( size > (int)( sizeof( blocklights ) / sizeof( blocklights[0] )))
			size = (int)( sizeof( blocklights ) / sizeof( blocklights[0] ));
		memset( blocklights, 0xff, sizeof( uint ) * size );
		return;
	}

	memset( blocklights, 0, sizeof( uint ) * size );

	// add all the lightmaps
	for( int map = 0; map < MAXLIGHTMAPS && surf->samples; map++ )
	{
		const color24 *lm = &surf->samples[map * size];

		if( surf->styles[map] >= 255 )
			break;

		uint scale = g_lightstylevalue[surf->styles[map]];

		for( int i = 0; i < size; i++ )
			blocklights[i] += ( lm[i].r + lm[i].g + lm[i].b ) * scale;
	}

#if XASH_GAMECUBE
	/* Low-res New Game: surfaces without samples still need a mid light grade
	 * so textured spans are not crushed to black by BLEND_LM.
	 * LightToTexGamma() only accepts 0..1023 (returns 0 for larger!).
	 * Seed a safe pre-gamma value; final grade is forced after the loop. */
	if( GC_UseLowResWorldProbe() && !surf->samples )
	{
		for( int i = 0; i < size; i++ )
			blocklights[i] = 900 << 6; /* >>6 = 900 */
	}
#endif

	// add all the dynamic lights
	if( surf->dlightframe == tr.framecount )
	{
		R_AddDynamicLights( surf );
#if XASH_GAMECUBE
		if( GC_UseLowResWorldProbe() )
		{
			static qboolean gc_dlight_marker_logged;

			if( !gc_dlight_marker_logged && surf->dlightbits )
			{
				gEngfuncs.Con_Reportf( "Xash3D GameCube: RGB565 world dlights active\n" );
				gc_dlight_marker_logged = true;
			}
		}
#endif
	}

	// bound, invert, and shift
	for( int i = 0; i < size; i++ )
	{
		int t;
		uint lg_in = blocklights[i] >> 6;
		/* Soft lightgammatable is 1024 entries; clamp instead of zeroing. */
		if( lg_in > 1023 )
			lg_in = 1023;
		if( blocklights[i] < 65280 )
			t = LightToTexGamma( lg_in ) << 6;
		else
			t = (int)blocklights[i];

		t = bound( 0, t, 65535 * 3 );
		t = t / 2048 / 3; // (255*256 - t) >> (8 - VID_CBITS);

		// if (t < (1 << 6))
		// t = (1 << 6);
		t = t << 8;

		blocklights[i] = t;
	}

#if XASH_GAMECUBE
	/* Force a readable BLEND_LM grade for null-sample capture faces.
	 * Post-loop values top out ~0x0A00 via the /2048/3 path; pin mid-bright. */
	if( GC_UseLowResWorldProbe() && !surf->samples )
	{
		for( int i = 0; i < size; i++ )
			blocklights[i] = 0x1400;
	}
#endif
}


/*
===============
R_TextureAnimation

Returns the proper texture for a given time and surface
===============
*/
static texture_t *R_TextureAnimation( msurface_t *s )
{
	texture_t *base = s->texinfo->texture;
	int       reletive;

	if( RI.currententity && RI.currententity->curstate.frame )
	{
		if( base->alternate_anims )
			base = base->alternate_anims;
	}

	if( !base->anim_total )
		return base;

	if( base->name[0] == '-' )
	{
		int tx = (int)(( s->texturemins[0] + ( base->width << 16 )) / base->width ) % MOD_FRAMES;
		int ty = (int)(( s->texturemins[1] + ( base->height << 16 )) / base->height ) % MOD_FRAMES;

		reletive = rtable[tx][ty] % base->anim_total;
	}
	else
	{
		int speed;

		// Quake1 textures uses 10 frames per second
		if( FBitSet( R_GetTexture( base->gl_texturenum )->flags, TF_QUAKEPAL ))
			speed = 10;
		else
			speed = 20;

		reletive = (int)( gp_cl->time * speed ) % base->anim_total;
	}

	int count = 0;

	while( base->anim_min > reletive || base->anim_max <= reletive )
	{
		base = base->anim_next;

		if( !base || ++count > MOD_FRAMES )
			return s->texinfo->texture;
	}

	return base;
}

/*
===============
R_DrawSurface
===============
*/
void R_DrawSurface( void )
{
	pixel_t *basetptr;
	int     smax, tmax, twidth;
	int     u;
	int     soffset, basetoffset, texwidth;
	int     horzblockstep;
	pixel_t *pcolumndest;
	void    (*pblockdrawer)( void );
	image_t *mt;
	uint    sample_size, sample_bits, sample_pot;


	surfrowbytes = r_drawsurf.rowbytes;

	sample_size = LM_SAMPLE_SIZE_AUTO( r_drawsurf.surf );
	if( sample_size == 16 )
		sample_bits = 4, sample_pot = sample_size;
	else
	{
		sample_bits = tr.sample_bits;

		if( sample_bits == -1 )
		{
			sample_bits = 0;
			for( sample_pot = 1; sample_pot < sample_size; sample_pot <<= 1, sample_bits++ )
				;
		}
		else
			sample_pot = 1 << sample_bits;
	}
	mt = r_drawsurf.image;

#if XASH_GAMECUBE
	if( !mt || !mt->pixels[r_drawsurf.surfmip] )
	{
		/* Quality 0 only has mip0 — fall back instead of leaving an empty cache. */
		if( mt && mt->pixels[0] && r_drawsurf.surfmip != 0 )
		{
			r_drawsurf.surfmip = 0;
		}
		else
		{
			if(( gEngfuncs.Sys_CheckParm( "-gcmap" ) || GC_UseLowResWorldProbe() )
				&& r_gc_surface_cache_skip_reports < 16 )
			{
				gEngfuncs.Con_Reportf( "Xash3D GameCube: R_DrawSurface skip missing image mip=%d\n",
					r_drawsurf.surfmip );
				r_gc_surface_cache_skip_reports++;
			}
			return;
		}
	}
#endif

	r_source = mt->pixels[r_drawsurf.surfmip];

// the fractional light values should range from 0 to (VID_GRADES - 1) << 16
// from a source range of 0 - 255

	texwidth = mt->width >> r_drawsurf.surfmip;

	blocksize = sample_pot >> r_drawsurf.surfmip;
	blockdivshift = sample_bits - r_drawsurf.surfmip;
	blockdivmask = ( 1 << blockdivshift ) - 1;

	if( sample_size == 16 )
		r_lightwidth = ( r_drawsurf.surf->info->lightextents[0] >> 4 ) + 1;
	else
		r_lightwidth = ( r_drawsurf.surf->info->lightextents[0] / sample_size ) + 1;

	r_numhblocks = r_drawsurf.surfwidth >> blockdivshift;
	r_numvblocks = r_drawsurf.surfheight >> blockdivshift;

#if XASH_GAMECUBE
	// G24b: bound draw dimensions for quality 0 to preserve cache and iteration budget.
	// Cap to 64x64 so low-memory surfaces stay within allocated cache blocks.
	if( GC_GetVisualQuality() == 0 )
	{
		if( r_numhblocks > 4 )
			r_numhblocks = 4;
		if( r_numvblocks > 4 )
			r_numvblocks = 4;
	}
	/* Keep light-column walks inside the built lightmap (when one exists).
	 * Tiled / no-LM faces use extents-sized caches — do not crush to 1×1. */
	if( GC_UseLowResWorldProbe()
	    && r_drawsurf.surf->info->lightextents[0] > 0
	    && r_drawsurf.surf->info->lightextents[1] > 0 )
	{
		int light_rows = (( r_drawsurf.surf->info->lightextents[1] / sample_size ) + 1 );

		if( r_numhblocks > r_lightwidth )
			r_numhblocks = r_lightwidth;
		if( light_rows > 0 && r_numvblocks > light_rows )
			r_numvblocks = light_rows;
	}
	/* mip0 block is 16px: widths <16 yielded r_numhblocks=0, left memset
	 * cache empty → vid.screen[0] flat teal spans. */
	if( GC_UseLowResWorldProbe() )
	{
		if( r_numhblocks <= 0 && r_drawsurf.surfwidth > 0 )
			r_numhblocks = 1;
		if( r_numvblocks <= 0 && r_drawsurf.surfheight > 0 )
			r_numvblocks = 1;
	}
#endif

// ==============================

	// Guard against degenerate block dimensions from misaligned extents
	// or zero-sized surfaces. Skip draw to avoid division-by-zero or infinite loops.
	if( r_numhblocks <= 0 || r_numvblocks <= 0 || blocksize <= 0 )
		return;

	if( sample_size == 16 )
		pblockdrawer = surfmiptable[r_drawsurf.surfmip];
	else
		pblockdrawer = R_DrawSurfaceBlock8_Generic;

// TODO: only needs to be set when there is a display settings change
	horzblockstep = blocksize;

	smax = mt->width >> r_drawsurf.surfmip;
	twidth = texwidth;
	tmax = mt->height >> r_drawsurf.surfmip;
	sourcetstep = texwidth;
	r_stepback = tmax * twidth;

	r_sourcemax = r_source + ( tmax * smax );

	// glitchy and slow way to draw some lightmap
	if( r_drawsurf.surf->texinfo->flags & TEX_WORLD_LUXELS )
	{
#if XASH_GAMECUBE
		/* Quality 0 smoke skips world-luxels lighting; New Game low-res keeps it. */
		if( !GC_GetVisualQuality() && !GC_UseLowResWorldProbe() )
		{
			r_lightptr = blocklights;
			prowdestbase = r_drawsurf.surfdat;
			pbasesource = r_source;
			R_DrawSurfaceBlock8_World();
			return;
		}
#endif
		worldlux_s = r_drawsurf.surf->extents[0] / r_drawsurf.surf->info->lightextents[0];
		worldlux_t = r_drawsurf.surf->extents[1] / r_drawsurf.surf->info->lightextents[1];
		if( worldlux_s == 0 )
			worldlux_s = 1;
		if( worldlux_t == 0 )
			worldlux_t = 1;

		soffset = r_drawsurf.surf->texturemins[0];
		basetoffset = r_drawsurf.surf->texturemins[1];
		// soffset =  r_drawsurf.surf->info->lightmapmins[0] * worldlux_s;
		// basetoffset = r_drawsurf.surf->info->lightmapmins[1] * worldlux_t;
		// << 16 components are to guarantee positive values for %
		soffset = (( soffset >> r_drawsurf.surfmip ) + ( smax << 16 )) % smax;
		basetptr = &r_source[(((( basetoffset >> r_drawsurf.surfmip )
					+ ( tmax << 16 )) % tmax ) * twidth )];

		pcolumndest = r_drawsurf.surfdat;

		for( u = 0; u < r_numhblocks; u++ )
		{
			r_lightptr = blocklights + (int)( u / ( worldlux_s + 0.5f ));

			prowdestbase = pcolumndest;

			pbasesource = basetptr + soffset;

			R_DrawSurfaceBlock8_World();

			soffset = soffset + blocksize;
			if( soffset >= smax )
				soffset = 0;

			pcolumndest += horzblockstep;
		}
		return;
	}

	soffset = r_drawsurf.surf->info->lightmapmins[0];
	basetoffset = r_drawsurf.surf->info->lightmapmins[1];

// << 16 components are to guarantee positive values for %
	soffset = (( soffset >> r_drawsurf.surfmip ) + ( smax << 16 )) % smax;
	basetptr = &r_source[(((( basetoffset >> r_drawsurf.surfmip )
				+ ( tmax << 16 )) % tmax ) * twidth )];

	pcolumndest = r_drawsurf.surfdat;

	for( u = 0; u < r_numhblocks; u++ )
	{
		r_lightptr = blocklights + u;

		prowdestbase = pcolumndest;

		pbasesource = basetptr + soffset;

		( *pblockdrawer )();

		soffset = soffset + blocksize;
		if( soffset >= smax )
			soffset = 0;

		pcolumndest += horzblockstep;
	}
	// test what if we have very slow cache building
	// usleep(10000);
}


// =============================================================================

#if XASH_GAMECUBE
/* G140: soft major<<8|minor → lit RGB565 (no Quake 8-bit colormap). */
#define BLEND_LM( pix, light ) \
	(( GC_UseLowResWorldProbe() ) \
		? R_GCBlendSoftToRGB565(( pix ), (unsigned)( light )) \
		: ( vid.colormap[( pix >> 3 ) | (( light & 0x1f00 ) << 5 )] | ( pix & 7 )))
#else
#define BLEND_LM( pix, light ) ( vid.colormap[( pix >> 3 ) | (( light & 0x1f00 ) << 5 )] | ( pix & 7 ))
#endif

/*
================
R_DrawSurfaceBlock8_World

Does not draw lightmap correclty, but scale it correctly. Better than nothing
================
*/
void R_DrawSurfaceBlock8_World( void )
{
	int     v, i, b;
	uint    lightstep, lighttemp, light;
	pixel_t pix, *psource, *prowdest;
	int     lightpos = 0;

#if XASH_GAMECUBE
	/* Quality 0 smoke stays unlit; New Game low-res applies lightmaps. */
	if( !GC_GetVisualQuality() && !GC_UseLowResWorldProbe() )
	{
		psource = pbasesource;
		prowdest = prowdestbase;
		// Guard against degenerate block dimensions on the low-memory path
		if( r_numvblocks <= 0 || blocksize <= 0 || r_numhblocks <= 0 )
			return;
		for( v = 0; v < r_numvblocks; v++ )
		{
			for( i = 0; i < blocksize; i++ )
			{
				for( b = blocksize - 1; b >= 0; b-- )
				{
					pix = psource[b];
					// Always write to dest even for transparent pixels to avoid
					// leaving uninitialized cache data on low-memory path.
					prowdest[b] = ( pix == TRANSPARENT_COLOR ) ? TRANSPARENT_COLOR : pix;
				}
				psource += sourcetstep;
				prowdest += surfrowbytes;
			}
			if( psource >= r_sourcemax )
				psource -= r_stepback;
		}
		return;
	}
#endif

	psource = pbasesource;
	prowdest = prowdestbase;

	for( v = 0; v < r_numvblocks; v++ )
	{
		// FIXME: make these locals?
		// FIXME: use delta rather than both right and left, like ASM?
		int light_row = ( lightpos / r_lightwidth ) * r_lightwidth;
		if( light_row < 0 || light_row + 1 >= r_lightwidth * r_drawsurf.surfheight )
		{
			// Guard against out-of-bounds light access on edge-case surfaces.
			// Quality 0 already returned early above, so this path is quality 1/2.
			// Fill with neutral gray so surface remains visible.
			for( int remaining = v; remaining < r_numvblocks; remaining++ )
			{
				for( int i = 0; i < blocksize; i++ )
				{
					for( int b = blocksize - 1; b >= 0; b-- )
						prowdest[b] = 0x7FFF;
					prowdest += surfrowbytes;
				}
			}
			return;
		}
		lightleft = r_lightptr[light_row];
		lightright = r_lightptr[light_row + 1];
		lightpos += r_lightwidth / worldlux_s;
		lightleftstep = ( r_lightptr[( lightpos / r_lightwidth ) * r_lightwidth] - lightleft ) >> ( 4 - r_drawsurf.surfmip );
		lightrightstep = ( r_lightptr[( lightpos / r_lightwidth ) * r_lightwidth + 1] - lightright ) >> ( 4 - r_drawsurf.surfmip );

		for( i = 0; i < blocksize; i++ )
		{
			lighttemp = lightleft - lightright;
			lightstep = lighttemp >> ( 4 - r_drawsurf.surfmip );

			light = lightright;

			for( b = blocksize - 1; b >= 0; b-- )
			{
				// pix = psource[(uint)(b * worldlux_s)];
				pix = psource[b];
				prowdest[b] = BLEND_LM( pix, light );
				if( pix == TRANSPARENT_COLOR )
					prowdest[b] = TRANSPARENT_COLOR;
				// ((unsigned char *)vid.colormap)
				// [(light & 0xFF00) + pix];
				light += lightstep;
			}

			psource += sourcetstep;
			lightright += lightrightstep;
			lightleft += lightleftstep;
			prowdest += surfrowbytes;
		}

		if( psource >= r_sourcemax )
			psource -= r_stepback;
	}
}


/*
================
R_DrawSurfaceBlock8_Generic
================
*/
void R_DrawSurfaceBlock8_Generic( void )
{
	int     v, i, b;
	uint    lightstep, lighttemp, light;
	pixel_t pix, *psource, *prowdest;

#if XASH_GAMECUBE
	/* Quality 0 smoke stays unlit; New Game low-res applies lightmaps. */
	if( !GC_GetVisualQuality() && !GC_UseLowResWorldProbe() )
	{
		psource = pbasesource;
		prowdest = prowdestbase;
		// Guard against degenerate block dimensions on the low-memory path
		if( r_numvblocks <= 0 || blocksize <= 0 || r_numhblocks <= 0 )
			return;
		for( v = 0; v < r_numvblocks; v++ )
		{
			for( i = 0; i < blocksize; i++ )
			{
				for( b = blocksize - 1; b >= 0; b-- )
				{
					pix = psource[b];
					// Always write to dest even for transparent pixels to avoid
					// leaving uninitialized cache data on low-memory path.
					prowdest[b] = ( pix == TRANSPARENT_COLOR ) ? TRANSPARENT_COLOR : pix;
				}
				psource += sourcetstep;
				prowdest += surfrowbytes;
			}
			if( psource >= r_sourcemax )
				psource -= r_stepback;
		}
		return;
	}
#endif

	psource = pbasesource;
	prowdest = prowdestbase;

	for( v = 0; v < r_numvblocks; v++ )
	{
		// FIXME: make these locals?
		// FIXME: use delta rather than both right and left, like ASM?
		lightleft = r_lightptr[0];
		lightright = r_lightptr[1];
		r_lightptr += r_lightwidth;
		lightleftstep = ( r_lightptr[0] - lightleft ) >> ( 4 - r_drawsurf.surfmip );
		lightrightstep = ( r_lightptr[1] - lightright ) >> ( 4 - r_drawsurf.surfmip );

		for( i = 0; i < blocksize; i++ )
		{
			lighttemp = lightleft - lightright;
			lightstep = lighttemp >> ( 4 - r_drawsurf.surfmip );

			light = lightright;

			for( b = blocksize - 1; b >= 0; b-- )
			{
				pix = psource[b];
				prowdest[b] = BLEND_LM( pix, light );
				if( pix == TRANSPARENT_COLOR )
					prowdest[b] = TRANSPARENT_COLOR;
				// ((unsigned char *)vid.colormap)
				// [(light & 0xFF00) + pix];
				light += lightstep;
			}

			psource += sourcetstep;
			lightright += lightrightstep;
			lightleft += lightleftstep;
			prowdest += surfrowbytes;
		}

		if( psource >= r_sourcemax )
			psource -= r_stepback;
	}
}


/*
================
R_DrawSurfaceBlock8_mip0
================
*/
void R_DrawSurfaceBlock8_mip0( void )
{
	int     v, i, b;
	uint    lightstep, lighttemp, light;
	pixel_t pix, *psource, *prowdest;

	psource = pbasesource;
	prowdest = prowdestbase;

	for( v = 0; v < r_numvblocks; v++ )
	{
		// FIXME: make these locals?
		// FIXME: use delta rather than both right and left, like ASM?
		lightleft = r_lightptr[0];
		lightright = r_lightptr[1];
		r_lightptr += r_lightwidth;
		lightleftstep = ( r_lightptr[0] - lightleft ) >> 4;
		lightrightstep = ( r_lightptr[1] - lightright ) >> 4;

		for( i = 0; i < 16; i++ )
		{
			lighttemp = lightleft - lightright;
			lightstep = lighttemp >> 4;

			light = lightright;

			for( b = 15; b >= 0; b-- )
			{
				pix = psource[b];
				prowdest[b] = BLEND_LM( pix, light );
				if( pix == TRANSPARENT_COLOR )
					prowdest[b] = TRANSPARENT_COLOR;

				// pix;
				// ((unsigned char *)vid.colormap)
				// [(light & 0xFF00) + pix];
				light += lightstep;
			}

			psource += sourcetstep;
			lightright += lightrightstep;
			lightleft += lightleftstep;
			prowdest += surfrowbytes;
		}

		if( psource >= r_sourcemax )
			psource -= r_stepback;
	}
}


/*
================
R_DrawSurfaceBlock8_mip1
================
*/
void R_DrawSurfaceBlock8_mip1( void )
{
	int     v, i, b;
	uint    lightstep, lighttemp, light;
	pixel_t pix, *psource, *prowdest;

	psource = pbasesource;
	prowdest = prowdestbase;

	for( v = 0; v < r_numvblocks; v++ )
	{
		// FIXME: make these locals?
		// FIXME: use delta rather than both right and left, like ASM?
		lightleft = r_lightptr[0];
		lightright = r_lightptr[1];
		r_lightptr += r_lightwidth;
		lightleftstep = ( r_lightptr[0] - lightleft ) >> 3;
		lightrightstep = ( r_lightptr[1] - lightright ) >> 3;

		for( i = 0; i < 8; i++ )
		{
			lighttemp = lightleft - lightright;
			lightstep = lighttemp >> 3;

			light = lightright;

			for( b = 7; b >= 0; b-- )
			{
				pix = psource[b];
				prowdest[b] = BLEND_LM( pix, light );
				// ((unsigned char *)vid.colormap)
				// [(light & 0xFF00) + pix];
				light += lightstep;
			}

			psource += sourcetstep;
			lightright += lightrightstep;
			lightleft += lightleftstep;
			prowdest += surfrowbytes;
		}

		if( psource >= r_sourcemax )
			psource -= r_stepback;
	}
}


/*
================
R_DrawSurfaceBlock8_mip2
================
*/
void R_DrawSurfaceBlock8_mip2( void )
{
	int     v, i, b;
	uint    lightstep, lighttemp, light;
	pixel_t pix, *psource, *prowdest;

	psource = pbasesource;
	prowdest = prowdestbase;

	for( v = 0; v < r_numvblocks; v++ )
	{
		// FIXME: make these locals?
		// FIXME: use delta rather than both right and left, like ASM?
		lightleft = r_lightptr[0];
		lightright = r_lightptr[1];
		r_lightptr += r_lightwidth;
		lightleftstep = ( r_lightptr[0] - lightleft ) >> 2;
		lightrightstep = ( r_lightptr[1] - lightright ) >> 2;

		for( i = 0; i < 4; i++ )
		{
			lighttemp = lightleft - lightright;
			lightstep = lighttemp >> 2;

			light = lightright;

			for( b = 3; b >= 0; b-- )
			{
				pix = psource[b];
				prowdest[b] = BLEND_LM( pix, light );
				// ((unsigned char *)vid.colormap)
				// [(light & 0xFF00) + pix];
				light += lightstep;
			}

			psource += sourcetstep;
			lightright += lightrightstep;
			lightleft += lightleftstep;
			prowdest += surfrowbytes;
		}

		if( psource >= r_sourcemax )
			psource -= r_stepback;
	}
}


/*
================
R_DrawSurfaceBlock8_mip3
================
*/
void R_DrawSurfaceBlock8_mip3( void )
{
	int     v, i, b;
	uint    lightstep, lighttemp, light;
	pixel_t pix, *psource, *prowdest;

	psource = pbasesource;
	prowdest = prowdestbase;

	for( v = 0; v < r_numvblocks; v++ )
	{
		// FIXME: make these locals?
		// FIXME: use delta rather than both right and left, like ASM?
		lightleft = r_lightptr[0];
		lightright = r_lightptr[1];
		r_lightptr += r_lightwidth;
		lightleftstep = ( r_lightptr[0] - lightleft ) >> 1;
		lightrightstep = ( r_lightptr[1] - lightright ) >> 1;

		for( i = 0; i < 2; i++ )
		{
			lighttemp = lightleft - lightright;
			lightstep = lighttemp >> 1;

			light = lightright;

			for( b = 1; b >= 0; b-- )
			{
				pix = psource[b];
				prowdest[b] = BLEND_LM( pix, light );
				// ((unsigned char *)vid.colormap)
				// [(light & 0xFF00) + pix];
				light += lightstep;
			}

			psource += sourcetstep;
			lightright += lightrightstep;
			lightleft += lightleftstep;
			prowdest += surfrowbytes;
		}

		if( psource >= r_sourcemax )
			psource -= r_stepback;
	}
}

// ============================================================================
/*
================
R_InitCaches

================
*/
void R_InitCaches( void )
{
	int size;

#if XASH_GAMECUBE
	if( sw_surfcacheoverride.value > 0 )
		size = (int)sw_surfcacheoverride.value;
	else size = GC_SURFACE_CACHE_DEFAULT;

	if( size > GC_SURFACE_CACHE_MAX )
		size = GC_SURFACE_CACHE_MAX;
#else /* !XASH_GAMECUBE */
	int pix;

	if( sw_surfcacheoverride.value )
	{
		size = sw_surfcacheoverride.value;
	}
	else
	{
		size = SURFCACHE_SIZE_AT_320X240 * 2;

		pix = vid.width * vid.height * 2;
		if( pix > 64000 )
			size += ( pix - 64000 ) * 3;
	}
#endif

#if XASH_GAMECUBE
	if( gEngfuncs.Sys_CheckParm( "-gcmap" ))
	{
		if( sc_base )
		{
			D_FlushCaches();
			free( sc_base );
			sc_base = sc_rover = NULL;
		}
		R_TryInitGcmapSurfaceCache();
		return;
	}
#endif

	// round up to page size
	size = ( size + 8191 ) & ~8191;

	gEngfuncs.Con_Printf( "%s surface cache\n", Q_memprint( size ));

	sc_size = size;
	if( sc_base )
	{
		D_FlushCaches(  );
#if XASH_GAMECUBE
		R_FreeGameCubeSurfaceCache();
#else
		Mem_Free( sc_base );
#endif
	}
#if XASH_GAMECUBE
	if( R_TryInitGameCubeSurfaceCacheSized( size, true ))
		return;
#endif
	sc_base = (surfcache_t *)Mem_Calloc( r_temppool, size );
	sc_rover = sc_base;
#if XASH_GAMECUBE
	gc_sc_static = false;
	gc_sc_heap = false;
#endif

	sc_base->next = NULL;
	sc_base->owner = NULL;
	sc_base->size = sc_size;
}

#if XASH_GAMECUBE
static void R_FreeGameCubeSurfaceCache( void )
{
	if( !sc_base || gc_sc_static )
		return;

	if( gc_sc_heap )
		free( sc_base );
	else
		Mem_Free( sc_base );
}

qboolean R_TryInitGcmapSurfaceCache( void )
{
	int size, try_size;

	if( sc_base )
		return true;

	if( !gEngfuncs.Sys_CheckParm( "-gcmap" ))
		return false;

	size = GC_SURFACE_CACHE_DEFAULT;
	if( size > GC_SURFACE_CACHE_MAX )
		size = GC_SURFACE_CACHE_MAX;

	/* G140: New Game must not take a 32/64 KiB crumb here — that starved
	 * materials.txt / HUD_Init. Prefer defer until landmark/static 128 KiB. */
	{
		const int min_heap = gEngfuncs.Sys_CheckParm( "-gcnewgame" ) ? 131072 : 32768;

		for( try_size = size; try_size >= min_heap; try_size >>= 1 )
		{
			int alloc_size = ( try_size + 8191 ) & ~8191;

			sc_base = malloc( alloc_size );
			if( !sc_base )
				continue;

			sc_size = alloc_size;
			sc_rover = sc_base;
			memset( sc_base, 0, alloc_size );
			sc_base->next = NULL;
			sc_base->owner = NULL;
			sc_base->size = sc_size;
			gc_sc_static = false;
			gc_sc_heap = true;
			gEngfuncs.Con_Reportf( "Xash3D GameCube: surface cache %s\n", Q_memprint( alloc_size ));
			return true;
		}
	}

	gEngfuncs.Con_Reportf( "Xash3D GameCube: surface cache deferred (%s unavailable)\n",
		Q_memprint( size ));
	return false;
}

static qboolean R_TryInitGameCubeSurfaceCacheSized( int size, qboolean allow_lowres_static )
{
	int try_size;

	if( sc_base )
		return true;

	if( size > GC_SURFACE_CACHE_MAX )
		size = GC_SURFACE_CACHE_MAX;

	for( try_size = size; try_size >= 32768; try_size >>= 1 )
	{
		int alloc_size = ( try_size + 8191 ) & ~8191;

		sc_base = malloc( alloc_size );
		if( !sc_base )
			continue;

		sc_size = alloc_size;
		sc_rover = sc_base;
		memset( sc_base, 0, alloc_size );
		sc_base->next = NULL;
		sc_base->owner = NULL;
		sc_base->size = sc_size;
		gc_sc_static = false;
		gc_sc_heap = true;
		gEngfuncs.Con_Reportf( "Xash3D GameCube: surface cache %s\n", Q_memprint( alloc_size ));
		return true;
	}

	if( allow_lowres_static )
		return R_TryInitLowResSurfaceCache();

	gEngfuncs.Con_Reportf( "Xash3D GameCube: surface cache deferred (%s unavailable)\n",
		Q_memprint( size ));
	return false;
}
#endif

#if XASH_GAMECUBE
void R_GcmapTrimSurfaceCache( void )
{
	if( sc_base )
	{
		D_FlushCaches();
		R_FreeGameCubeSurfaceCache();
		sc_base = sc_rover = NULL;
		gc_sc_static = false;
		gc_sc_heap = false;
	}
}

qboolean R_GcmapHasSurfaceCache( void )
{
	return sc_base != NULL;
}

qboolean R_TryInitLowResSurfaceCache( void )
{
	if( sc_base )
		return true;

	sc_base = (surfcache_t *)gc_lowres_surfcache_store;
	sc_size = GC_SURFACE_CACHE_LOWRES;
	sc_rover = sc_base;
	memset( sc_base, 0, sc_size );
	sc_base->next = NULL;
	sc_base->owner = NULL;
	sc_base->size = sc_size;
	gc_sc_static = true;
	gc_sc_heap = false;
	gEngfuncs.Con_Reportf( "Xash3D GameCube: low-res surface cache %s (static)\n",
		Q_memprint( sc_size ));
	return true;
}
#endif


/*
==================
D_FlushCaches
==================
*/
void D_FlushCaches( void )
{
	if( !sc_base )
		return;

	// if newmap, surfaces already freed
	if( !tr.map_unload )
	{
		for( surfcache_t *c = sc_base; c; c = c->next )
		{
			if( c->owner )
				*c->owner = NULL;
		}
	}

	sc_rover = sc_base;
	sc_base->next = NULL;
	sc_base->owner = NULL;
	sc_base->size = sc_size;
}

/*
=================
D_SCAlloc
=================
*/
static surfcache_t     *D_SCAlloc( int width, int size )
{
	surfcache_t *new;
	qboolean    wrapped_this_time;

	if( !sc_base )
		return NULL;

	if(( width < 0 ))// || (width > 256))
		gEngfuncs.Host_Error( "%s: bad cache width %d\n", __func__, width );

	if(( size <= 0 ) || ( size > 0x10000000 ))
	{
#if XASH_GAMECUBE
		if( gEngfuncs.Sys_CheckParm( "-gcmap" ) || GC_UseLowResWorldProbe() )
		{
			if( r_gc_surface_cache_skip_reports < 16 )
				gEngfuncs.Con_Reportf( "Xash3D GameCube: surface cache skip bad size %d\n", size );
			r_gc_surface_cache_skip_reports++;
			return NULL;
		}
#endif
		gEngfuncs.Host_Error( "%s: bad cache size %d\n", __func__, size );
	}

	size = offsetof( surfcache_t, data ) + size;
	size = ( size + 3 ) & ~3;
	if( size > sc_size )
	{
#if XASH_GAMECUBE
		if( gEngfuncs.Sys_CheckParm( "-gcmap" ) || GC_UseLowResWorldProbe() )
		{
			if( r_gc_surface_cache_skip_reports < 16 )
				gEngfuncs.Con_Reportf( "Xash3D GameCube: surface cache skip alloc=%i cache=%i\n",
					size, sc_size );
			r_gc_surface_cache_skip_reports++;
			return NULL;
		}
#endif
		gEngfuncs.Host_Error( "%s: %i > cache size of %i", __func__, size, sc_size );
	}

// if there is not size bytes after the rover, reset to the start
	wrapped_this_time = false;

	if( !sc_rover || (byte *)sc_rover - (byte *)sc_base > sc_size - size )
	{
		if( sc_rover )
		{
			wrapped_this_time = true;
		}
		sc_rover = sc_base;
	}

// colect and free surfcache_t blocks until the rover block is large enough
	new = sc_rover;
	if( sc_rover->owner )
		*sc_rover->owner = NULL;

	while( new->size < size )
	{
		// free another
		sc_rover = sc_rover->next;
			if( !sc_rover )
			{
#if XASH_GAMECUBE
				if( gEngfuncs.Sys_CheckParm( "-gcmap" ) || GC_UseLowResWorldProbe() )
				{
					if( r_gc_surface_cache_skip_reports < 16 )
						gEngfuncs.Con_Reportf( "Xash3D GameCube: surface cache exhausted alloc=%i cache=%i\n",
							size, sc_size );
					r_gc_surface_cache_skip_reports++;
					return NULL;
				}
#endif
				gEngfuncs.Host_Error( "%s: hit the end of memory", __func__ );
			}
		if( sc_rover->owner )
			*sc_rover->owner = NULL;

		new->size += sc_rover->size;
		new->next = sc_rover->next;
	}

// create a fragment out of any leftovers
	if( new->size - size > 256 )
	{
		sc_rover = (surfcache_t *)((byte *)new + size );
		sc_rover->size = new->size - size;
		sc_rover->next = new->next;
		sc_rover->width = 0;
		sc_rover->owner = NULL;
		new->next = sc_rover;
		new->size = size;
	}
	else
		sc_rover = new->next;

	new->width = width;
// DEBUG
	if( width > 0 )
		new->height = ( size - sizeof( *new ) + sizeof( new->data )) / width;

	new->owner = NULL; // should be set properly after return

	if( d_roverwrapped )
	{
		if( wrapped_this_time || ( sc_rover >= d_initial_rover ))
			r_cache_thrash = true;
	}
	else if( wrapped_this_time )
	{
		d_roverwrapped = true;
	}

	return new;
}

// =============================================================================
static void R_DrawSurfaceDecals( void )
{
	msurface_t *fa = r_drawsurf.surf;

#if XASH_GAMECUBE
	// Smoke quality 0 skips decals; New Game low-res keeps them in the surfcache.
	if( !GC_GetVisualQuality() && !GC_UseLowResWorldProbe() )
		return;
#endif

	for( decal_t *p = fa->pdecals; p; p = p->pnext )
	{
#if XASH_GAMECUBE
		if( GC_UseLowResWorldProbe() )
		{
			static qboolean gc_decal_marker_logged;

			if( !gc_decal_marker_logged )
			{
				gEngfuncs.Con_Reportf( "Xash3D GameCube: RGB565 world decals active\n" );
				gc_decal_marker_logged = true;
			}
		}
#endif
		pixel_t      *dest, *source;
		image_t      *tex = R_GetTexture( p->texture );
		int          s1 = 0, t1 = 0, s2 = tex->width, t2 = tex->height;
		unsigned int height;
		unsigned int f, fstep;
		int          skip;
		pixel_t      *buffer;
		qboolean     transparent;
		int          x, y, u, v, sv, w, h;
		vec3_t       basis[3];

		vec4_t textureU = Vec4( fa->texinfo->vecs[0] );
		vec4_t textureV = Vec4( fa->texinfo->vecs[1] );

		R_DecalComputeBasis( fa, 0, basis );

		w = fabs( tex->width * DotProduct( textureU, basis[0] ))
		    + fabs( tex->height * DotProduct( textureU, basis[1] ));
		h = fabs( tex->width * DotProduct( textureV, basis[0] ))
		    + fabs( tex->height * DotProduct( textureV, basis[1] ));

		// project decal center into the texture space of the surface
		x = DotProduct( p->position, textureU ) + textureU[3] - fa->texturemins[0] - w / 2;
		y = DotProduct( p->position, textureV ) + textureV[3] - fa->texturemins[1] - h / 2;

		x = x >> r_drawsurf.surfmip;
		y = y >> r_drawsurf.surfmip;
		w = w >> r_drawsurf.surfmip;
		h = h >> r_drawsurf.surfmip;

		if( w < 1 || h < 1 )
			continue;

		if( x < 0 )
		{
			s1 += ( -x ) * ( s2 - s1 ) / w;
			x = 0;
		}
		if( x + w > r_drawsurf.surfwidth )
		{
			s2 -= ( x + w - r_drawsurf.surfwidth ) * ( s2 - s1 ) / w;
			w = r_drawsurf.surfwidth - x;
		}
		if( y + h > r_drawsurf.surfheight )
		{
			t2 -= ( y + h - r_drawsurf.surfheight ) * ( t2 - t1 ) / h;
			h = r_drawsurf.surfheight - y;
		}

		if( s1 < 0 )
			s1 = 0;
		if( t1 < 0 )
			t1 = 0;

		if( s2 > tex->width )
			s2 = tex->width;
		if( t2 > tex->height )
			t2 = tex->height;

		if( !tex->pixels[0] || s1 >= s2 || t1 >= t2 || !w )
			continue;

		if( tex->alpha_pixels )
		{
			buffer = tex->alpha_pixels;
			transparent = true;
		}
		else
			buffer = tex->pixels[0];

		height = h;
		if( y < 0 )
		{
			skip = -y;
			height += y;
			y = 0;
		}
		else
			skip = 0;

		dest = ((pixel_t *)r_drawsurf.surfdat ) + y * r_drawsurf.rowbytes + x;

		for( v = 0; v < height; v++ )
		{
			// int alpha1 = vid.alpha;
			sv = ( skip + v ) * ( t2 - t1 ) / h + t1;
			source = buffer + sv * tex->width + s1;

			{
				f = 0;
				fstep = ( s2 - s1 ) * 0x10000 / w;
				if( w == s2 - s1 )
					fstep = 0x10000;

				for( u = 0; u < w; u++ )
				{
					pixel_t src = source[f >> 16];
					int     alpha = 7;
					f += fstep;

					if( transparent )
					{
						alpha &= src >> ( 16 - 3 );
						src = src << 3;
					}

					if( alpha <= 0 )
						continue;

#if XASH_GAMECUBE
					/* G140: cache is display RGB565 after lit BLEND_LM — unpack
					 * soft decal texels (and simple replace/blend). */
					if( GC_UseLowResWorldProbe() && r_gc_surf_cache_rgb565 )
					{
						pixel_t src565;

						if( src == TRANSPARENT_COLOR )
							continue;
						src565 = R_GCSoftMajorMinorToRGB565( src );
						if( alpha >= 7 )
							dest[u] = src565;
						else
						{
							pixel_t screen = dest[u];
							unsigned sr = ( src565 >> 11 ) & 0x1F, sg = ( src565 >> 5 ) & 0x3F, sb = src565 & 0x1F;
							unsigned dr = ( screen >> 11 ) & 0x1F, dg = ( screen >> 5 ) & 0x3F, db = screen & 0x1F;
							unsigned r = ( sr * (unsigned)alpha + dr * (unsigned)( 7 - alpha )) / 7u;
							unsigned g = ( sg * (unsigned)alpha + dg * (unsigned)( 7 - alpha )) / 7u;
							unsigned b = ( sb * (unsigned)alpha + db * (unsigned)( 7 - alpha )) / 7u;

							dest[u] = (pixel_t)(( r << 11 ) | ( g << 5 ) | b );
						}
						continue;
					}
#endif

					if( alpha < 7 )        // && (vid.rendermode == kRenderTransAlpha || vid.rendermode == kRenderTransTexture ) )
					{
						pixel_t screen = dest[u];         //  | 0xff & screen & src ;
						if( screen == TRANSPARENT_COLOR )
							continue;
						dest[u] = BLEND_ALPHA( alpha, src, screen );

					}
					else
						dest[u] = src;

				}
			}
			dest += r_drawsurf.rowbytes;
		}
	}

}

/*
================
D_CacheSurface
================
*/
surfcache_t *D_CacheSurface( msurface_t *surface, int miplevel )
{
	surfcache_t *cache;
//
// if the surface is animating or flashing, flush the cache
//
	r_drawsurf.image = R_GetTexture( R_TextureAnimation( surface )->gl_texturenum );

	// does not support conveyors with world luxels now
	if( surface->texinfo->flags & TEX_WORLD_LUXELS )
		surface->flags &= ~SURF_CONVEYOR;

	if( surface->flags & SURF_CONVEYOR )
	{
		if( miplevel >= 1 )
		{
			surface->extents[0] = surface->info->lightextents[0] * LM_SAMPLE_SIZE_AUTO( r_drawsurf.surf ) * 2;
			surface->info->lightmapmins[0] = -surface->info->lightextents[0] * LM_SAMPLE_SIZE_AUTO( r_drawsurf.surf );
		}
		else
		{
			surface->extents[0] = surface->info->lightextents[0] * LM_SAMPLE_SIZE_AUTO( r_drawsurf.surf );
			surface->info->lightmapmins[0] = -surface->info->lightextents[0] * LM_SAMPLE_SIZE_AUTO( r_drawsurf.surf ) / 2;
		}
	}
	/// todo: port this
	// r_drawsurf.lightadj[0] = r_newrefdef.lightstyles[surface->styles[0]].white*128;
	// r_drawsurf.lightadj[1] = r_newrefdef.lightstyles[surface->styles[1]].white*128;
	// r_drawsurf.lightadj[2] = r_newrefdef.lightstyles[surface->styles[2]].white*128;
	// r_drawsurf.lightadj[3] = r_newrefdef.lightstyles[surface->styles[3]].white*128;

//
// see if the cache holds apropriate data
//
	cache = CACHESPOT( surface )[miplevel];

#if XASH_GAMECUBE
	/* Quality 0 smoke skips heavy cache entries. New Game low-res caches
	 * conveyor / tiled / alpha faces so they are not neon flat-fills.
	 * Keep TF_SKY out — sky still uses the flat background path. */
	if( !GC_GetVisualQuality() )
	{
		qboolean skip_cache;

		if( GC_UseLowResWorldProbe() )
		{
			skip_cache = ( r_drawsurf.image->flags & TF_SKY ) ? true : false;
		}
		else
		{
			skip_cache = ( surface->dlightframe == tr.framecount ||
				( surface->flags & ( SURF_CONVEYOR | SURF_DRAWTILED )) ||
				( surface->texinfo->flags & TEX_WORLD_LUXELS ) ||
				( r_drawsurf.image->flags & ( TF_HAS_ALPHA | TF_SKY )));
		}
		if( skip_cache )
		{
			CACHESPOT( surface )[miplevel] = NULL;
			cache = NULL;
		}
	}
#endif

	// check for lightmap modification
	for( int maps = 0; maps < MAXLIGHTMAPS && surface->styles[maps] != 255; maps++ )
	{
		if( g_lightstylevalue[surface->styles[maps]] != surface->cached_light[maps] )
		{
			surface->dlightframe = tr.framecount;
		}
	}


	if( cache && !cache->dlight && surface->dlightframe != tr.framecount
	    && cache->image == r_drawsurf.image
	    && cache->lightadj[0] == r_drawsurf.lightadj[0]
	    && cache->lightadj[1] == r_drawsurf.lightadj[1]
	    && cache->lightadj[2] == r_drawsurf.lightadj[2]
	    && cache->lightadj[3] == r_drawsurf.lightadj[3] )
		return cache;

	if( surface->dlightframe == tr.framecount )
	{
		// invalidate dlight cache
		for( int i = 0; i < 4; i++ )
		{
			if( CACHESPOT( surface )[i] )
				CACHESPOT( surface )[i]->image = NULL;
		}
	}
//
// determine shape of surface
//
	surfscale = 1.0 / ( 1 << miplevel );
	r_drawsurf.surfmip = miplevel;
	if( surface->flags & SURF_CONVEYOR )
		r_drawsurf.surfwidth = surface->extents[0] >> miplevel;
	else
		r_drawsurf.surfwidth = surface->info->lightextents[0] >> miplevel;
	r_drawsurf.rowbytes = r_drawsurf.surfwidth;
	r_drawsurf.surfheight = surface->info->lightextents[1] >> miplevel;

	// use texture space if world luxels used
	if( surface->texinfo->flags & TEX_WORLD_LUXELS )
	{
		r_drawsurf.surfwidth = surface->extents[0] >> miplevel;
		r_drawsurf.rowbytes = r_drawsurf.surfwidth;
		r_drawsurf.surfheight = surface->extents[1] >> miplevel;
	}

#if XASH_GAMECUBE
	/* Low-res New Game: size from lightmap extents when present so light
	 * columns match; otherwise face extents (lightextents are often empty).
	 * G146: bump mip until the block fits ≤64×64 — never clamp width/height
	 * alone (that desyncs D_CalcGradients UV space → dark span cracks). */
	if( GC_UseLowResWorldProbe() )
	{
		int w, h;
		int mip = miplevel;
		int start_mip;
		const qboolean use_lm = !( surface->texinfo->flags & TEX_WORLD_LUXELS ) &&
			surface->info->lightextents[0] > 0 && surface->info->lightextents[1] > 0;

		if( use_lm )
		{
			w = surface->info->lightextents[0] >> mip;
			h = surface->info->lightextents[1] >> mip;
		}
		else
		{
			w = surface->extents[0] >> mip;
			h = surface->extents[1] >> mip;
		}

		while(( w <= 0 || h <= 0 ) && mip > 0 )
		{
			mip--;
			if( use_lm )
			{
				w = surface->info->lightextents[0] >> mip;
				h = surface->info->lightextents[1] >> mip;
			}
			else
			{
				w = surface->extents[0] >> mip;
				h = surface->extents[1] >> mip;
			}
		}
		start_mip = mip;
		while(( w > 64 || h > 64 ) && mip < 3 )
		{
			mip++;
			if( use_lm )
			{
				w = surface->info->lightextents[0] >> mip;
				h = surface->info->lightextents[1] >> mip;
			}
			else
			{
				w = surface->extents[0] >> mip;
				h = surface->extents[1] >> mip;
			}
		}
		if( w <= 0 )
			w = 16;
		if( h <= 0 )
			h = 16;
		/* mip0 light blocks are 16×16 — keep at least one full block so
		 * R_DrawSurface does not early-out after memset (flat teal). */
		if( w < 16 )
			w = 16;
		if( h < 16 )
			h = 16;
		/* Last resort if still huge at mip3 (rare). Keep UV space consistent
		 * by also raising surfscale to match the clamped block. */
		if( w > 64 || h > 64 )
		{
			float sx = ( w > 64 ) ? ( 64.0f / (float)w ) : 1.0f;
			float sy = ( h > 64 ) ? ( 64.0f / (float)h ) : 1.0f;
			float s = ( sx < sy ) ? sx : sy;

			if( w > 64 )
				w = 64;
			if( h > 64 )
				h = 64;
			surfscale = ( 1.0 / ( 1 << mip )) * s;
		}
		else
			surfscale = 1.0 / ( 1 << mip );
		if( mip != start_mip && r_gc_surface_cache_skip_reports < 8 )
		{
			gEngfuncs.Con_Reportf(
				"Xash3D GameCube: G146 surfcache mip %d→%d size %dx%d (UV-matched)\n",
				start_mip, mip, w, h );
			r_gc_surface_cache_skip_reports++;
		}
		r_drawsurf.surfmip = mip;
		r_drawsurf.surfwidth = w;
		r_drawsurf.surfheight = h;
		r_drawsurf.rowbytes = w;
	}
#endif


//
// allocate memory if needed
//
	if( !cache ) // if a texture just animated, don't reallocate it
	{
#if XASH_GAMECUBE
		// G24b: bound surface cache allocation size for low-memory quality 0
		// to avoid blowing budget on large animated/dynamic surfaces.
		// Quality 1/2 preserve existing behavior. New Game low-res already
		// UV-matched via G146 mip bump — do not clamp dimensions here.
		int alloc_width = r_drawsurf.surfwidth;
		int alloc_height = r_drawsurf.surfheight;
		if( GC_GetVisualQuality() == 0 && !GC_UseLowResWorldProbe() )
		{
			// Clamp to 64x64 for quality 0 smoke to preserve cache budget
			if( alloc_width > 64 )
				alloc_width = 64;
			if( alloc_height > 64 )
				alloc_height = 64;
			if( alloc_width != r_drawsurf.surfwidth || alloc_height != r_drawsurf.surfheight )
			{
				gEngfuncs.Con_Reportf(
					"Xash3D GameCube: clamping surface cache %dx%d to %dx%d (quality=0)\n",
					r_drawsurf.surfwidth, r_drawsurf.surfheight,
					alloc_width, alloc_height );
				r_drawsurf.surfwidth = alloc_width;
				r_drawsurf.surfheight = alloc_height;
				r_drawsurf.rowbytes = alloc_width;
			}
		}
		cache = D_SCAlloc( alloc_width, alloc_width * alloc_height * 2 );
#else
		cache = D_SCAlloc(
			r_drawsurf.surfwidth,
			( r_drawsurf.surfwidth * r_drawsurf.surfheight * 2 ));
#endif

		if( !cache )
			return NULL;

		CACHESPOT( surface )[miplevel] = cache;
		cache->owner = &CACHESPOT( surface )[miplevel];
		cache->mipscale = surfscale;
	}
#if XASH_GAMECUBE
	else if( GC_UseLowResWorldProbe() )
		cache->mipscale = surfscale;
#endif

	if( surface->dlightframe == tr.framecount )
		cache->dlight = 1;
	else
		cache->dlight = 0;

	r_drawsurf.surfdat = (pixel_t *)cache->data;

#if XASH_GAMECUBE
	/* Clear before lit draw so capped light blocks never leave garbage texels. */
	if( GC_UseLowResWorldProbe() && cache->width > 0 && r_drawsurf.surfheight > 0 )
	{
		memset( cache->data, 0, (size_t)cache->width * (size_t)r_drawsurf.surfheight * sizeof( pixel_t ));
		r_gc_surf_cache_rgb565 = false;
	}
#endif

	cache->image = r_drawsurf.image;
	cache->lightadj[0] = r_drawsurf.lightadj[0];
	cache->lightadj[1] = r_drawsurf.lightadj[1];
	cache->lightadj[2] = r_drawsurf.lightadj[2];
	cache->lightadj[3] = r_drawsurf.lightadj[3];
	R_UpdateSurfaceCachedLight( surface );
//
// draw and light the surface texture
//
	r_drawsurf.surf = surface;

	// c_surf++;


	{
#if XASH_GAMECUBE
		/* Quality 0 smoke skips lightmaps; New Game low-res builds them. */
		if( !GC_GetVisualQuality() && !GC_UseLowResWorldProbe() )
		{
			int sample_size = LM_SAMPLE_SIZE_AUTO( surface );
			int smax = ( surface->info->lightextents[0] / sample_size ) + 1;
			int tmax = ( surface->info->lightextents[1] / sample_size ) + 1;
			memset( blocklights, 0, sizeof( uint ) * smax * tmax );
		}
		else
#endif
		{
			// calculate the lightings
			R_BuildLightMap( );
		}
		// rasterize the surface into the cache
		R_DrawSurface();
#if XASH_GAMECUBE
		R_GCEnsureLowResSoftSurfaceCache( cache );
#endif
	}

#if XASH_GAMECUBE
	/* Smoke quality 0 skips world-luxels decals; New Game low-res draws them. */
	if( !GC_GetVisualQuality() && !GC_UseLowResWorldProbe()
	    && ( surface->texinfo->flags & TEX_WORLD_LUXELS ))
		return cache;
#endif
R_DrawSurfaceDecals();

#if XASH_GAMECUBE
	R_GCConvertLowResSurfaceCacheToRGB565( cache );
#endif

	return cache;
}
