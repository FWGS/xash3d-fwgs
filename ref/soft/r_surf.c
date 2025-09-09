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

#include "r_local.h"
#include "mod_local.h"

drawsurf_t r_drawsurf;

uint       lightleft, sourcesstep, blocksize, sourcetstep;
uint       lightdelta, lightdeltastep;
uint       lightright, lightleftstep, lightrightstep, blockdivshift;
unsigned   blockdivmask;
void       *prowdestbase;
pixel_t    *pbasesource;
int        surfrowbytes;                        // used by ASM files
unsigned   *r_lightptr;
int        r_stepback;
int        r_lightwidth;
int        r_numhblocks, r_numvblocks;
pixel_t    *r_source, *r_sourcemax;

void R_DrawSurfaceBlock8_mip0( void );
void R_DrawSurfaceBlock8_mip1( void );
void R_DrawSurfaceBlock8_mip2( void );
void R_DrawSurfaceBlock8_mip3( void );
void R_DrawSurfaceBlock8_Generic( void );
void R_DrawSurfaceBlock8_World( void );

static float    worldlux_s, worldlux_t;

static void     (*surfmiptable[4])( void ) = {
	R_DrawSurfaceBlock8_mip0,
	R_DrawSurfaceBlock8_mip1,
	R_DrawSurfaceBlock8_mip2,
	R_DrawSurfaceBlock8_mip3
};

// void R_BuildLightMap (void);
extern unsigned blocklights[10240]; // allow some very large lightmaps

float           surfscale;
qboolean        r_cache_thrash;         // set if surface cache is thrashing

int sc_size;
surfcache_t     *sc_rover, *sc_base;

static int      rtable[MOD_FRAMES][MOD_FRAMES];

static void R_BuildLightMap( void );
/*
===============
R_AddDynamicLights
===============
*/
static void R_AddDynamicLights( const msurface_t *surf )
{
	const mextrasurf_t *info = surf->info;
	int        lnum, smax, tmax;
	int        sample_frac = 1.0;
	float      sample_size;
	mtexinfo_t *tex;

	// no dlighted surfaces here
	if( !surf->dlightbits )
		return;

	sample_size = gEngfuncs.Mod_SampleSizeForFace( surf );
	smax = ( info->lightextents[0] / sample_size ) + 1;
	tmax = ( info->lightextents[1] / sample_size ) + 1;
	tex = surf->texinfo;

	if( FBitSet( tex->flags, TEX_WORLD_LUXELS ))
	{
		if( surf->texinfo->faceinfo )
			sample_frac = surf->texinfo->faceinfo->texture_step;
		else if( FBitSet( surf->texinfo->flags, TEX_EXTRA_LIGHTMAP ))
			sample_frac = LM_SAMPLE_EXTRASIZE;
		else
			sample_frac = LM_SAMPLE_SIZE;
	}

	for( lnum = 0; lnum < MAX_DLIGHTS; lnum++ )
	{
		dlight_t *dl;
		vec3_t   impact, origin_l;
		float    dist, rad, minlight;
		float    sl, tl;
		int      t, monolight;

		if( !FBitSet( surf->dlightbits, BIT( lnum )))
			continue; // not lit by this light

		dl = &tr.dlights[lnum];

		// transform light origin to local bmodel space
		if( !tr.modelviewIdentity )
			Matrix4x4_VectorITransform( RI.objectMatrix, dl->origin, origin_l );
		else
			VectorCopy( dl->origin, origin_l );

		rad = dl->radius;
		dist = PlaneDiff( origin_l, surf->plane );
		rad -= fabs( dist );

		// rad is now the highest intensity on the plane
		minlight = dl->minlight;
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

		sl = DotProduct( impact, info->lmvecs[0] ) + info->lmvecs[0][3] - info->lightmapmins[0];
		tl = DotProduct( impact, info->lmvecs[1] ) + info->lmvecs[1][3] - info->lightmapmins[1];

		monolight = LightToTexGamma(( dl->color.r + dl->color.g + dl->color.b ) / 3 * 4 ) * 3;

		for( t = 0; t < tmax; t++ )
		{
			int td = ( tl - sample_size * t ) * sample_frac;
			int s;

			if( td < 0 )
				td = -td;

			for( s = 0; s < smax; s++ )
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
	int                map, t, i;
	const msurface_t   *surf = r_drawsurf.surf;
	const mextrasurf_t *info = surf->info;
	const int          sample_size = gEngfuncs.Mod_SampleSizeForFace( surf );
	int                smax = ( info->lightextents[0] / sample_size ) + 1;
	int                tmax = ( info->lightextents[1] / sample_size ) + 1;
	int                size = smax * tmax;

	if( FBitSet( surf->flags, SURF_CONVEYOR ))
	{
		smax = ( info->lightextents[0] * 3 / sample_size ) + 1;
		size = smax * tmax;
		memset( blocklights, 0xff, sizeof( uint ) * size );
		return;
	}

	memset( blocklights, 0, sizeof( uint ) * size );

	// add all the lightmaps
	for( map = 0; map < MAXLIGHTMAPS && surf->samples; map++ )
	{
		const color24 *lm = &surf->samples[map * size];
		uint          scale;
		int           i;

		if( surf->styles[map] >= 255 )
			break;

		scale = tr.lightstylevalue[surf->styles[map]];

		for( i = 0; i < size; i++ )
			blocklights[i] += ( lm[i].r + lm[i].g + lm[i].b ) * scale;
	}

	// add all the dynamic lights
	if( surf->dlightframe == tr.framecount )
		R_AddDynamicLights( surf );

	// bound, invert, and shift
	for( i = 0; i < size; i++ )
	{
		if( blocklights[i] < 65280 )
			t = LightToTexGamma( blocklights[i] >> 6 ) << 6;
		else
			t = (int)blocklights[i];

		t = bound( 0, t, 65535 * 3 );
		t = t / 2048 / 3; // (255*256 - t) >> (8 - VID_CBITS);

		// if (t < (1 << 6))
		// t = (1 << 6);
		t = t << 8;

		blocklights[i] = t;
	}
}

void GL_InitRandomTable( void )
{
	int tu, tv;

	for( tu = 0; tu < MOD_FRAMES; tu++ )
	{
		for( tv = 0; tv < MOD_FRAMES; tv++ )
		{
			rtable[tu][tv] = gEngfuncs.COM_RandomLong( 0, 0x7FFF );
		}
	}

	gEngfuncs.COM_SetRandomSeed( 0 );
}

/*
===============
R_TextureAnim

Returns the proper texture for a given time and base texture, do not process random tiling
===============
*/
static texture_t *R_TextureAnim( texture_t *b )
{
	texture_t *base = b;
	int       count, reletive;

	if( RI.currententity->curstate.frame )
	{
		if( base->alternate_anims )
			base = base->alternate_anims;
	}

	if( !base->anim_total )
		return base;
	if( base->name[0] == '-' )
	{
		return b; // already tiled
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


	count = 0;

	while( base->anim_min > reletive || base->anim_max <= reletive )
	{
		base = base->anim_next;

		if( !base || ++count > MOD_FRAMES )
			return b;
	}

	return base;
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
	int       count, reletive;

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

	count = 0;

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


// ==============================

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

#define BLEND_LM( pix, light ) vid.colormap[( pix >> 3 ) | (( light & 0x1f00 ) << 5 )] | ( pix & 7 );

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

	psource = pbasesource;
	prowdest = prowdestbase;

	for( v = 0; v < r_numvblocks; v++ )
	{
		// FIXME: make these locals?
		// FIXME: use delta rather than both right and left, like ASM?
		lightleft = r_lightptr[( lightpos / r_lightwidth ) * r_lightwidth];
		lightright = r_lightptr[( lightpos / r_lightwidth ) * r_lightwidth + 1];
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
				prowdest[b] = BLEND_LM( pix, light );;
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
				prowdest[b] = BLEND_LM( pix, light );;
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
	int pix;

	// calculate size to allocate
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

	// round up to page size
	size = ( size + 8191 ) & ~8191;

	gEngfuncs.Con_Printf( "%s surface cache\n", Q_memprint( size ));

	sc_size = size;
	if( sc_base )
	{
		D_FlushCaches(  );
		Mem_Free( sc_base );
	}
	sc_base = (surfcache_t *)Mem_Calloc( r_temppool, size );
	sc_rover = sc_base;

	sc_base->next = NULL;
	sc_base->owner = NULL;
	sc_base->size = sc_size;
}


/*
==================
D_FlushCaches
==================
*/
void D_FlushCaches( void )
{
	surfcache_t *c;

	// if newmap, surfaces already freed
	if( !tr.map_unload )
	{
		for( c = sc_base; c; c = c->next )
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

	if(( width < 0 ))// || (width > 256))
		gEngfuncs.Host_Error( "%s: bad cache width %d\n", __func__, width );

	if(( size <= 0 ) || ( size > 0x10000000 ))
		gEngfuncs.Host_Error( "%s: bad cache size %d\n", __func__, size );

	size = (int)&((surfcache_t *)0 )->data[size];
	size = ( size + 3 ) & ~3;
	if( size > sc_size )
		gEngfuncs.Host_Error( "%s: %i > cache size of %i", __func__, size, sc_size );

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
			gEngfuncs.Host_Error( "%s: hit the end of memory", __func__ );
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
	decal_t    *p;

	for( p = fa->pdecals; p; p = p->pnext )
	{
		pixel_t      *dest, *source;
		vec4_t       textureU, textureV;
		image_t      *tex = R_GetTexture( p->texture );
		int          s1 = 0, t1 = 0, s2 = tex->width, t2 = tex->height;
		unsigned int height;
		unsigned int f, fstep;
		int          skip;
		pixel_t      *buffer;
		qboolean     transparent;
		int          x, y, u, v, sv, w, h;
		vec3_t       basis[3];

		Vector4Copy( fa->texinfo->vecs[0], textureU );
		Vector4Copy( fa->texinfo->vecs[1], textureV );

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
	int         maps;
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

	// check for lightmap modification
	for( maps = 0; maps < MAXLIGHTMAPS && surface->styles[maps] != 255; maps++ )
	{
		if( tr.lightstylevalue[surface->styles[maps]] != surface->cached_light[maps] )
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
		int i;
		// invalidate dlight cache
		for( i = 0; i < 4; i++ )
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


//
// allocate memory if needed
//
	if( !cache ) // if a texture just animated, don't reallocate it
	{
		cache = D_SCAlloc( r_drawsurf.surfwidth,
				   r_drawsurf.surfwidth * r_drawsurf.surfheight * 2 );
		CACHESPOT( surface )[miplevel] = cache;
		cache->owner = &CACHESPOT( surface )[miplevel];
		cache->mipscale = surfscale;
	}

	if( surface->dlightframe == tr.framecount )
		cache->dlight = 1;
	else
		cache->dlight = 0;

	r_drawsurf.surfdat = (pixel_t *)cache->data;

	cache->image = r_drawsurf.image;
	cache->lightadj[0] = r_drawsurf.lightadj[0];
	cache->lightadj[1] = r_drawsurf.lightadj[1];
	cache->lightadj[2] = r_drawsurf.lightadj[2];
	cache->lightadj[3] = r_drawsurf.lightadj[3];
	for( maps = 0; maps < MAXLIGHTMAPS && surface->styles[maps] != 255; maps++ )
	{
		surface->cached_light[maps] = tr.lightstylevalue[surface->styles[maps]];
	}
//
// draw and light the surface texture
//
	r_drawsurf.surf = surface;

	// c_surf++;

	// calculate the lightings
	R_BuildLightMap( );

	// rasterize the surface into the cache
	R_DrawSurface();
	R_DrawSurfaceDecals();

	return cache;
}


