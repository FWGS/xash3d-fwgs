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

drawsurf_t	r_drawsurf;

int				lightleft, sourcesstep, blocksize, sourcetstep;
int				lightdelta, lightdeltastep;
int				lightright, lightleftstep, lightrightstep, blockdivshift;
unsigned		blockdivmask;
void			*prowdestbase;
pixel_t	*pbasesource;
int				surfrowbytes;	// used by ASM files
unsigned		*r_lightptr;
int				r_stepback;
int				r_lightwidth;
int				r_numhblocks, r_numvblocks;
pixel_t	*r_source, *r_sourcemax;

void R_DrawSurfaceBlock8_mip0 (void);
void R_DrawSurfaceBlock8_mip1 (void);
void R_DrawSurfaceBlock8_mip2 (void);
void R_DrawSurfaceBlock8_mip3 (void);

static void	(*surfmiptable[4])(void) = {
	R_DrawSurfaceBlock8_mip0,
	R_DrawSurfaceBlock8_mip1,
	R_DrawSurfaceBlock8_mip2,
	R_DrawSurfaceBlock8_mip3
};

void R_BuildLightMap (void);
extern	unsigned		blocklights[1024];	// allow some very large lightmaps

float           surfscale;
qboolean        r_cache_thrash;         // set if surface cache is thrashing

int         sc_size;
surfcache_t	*sc_rover, *sc_base;

static int		rtable[MOD_FRAMES][MOD_FRAMES];

void R_InitRandomTable( void )
{
	int	tu, tv;

	// make random predictable
	gEngfuncs.COM_SetRandomSeed( 255 );

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
texture_t *R_TextureAnim( texture_t *b )
{
	texture_t *base = b;
	int	count, reletive;

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
		int	speed;

		// Quake1 textures uses 10 frames per second
		if( FBitSet( R_GetTexture( base->gl_texturenum )->flags, TF_QUAKEPAL ))
			speed = 10;
		else speed = 20;

		reletive = (int)(gpGlobals->time * speed) % base->anim_total;
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
texture_t *R_TextureAnimation( msurface_t *s )
{
	texture_t	*base = s->texinfo->texture;
	int	count, reletive;

	if( RI.currententity && RI.currententity->curstate.frame )
	{
		if( base->alternate_anims )
			base = base->alternate_anims;
	}

	if( !base->anim_total )
		return base;

	if( base->name[0] == '-' )
	{
		int	tx = (int)((s->texturemins[0] + (base->width << 16)) / base->width) % MOD_FRAMES;
		int	ty = (int)((s->texturemins[1] + (base->height << 16)) / base->height) % MOD_FRAMES;

		reletive = rtable[tx][ty] % base->anim_total;
	}
	else
	{
		int	speed;

		// Quake1 textures uses 10 frames per second
		if( FBitSet( R_GetTexture( base->gl_texturenum )->flags, TF_QUAKEPAL ))
			speed = 10;
		else speed = 20;

		reletive = (int)(gpGlobals->time * speed) % base->anim_total;
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

static char r_transtexture;
/*
===============
R_DrawSurface
===============
*/
void R_DrawSurface (void)
{
	pixel_t	*basetptr;
	int				smax, tmax, twidth;
	int				u;
	int				soffset, basetoffset, texwidth;
	int				horzblockstep;
	pixel_t	*pcolumndest;
	void			(*pblockdrawer)(void);
	image_t			*mt;

	surfrowbytes = r_drawsurf.rowbytes;

	mt = r_drawsurf.image;
	r_transtexture = mt->transparent;
	
	r_source = mt->pixels[r_drawsurf.surfmip];
	
// the fractional light values should range from 0 to (VID_GRADES - 1) << 16
// from a source range of 0 - 255
	
	texwidth = mt->width >> r_drawsurf.surfmip;

	blocksize = 16 >> r_drawsurf.surfmip;
	blockdivshift = 4 - r_drawsurf.surfmip;
	blockdivmask = (1 << blockdivshift) - 1;
	
	r_lightwidth = (r_drawsurf.surf->extents[0]>>4)+1;

	r_numhblocks = r_drawsurf.surfwidth >> blockdivshift;
	r_numvblocks = r_drawsurf.surfheight >> blockdivshift;

//==============================

	pblockdrawer = surfmiptable[r_drawsurf.surfmip];
// TODO: only needs to be set when there is a display settings change
	horzblockstep = blocksize;

	smax = mt->width >> r_drawsurf.surfmip;
	twidth = texwidth;
	tmax = mt->height >> r_drawsurf.surfmip;
	sourcetstep = texwidth;
	r_stepback = tmax * twidth;

	r_sourcemax = r_source + (tmax * smax);

	soffset = r_drawsurf.surf->texturemins[0];
	basetoffset = r_drawsurf.surf->texturemins[1];

// << 16 components are to guarantee positive values for %
	soffset = ((soffset >> r_drawsurf.surfmip) + (smax << 16)) % smax;
	basetptr = &r_source[((((basetoffset >> r_drawsurf.surfmip) 
		+ (tmax << 16)) % tmax) * twidth)];

	pcolumndest = r_drawsurf.surfdat;

	for (u=0 ; u<r_numhblocks; u++)
	{
		r_lightptr = blocklights + u;

		prowdestbase = pcolumndest;

		pbasesource = basetptr + soffset;

		(*pblockdrawer)();

		soffset = soffset + blocksize;
		if (soffset >= smax)
			soffset = 0;

		pcolumndest += horzblockstep;
	}
}


//=============================================================================

#if	!id386

/*
================
R_DrawSurfaceBlock8_mip0
================
*/
void R_DrawSurfaceBlock8_mip0 (void)
{
	int				v, i, b, lightstep, lighttemp, light;
	pixel_t	pix, *psource, *prowdest;

	psource = pbasesource;
	prowdest = prowdestbase;
	char transtexture = r_transtexture;

	for (v=0 ; v<r_numvblocks ; v++)
	{
	// FIXME: make these locals?
	// FIXME: use delta rather than both right and left, like ASM?
		//lightleft = r_lightptr[0];
		//lightright = r_lightptr[1];
		//r_lightptr += r_lightwidth;
		//lightleftstep = (r_lightptr[0] - lightleft) >> 4;
		//lightrightstep = (r_lightptr[1] - lightright) >> 4;

		for (i=0 ; i<16 ; i++)
		{
			lighttemp = lightleft - lightright;
			lightstep = lighttemp >> 4;

			light = lightright;

			for (b=15; b>=0; b--)
			{
				pix = psource[b];
				if( transtexture )
					pix = pix << 3;
				prowdest[b] = pix;
						//((unsigned char *)vid.colormap)
						//[(light & 0xFF00) + pix];
				light += lightstep;
			}
	
			psource += sourcetstep;
			//lightright += lightrightstep;
			//lightleft += lightleftstep;
			prowdest += surfrowbytes;
		}

		if (psource >= r_sourcemax)
			psource -= r_stepback;
	}
}


/*
================
R_DrawSurfaceBlock8_mip1
================
*/
void R_DrawSurfaceBlock8_mip1 (void)
{
	int				v, i, b, lightstep, lighttemp, light;
	pixel_t	pix, *psource, *prowdest;
	char transtexture = r_transtexture;

	psource = pbasesource;
	prowdest = prowdestbase;

	for (v=0 ; v<r_numvblocks ; v++)
	{
	// FIXME: make these locals?
	// FIXME: use delta rather than both right and left, like ASM?
		//lightleft = r_lightptr[0];
		//lightright = r_lightptr[1];
		//r_lightptr += r_lightwidth;
		//lightleftstep = (r_lightptr[0] - lightleft) >> 3;
		//lightrightstep = (r_lightptr[1] - lightright) >> 3;

		for (i=0 ; i<8 ; i++)
		{
			lighttemp = lightleft - lightright;
			lightstep = lighttemp >> 3;

			light = lightright;

			for (b=7; b>=0; b--)
			{
				pix = psource[b];
				if( transtexture )
					pix = pix << 3;
				prowdest[b] = pix;
						//((unsigned char *)vid.colormap)
						//[(light & 0xFF00) + pix];
				light += lightstep;
			}
	
			psource += sourcetstep;
			//lightright += lightrightstep;
			//lightleft += lightleftstep;
			prowdest += surfrowbytes;
		}

		if (psource >= r_sourcemax)
			psource -= r_stepback;
	}
}


/*
================
R_DrawSurfaceBlock8_mip2
================
*/
void R_DrawSurfaceBlock8_mip2 (void)
{
	int				v, i, b, lightstep, lighttemp, light;
	pixel_t	pix, *psource, *prowdest;
	char transtexture = r_transtexture;

	psource = pbasesource;
	prowdest = prowdestbase;

	for (v=0 ; v<r_numvblocks ; v++)
	{
	// FIXME: make these locals?
	// FIXME: use delta rather than both right and left, like ASM?
		//lightleft = r_lightptr[0];
		//lightright = r_lightptr[1];
		//r_lightptr += r_lightwidth;
		//lightleftstep = (r_lightptr[0] - lightleft) >> 2;
		//lightrightstep = (r_lightptr[1] - lightright) >> 2;

		for (i=0 ; i<4 ; i++)
		{
			lighttemp = lightleft - lightright;
			lightstep = lighttemp >> 2;

			light = lightright;

			for (b=3; b>=0; b--)
			{
				pix = psource[b];
				if( transtexture )
					pix = pix << 3;
				prowdest[b] = pix;
						//((unsigned char *)vid.colormap)
						//[(light & 0xFF00) + pix];
				light += lightstep;
			}
	
			psource += sourcetstep;
			//lightright += lightrightstep;
			//lightleft += lightleftstep;
			prowdest += surfrowbytes;
		}

		if (psource >= r_sourcemax)
			psource -= r_stepback;
	}
}


/*
================
R_DrawSurfaceBlock8_mip3
================
*/
void R_DrawSurfaceBlock8_mip3 (void)
{
	int				v, i, b, lightstep, lighttemp, light;
	pixel_t	pix, *psource, *prowdest;
	char transtexture = r_transtexture;

	psource = pbasesource;
	prowdest = prowdestbase;

	for (v=0 ; v<r_numvblocks ; v++)
	{
	// FIXME: make these locals?
	// FIXME: use delta rather than both right and left, like ASM?
		//lightleft = r_lightptr[0];
		//lightright = r_lightptr[1];
		//r_lightptr += r_lightwidth;
		//lightleftstep = (r_lightptr[0] - lightleft) >> 1;
		//lightrightstep = (r_lightptr[1] - lightright) >> 1;

		for (i=0 ; i<2 ; i++)
		{
			lighttemp = lightleft - lightright;
			lightstep = lighttemp >> 1;

			light = lightright;

			for (b=1; b>=0; b--)
			{
				pix = psource[b];
				if( transtexture )
					pix = pix << 3;
				prowdest[b] = pix;
						//((unsigned char *)vid.colormap)
						//[(light & 0xFF00) + pix];
				light += lightstep;
			}
	
			psource += sourcetstep;
			//lightright += lightrightstep;
			//lightleft += lightleftstep;
			prowdest += surfrowbytes;
		}

		if (psource >= r_sourcemax)
			psource -= r_stepback;
	}
}

#endif


//============================================================================


/*
================
R_InitCaches

================
*/
void R_InitCaches (void)
{
	int		size;
	int		pix;

	// calculate size to allocate
	if (sw_surfcacheoverride->value)
	{
		size = sw_surfcacheoverride->value;
	}
	else
	{
		size = SURFCACHE_SIZE_AT_320X240;

		pix =1920 * 1080 * 16;
		if (pix > 64000)
			size += (pix-64000)*3;
	}		

	// round up to page size
	size = (size + 8191) & ~8191;

	gEngfuncs.Con_Printf ("%ik surface cache\n", size/1024);

	sc_size = size;
	sc_base = (surfcache_t *)malloc(size);
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
void D_FlushCaches (void)
{
	surfcache_t     *c;
	
	if (!sc_base)
		return;

	for (c = sc_base ; c ; c = c->next)
	{
		if (c->owner)
			*c->owner = NULL;
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
surfcache_t     *D_SCAlloc (int width, int size)
{
	surfcache_t             *new;
	qboolean                wrapped_this_time;

	if ((width < 0) )// || (width > 256))
		gEngfuncs.Host_Error ("D_SCAlloc: bad cache width %d\n", width);

	if ((size <= 0) || (size > 0x10000000))
		gEngfuncs.Host_Error ("D_SCAlloc: bad cache size %d\n", size);
	
	size = (int)&((surfcache_t *)0)->data[size];
	size = (size + 3) & ~3;
	if (size > sc_size)
		gEngfuncs.Host_Error ("D_SCAlloc: %i > cache size of %i",size, sc_size);

// if there is not size bytes after the rover, reset to the start
	wrapped_this_time = false;

	if ( !sc_rover || (byte *)sc_rover - (byte *)sc_base > sc_size - size)
	{
		if (sc_rover)
		{
			wrapped_this_time = true;
		}
		sc_rover = sc_base;
	}
		
// colect and free surfcache_t blocks until the rover block is large enough
	new = sc_rover;
	if (sc_rover->owner)
		*sc_rover->owner = NULL;
	
	while (new->size < size)
	{
	// free another
		sc_rover = sc_rover->next;
		if (!sc_rover)
			gEngfuncs.Host_Error ("D_SCAlloc: hit the end of memory");
		if (sc_rover->owner)
			*sc_rover->owner = NULL;
			
		new->size += sc_rover->size;
		new->next = sc_rover->next;
	}

// create a fragment out of any leftovers
	if (new->size - size > 256)
	{
		sc_rover = (surfcache_t *)( (byte *)new + size);
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
	if (width > 0)
		new->height = (size - sizeof(*new) + sizeof(new->data)) / width;

	new->owner = NULL;              // should be set properly after return

	if (d_roverwrapped)
	{
		if (wrapped_this_time || (sc_rover >= d_initial_rover))
			r_cache_thrash = true;
	}
	else if (wrapped_this_time)
	{       
		d_roverwrapped = true;
	}

	return new;
}


/*
=================
D_SCDump
=================
*/
void D_SCDump (void)
{
	surfcache_t             *test;

	for (test = sc_base ; test ; test = test->next)
	{
		if (test == sc_rover)
			gEngfuncs.Con_Printf ("ROVER:\n");
		gEngfuncs.Con_Printf ("%p : %i bytes     %i width\n",test, test->size, test->width);
	}
}

//=============================================================================

// if the num is not a power of 2, assume it will not repeat

int     MaskForNum (int num)
{
	if (num==128)
		return 127;
	if (num==64)
		return 63;
	if (num==32)
		return 31;
	if (num==16)
		return 15;
	return 255;
}

int D_log2 (int num)
{
	int     c;
	
	c = 0;
	
	while (num>>=1)
		c++;
	return c;
}

//=============================================================================

/*
================
D_CacheSurface
================
*/
surfcache_t *D_CacheSurface (msurface_t *surface, int miplevel)
{
	surfcache_t     *cache;
//
// if the surface is animating or flashing, flush the cache
//
	r_drawsurf.image = R_GetTexture(R_TextureAnimation (surface)->gl_texturenum);

	/// todo: port this
	//r_drawsurf.lightadj[0] = r_newrefdef.lightstyles[surface->styles[0]].white*128;
	//r_drawsurf.lightadj[1] = r_newrefdef.lightstyles[surface->styles[1]].white*128;
	//r_drawsurf.lightadj[2] = r_newrefdef.lightstyles[surface->styles[2]].white*128;
	//r_drawsurf.lightadj[3] = r_newrefdef.lightstyles[surface->styles[3]].white*128;
	
//
// see if the cache holds apropriate data
//
	cache = CACHESPOT(surface)[miplevel];


	if (cache && !cache->dlight && surface->dlightframe != r_framecount
			&& cache->image == r_drawsurf.image
			&& cache->lightadj[0] == r_drawsurf.lightadj[0]
			&& cache->lightadj[1] == r_drawsurf.lightadj[1]
			&& cache->lightadj[2] == r_drawsurf.lightadj[2]
			&& cache->lightadj[3] == r_drawsurf.lightadj[3] )
		return cache;

//
// determine shape of surface
//
	surfscale = 1.0 / (1<<miplevel);
	r_drawsurf.surfmip = miplevel;
	r_drawsurf.surfwidth = surface->extents[0] >> miplevel;
	r_drawsurf.rowbytes = r_drawsurf.surfwidth;
	r_drawsurf.surfheight = surface->extents[1] >> miplevel;
	
//
// allocate memory if needed
//
	if (!cache)     // if a texture just animated, don't reallocate it
	{
		cache = D_SCAlloc (r_drawsurf.surfwidth,
						   r_drawsurf.surfwidth * r_drawsurf.surfheight * 2);
		CACHESPOT(surface)[miplevel] = cache;
		cache->owner = &CACHESPOT(surface)[miplevel];
		cache->mipscale = surfscale;
	}
	
	if (surface->dlightframe == r_framecount)
		cache->dlight = 1;
	else
		cache->dlight = 0;

	r_drawsurf.surfdat = (pixel_t *)cache->data;
	
	cache->image = r_drawsurf.image;
	cache->lightadj[0] = r_drawsurf.lightadj[0];
	cache->lightadj[1] = r_drawsurf.lightadj[1];
	cache->lightadj[2] = r_drawsurf.lightadj[2];
	cache->lightadj[3] = r_drawsurf.lightadj[3];

//
// draw and light the surface texture
//
	r_drawsurf.surf = surface;

	c_surf++;

	// calculate the lightings
	//R_BuildLightMap ();
	
	// rasterize the surface into the cache
	R_DrawSurface ();

	return cache;
}


