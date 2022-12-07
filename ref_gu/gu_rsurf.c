/*
gu_rsurf.c - surface-related refresh code
Copyright (C) 2010 Uncle Mike
Copyright (C) 2021 Sergey Galushko

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "gu_local.h"
#include "xash3d_mathlib.h"
#include "mod_local.h"

typedef struct
{
	int		allocated[BLOCK_SIZE_MAX];
	int		current_lightmap_texture;
	msurface_t	*dynamic_surfaces;
	msurface_t	*lightmap_surfaces[MAX_LIGHTMAPS];
	byte		lightmap_buffer[BLOCK_SIZE_MAX*BLOCK_SIZE_MAX*LIGHTMAP_BPP];
} gllightmapstate_t;

static int		nColinElim; // stats
static vec2_t		world_orthocenter;
static vec2_t		world_orthohalf;
static uint		r_blocklights[BLOCK_SIZE_MAX*BLOCK_SIZE_MAX*3];
static mextrasurf_t		*fullbright_surfaces[MAX_TEXTURES];
static mextrasurf_t		*detail_surfaces[MAX_TEXTURES];
static int		rtable[MOD_FRAMES][MOD_FRAMES];
static qboolean		draw_alpha_surfaces = false;
static qboolean		draw_fullbrights = false;
static qboolean		draw_details = false;
static msurface_t		*skychain = NULL;
static gllightmapstate_t	gl_lms;
static void LM_UploadBlock( qboolean dynamic );

byte *Mod_GetCurrentVis( void )
{
	if( gEngfuncs.drawFuncs->Mod_GetCurrentVis && tr.fCustomRendering )
		return gEngfuncs.drawFuncs->Mod_GetCurrentVis();
	return RI.visbytes;
}

void Mod_SetOrthoBounds( const float *mins, const float *maxs )
{
	if( gEngfuncs.drawFuncs->GL_OrthoBounds )
	{
		gEngfuncs.drawFuncs->GL_OrthoBounds( mins, maxs );
	}

	Vector2Average( maxs, mins, world_orthocenter );
	Vector2Subtract( maxs, world_orthocenter, world_orthohalf );
}

static void BoundPoly( int numverts, float *verts, vec3_t mins, vec3_t maxs )
{
	int	i, j;
	float	*v;

	ClearBounds( mins, maxs );

	for( i = 0, v = verts; i < numverts; i++ )
	{
		for( j = 0; j < 3; j++, v++ )
		{
			if( *v < mins[j] ) mins[j] = *v;
			if( *v > maxs[j] ) maxs[j] = *v;
		}
	}
}

static void SubdividePolygon_r( msurface_t *warpface, int numverts, float *verts )
{
	vec3_t		front[SUBDIVIDE_SIZE], back[SUBDIVIDE_SIZE];
	mextrasurf_t	*warpinfo = warpface->info;
	float		dist[SUBDIVIDE_SIZE];
	float		m, frac, s, t, *v;
	int		i, j, k, f, b;
	float		sample_size;
	vec3_t		mins, maxs;
	glpoly_t		*poly;
	model_t *loadmodel = gEngfuncs.Mod_GetCurrentLoadingModel();

	if( numverts > ( SUBDIVIDE_SIZE - 4 ))
		gEngfuncs.Host_Error( "Mod_SubdividePolygon: too many vertexes on face ( %i )\n", numverts );

	sample_size = gEngfuncs.Mod_SampleSizeForFace( warpface );
	BoundPoly( numverts, verts, mins, maxs );

	for( i = 0; i < 3; i++ )
	{
		m = ( mins[i] + maxs[i] ) * 0.5f;
		m = gl_subdivide_size->value * floor( m / gl_subdivide_size->value + 0.5f );
		if( maxs[i] - m < 8 ) continue;
		if( m - mins[i] < 8 ) continue;

		// cut it
		v = verts + i;
		for( j = 0; j < numverts; j++, v += 3 )
			dist[j] = *v - m;

		// wrap cases
		dist[j] = dist[0];
		v -= i;
		VectorCopy( verts, v );

		f = b = 0;
		v = verts;
		for( j = 0; j < numverts; j++, v += 3 )
		{
			if( dist[j] >= 0 )
			{
				VectorCopy( v, front[f] );
				f++;
			}

			if( dist[j] <= 0 )
			{
				VectorCopy (v, back[b]);
				b++;
			}

			if( dist[j] == 0 || dist[j+1] == 0 )
				continue;

			if(( dist[j] > 0 ) != ( dist[j+1] > 0 ))
			{
				// clip point
				frac = dist[j] / ( dist[j] - dist[j+1] );
				for( k = 0; k < 3; k++ )
					front[f][k] = back[b][k] = v[k] + frac * (v[3+k] - v[k]);
				f++;
				b++;
			}
		}

		SubdividePolygon_r( warpface, f, front[0] );
		SubdividePolygon_r( warpface, b, back[0] );
		return;
	}

	if( numverts != 4 )
		ClearBits( warpface->flags, SURF_DRAWTURB_QUADS );

	// add a point in the center to help keep warp valid
	poly = Mem_Calloc( loadmodel->mempool, sizeof( glpoly_t ) + ( numverts * 2 - 1 ) * sizeof( gu_vert_t ) );
	poly->next = warpface->polys;
	poly->flags = warpface->flags;
	warpface->polys = poly;
	poly->numverts = numverts;

	for( i = 0; i < numverts; i++, verts += 3 )
	{
		if( FBitSet( warpface->flags, SURF_DRAWTURB ))
		{
			s = DotProduct( verts, warpface->texinfo->vecs[0] );
			t = DotProduct( verts, warpface->texinfo->vecs[1] );
		}
		else
		{
			s = DotProduct( verts, warpface->texinfo->vecs[0] ) + warpface->texinfo->vecs[0][3];
			t = DotProduct( verts, warpface->texinfo->vecs[1] ) + warpface->texinfo->vecs[1][3];
			s /= warpface->texinfo->texture->width;
			t /= warpface->texinfo->texture->height;
		}

		poly->verts[i].uv[0] = s;
		poly->verts[i].uv[1] = t;
		VectorCopy( verts, poly->verts[i].xyz );

		// for speed reasons
		if( !FBitSet( warpface->flags, SURF_DRAWTURB ))
		{
			// lightmap texture coordinates
			s = DotProduct( verts, warpinfo->lmvecs[0] ) + warpinfo->lmvecs[0][3];
			s -= warpinfo->lightmapmins[0];
			s += warpface->light_s * sample_size;
			s += sample_size * 0.5f;
			s /= BLOCK_SIZE * sample_size; //fa->texinfo->texture->width;

			t = DotProduct( verts, warpinfo->lmvecs[1] ) + warpinfo->lmvecs[1][3];
			t -= warpinfo->lightmapmins[1];
			t += warpface->light_t * sample_size;
			t += sample_size * 0.5f;
			t /= BLOCK_SIZE * sample_size; //fa->texinfo->texture->height;

			poly->verts[i + numverts].uv[0] = s;
			poly->verts[i + numverts].uv[1] = t;
			VectorCopy( verts, poly->verts[i + numverts].xyz );
		}
	}
}

void GL_SetupFogColorForSurfaces( void )
{
	vec3_t	fogColor;
	float	factor, div;

	if( !glState.isFogEnabled)
		return;

	if( RI.currententity && RI.currententity->curstate.rendermode == kRenderTransTexture )
	{
#if 1
		glState.fogColor = GUCOLOR4F( RI.fogColor[0], RI.fogColor[1], RI.fogColor[2], glState.fogDensity );
		sceGuFog( glState.fogStart, glState.fogEnd, glState.fogColor );
#else
		pglFogfv( GL_FOG_COLOR, RI.fogColor );
#endif
		return;
	}

	div = (r_detailtextures->value) ? 2.0f : 1.0f;
	factor = (r_detailtextures->value) ? 3.0f : 2.0f;
	fogColor[0] = pow( RI.fogColor[0] / div, ( 1.0f / factor ));
	fogColor[1] = pow( RI.fogColor[1] / div, ( 1.0f / factor ));
	fogColor[2] = pow( RI.fogColor[2] / div, ( 1.0f / factor ));
#if 1
	glState.fogColor = GUCOLOR4F( fogColor[0], fogColor[1], fogColor[2], glState.fogDensity );
	sceGuFog( glState.fogStart, glState.fogEnd, glState.fogColor );
#else
	pglFogfv( GL_FOG_COLOR, fogColor );
#endif
}

void GL_ResetFogColor( void )
{
	// restore fog here
#if 1
	if( !glState.isFogEnabled )
		return;

	glState.fogColor = GUCOLOR4F( RI.fogColor[0], RI.fogColor[1], RI.fogColor[2], glState.fogDensity );
	sceGuFog( glState.fogStart, glState.fogEnd, glState.fogColor );
#else
	if( glState.isFogEnabled )
		pglFogfv( GL_FOG_COLOR, RI.fogColor );
#endif
}

/*
================
GL_SubdivideSurface

Breaks a polygon up along axial 64 unit
boundaries so that turbulent and sky warps
can be done reasonably.
================
*/
void GL_SubdivideSurface( msurface_t *fa )
{
	vec3_t	verts[SUBDIVIDE_SIZE];
	int	numverts;
	int	i, lindex;
	float	*vec;
	model_t *loadmodel = gEngfuncs.Mod_GetCurrentLoadingModel();

	// convert edges back to a normal polygon
	numverts = 0;
	for( i = 0; i < fa->numedges; i++ )
	{
		lindex = loadmodel->surfedges[fa->firstedge + i];

		if( lindex > 0 ) vec = loadmodel->vertexes[loadmodel->edges[lindex].v[0]].position;
		else vec = loadmodel->vertexes[loadmodel->edges[-lindex].v[1]].position;
		VectorCopy( vec, verts[numverts] );
		numverts++;
	}

	SetBits( fa->flags, SURF_DRAWTURB_QUADS ); // predict state

	// do subdivide
	SubdividePolygon_r( fa, numverts, verts[0] );
}

/*
================
GL_BuildPolygonFromSurface
================
*/
void GL_BuildPolygonFromSurface( model_t *mod, msurface_t *fa )
{
	int				i, lindex, lnumverts;
	medge_t			*pedges, *r_pedge;
	mextrasurf_t	*info = fa->info;
	float			sample_size;
	texture_t		*tex;
	gl_texture_t	*glt;
	float			*vec;
	float			s, t;
	glpoly_t		*poly;
	int 			num_removed = 0;

	if( !mod || !fa->texinfo || !fa->texinfo->texture )
		return; // bad polygon ?

	if( FBitSet( fa->flags, SURF_CONVEYOR ) && fa->texinfo->texture->gl_texturenum != 0 )
	{
		glt = R_GetTexture( fa->texinfo->texture->gl_texturenum );
		tex = fa->texinfo->texture;
		Assert( glt != NULL && tex != NULL );

		// update conveyor widths for keep properly speed of scrolling
		glt->srcWidth = tex->width;
		glt->srcHeight = tex->height;
	}

	sample_size = gEngfuncs.Mod_SampleSizeForFace( fa );

	// reconstruct the polygon
	pedges = mod->edges;
	lnumverts = fa->numedges;

	// detach if already created, reconstruct again
	poly = fa->polys;
	fa->polys = NULL;

	// quake simple models (healthkits etc) need to be reconstructed their polys because LM coords has changed after the map change
	// PSP GU-friendly format s1t1 xyz s2t2 xyz (Idea from PSP Quake)
	poly = Mem_Realloc( mod->mempool, poly, sizeof( glpoly_t ) + ( lnumverts * 2 - 1 ) * sizeof( gu_vert_t ));
	poly->next = fa->polys;
	poly->flags = fa->flags;
	fa->polys = poly;
	poly->numverts = lnumverts;

	for( i = 0; i < lnumverts; i++ )
	{
		lindex = mod->surfedges[fa->firstedge + i];

		if( lindex > 0 )
		{
			r_pedge = &pedges[lindex];
			vec = mod->vertexes[r_pedge->v[0]].position;
		}
		else
		{
			r_pedge = &pedges[-lindex];
			vec = mod->vertexes[r_pedge->v[1]].position;
		}

		s = DotProduct( vec, fa->texinfo->vecs[0] ) + fa->texinfo->vecs[0][3];
		s /= fa->texinfo->texture->width;

		t = DotProduct( vec, fa->texinfo->vecs[1] ) + fa->texinfo->vecs[1][3];
		t /= fa->texinfo->texture->height;

		poly->verts[i].uv[0] = s;
		poly->verts[i].uv[1] = t;
		VectorCopy( vec, poly->verts[i].xyz );

		// lightmap texture coordinates
		s = DotProduct( vec, info->lmvecs[0] ) + info->lmvecs[0][3];
		s -= info->lightmapmins[0];
		s += fa->light_s * sample_size;
		s += sample_size * 0.5f;
		s /= BLOCK_SIZE * sample_size; //fa->texinfo->texture->width;

		t = DotProduct( vec, info->lmvecs[1] ) + info->lmvecs[1][3];
		t -= info->lightmapmins[1];
		t += fa->light_t * sample_size;
		t += sample_size * 0.5f;
		t /= BLOCK_SIZE * sample_size; //fa->texinfo->texture->height;

		poly->verts[i + poly->numverts].uv[0] = s;
		poly->verts[i + poly->numverts].uv[1] = t;
		VectorCopy( vec, poly->verts[i + poly->numverts].xyz );
	}

	// remove co-linear points - Ed
	if( !CVAR_TO_BOOL( gl_keeptjunctions ) && !FBitSet( fa->flags, SURF_UNDERWATER ))
	{
		for( i = 0; i < lnumverts; i++ )
		{
			vec3_t		v1, v2;
			gu_vert_t	*prev, *this, *next;

			prev = &poly->verts[(i + lnumverts - 1) % lnumverts];
			next = &poly->verts[(i + 1) % lnumverts];
			this = &poly->verts[i];

			VectorSubtract( this->xyz, prev->xyz, v1 );
			VectorNormalize( v1 );
			VectorSubtract( next->xyz, prev->xyz, v2 );
			VectorNormalize( v2 );

			// skip co-linear points
			if(( fabs( v1[0] - v2[0] ) <= 0.001f) && (fabs( v1[1] - v2[1] ) <= 0.001f) && (fabs( v1[2] - v2[2] ) <= 0.001f))
			{
				int	j, k;

				for( j = i + 1; j < lnumverts; j++ )
				{
					poly->verts[j - 1] = poly->verts[j];
					poly->verts[poly->numverts + j - 1] = poly->verts[poly->numverts + j];
				}

				// retry next vertex next time, which is now current vertex
				lnumverts--;
				nColinElim++;
				num_removed++;
				i--;
			}
		}

		if (num_removed > 0)
		{
			for (i = poly->numverts; i < poly->numverts + lnumverts; i++)
				poly->verts[i - num_removed] = poly->verts[i];
		}
	}

	poly->numverts = lnumverts;
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

/*
===============
R_AddDynamicLights
===============
*/
void R_AddDynamicLights( msurface_t *surf )
{
	float		dist, rad, minlight;
	int		lnum, s, t, sd, td, smax, tmax;
	float		sl, tl, sacc, tacc;
	vec3_t		impact, origin_l;
	mextrasurf_t	*info = surf->info;
	int		sample_frac = 1.0;
	float		sample_size;
	mtexinfo_t	*tex;
	dlight_t		*dl;
	uint		*bl;

	// no dlighted surfaces here
	if( !R_CountSurfaceDlights( surf )) return;

	sample_size = gEngfuncs.Mod_SampleSizeForFace( surf );
	smax = (info->lightextents[0] / sample_size) + 1;
	tmax = (info->lightextents[1] / sample_size) + 1;
	tex = surf->texinfo;

	if( FBitSet( tex->flags, TEX_WORLD_LUXELS ))
	{
		if( surf->texinfo->faceinfo )
			sample_frac = surf->texinfo->faceinfo->texture_step;
		else if( FBitSet( surf->texinfo->flags, TEX_EXTRA_LIGHTMAP ))
			sample_frac = LM_SAMPLE_EXTRASIZE;
		else sample_frac = LM_SAMPLE_SIZE;
	}

	for( lnum = 0, dl = gEngfuncs.GetDynamicLight( 0 ); lnum < MAX_DLIGHTS; lnum++, dl++ )
	{
		if( !FBitSet( surf->dlightbits, BIT( lnum )))
			continue;	// not lit by this light

		// transform light origin to local bmodel space
		if( !tr.modelviewIdentity )
			Matrix4x4_VectorITransform( RI.objectMatrix, dl->origin, origin_l );
		else VectorCopy( dl->origin, origin_l );

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
		else VectorMA( origin_l, -dist, surf->plane->normal, impact );

		sl = DotProduct( impact, info->lmvecs[0] ) + info->lmvecs[0][3] - info->lightmapmins[0];
		tl = DotProduct( impact, info->lmvecs[1] ) + info->lmvecs[1][3] - info->lightmapmins[1];
		bl = r_blocklights;

		for( t = 0, tacc = 0; t < tmax; t++, tacc += sample_size )
		{
			td = (tl - tacc) * sample_frac;
			if( td < 0 ) td = -td;

			for( s = 0, sacc = 0; s < smax; s++, sacc += sample_size, bl += 3 )
			{
				sd = (sl - sacc) * sample_frac;
				if( sd < 0 ) sd = -sd;

				if( sd > td ) dist = sd + (td >> 1);
				else dist = td + (sd >> 1);

				if( dist < minlight )
				{
					bl[0] += ((int)((rad - dist) * 256) * gEngfuncs.LightToTexGamma( dl->color.r )) / 256;
					bl[1] += ((int)((rad - dist) * 256) * gEngfuncs.LightToTexGamma( dl->color.g )) / 256;
					bl[2] += ((int)((rad - dist) * 256) * gEngfuncs.LightToTexGamma( dl->color.b )) / 256;
				}
			}
		}
	}
}

/*
================
R_SetCacheState
================
*/
void R_SetCacheState( msurface_t *surf )
{
	int	maps;

	for( maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++ )
	{
		surf->cached_light[maps] = tr.lightstylevalue[surf->styles[maps]];
	}
}

/*
=============================================================================

  LIGHTMAP ALLOCATION

=============================================================================
*/
static void LM_InitBlock( void )
{
	memset( gl_lms.allocated, 0, sizeof( gl_lms.allocated ));
}

static int LM_AllocBlock( int w, int h, int *x, int *y )
{
	int	i, j;
	int	best, best2;

	best = BLOCK_SIZE;

	for( i = 0; i < BLOCK_SIZE - w; i++ )
	{
		best2 = 0;

		for( j = 0; j < w; j++ )
		{
			if( gl_lms.allocated[i+j] >= best )
				break;
			if( gl_lms.allocated[i+j] > best2 )
				best2 = gl_lms.allocated[i+j];
		}

		if( j == w )
		{
			// this is a valid spot
			*x = i;
			*y = best = best2;
		}
	}

	if( best + h > BLOCK_SIZE )
		return false;

	for( i = 0; i < w; i++ )
		gl_lms.allocated[*x + i] = best + h;

	return true;
}

static void LM_UploadBlock( qboolean dynamic )
{
	int	i;

	if( dynamic )
	{
		int	height = 0;

		for( i = 0; i < BLOCK_SIZE; i++ )
		{
			if( gl_lms.allocated[i] > height )
				height = gl_lms.allocated[i];
		}

		GL_UpdateTexture( tr.dlightTexture, 0, 0, BLOCK_SIZE, height, gl_lms.lightmap_buffer );
		GL_Bind( XASH_TEXTURE0, tr.dlightTexture );
	}
	else
	{
		rgbdata_t	r_lightmap;
		char	lmName[16];

		i = gl_lms.current_lightmap_texture;

		// upload static lightmaps only during loading
		memset( &r_lightmap, 0, sizeof( r_lightmap ));
		Q_snprintf( lmName, sizeof( lmName ), "*lightmap%i", i );

		r_lightmap.width = BLOCK_SIZE;
		r_lightmap.height = BLOCK_SIZE;
		r_lightmap.type = LIGHTMAP_FORMAT;
		r_lightmap.size = r_lightmap.width * r_lightmap.height * LIGHTMAP_BPP;
		r_lightmap.flags = IMAGE_HAS_COLOR;
		r_lightmap.buffer = gl_lms.lightmap_buffer;
		tr.lightmapTextures[i] = GL_LoadTextureInternal( lmName, &r_lightmap, TF_NOMIPMAP|TF_ATLAS_PAGE );

		if( ++gl_lms.current_lightmap_texture == MAX_LIGHTMAPS )
			gEngfuncs.Host_Error( "AllocBlock: full\n" );
	}
}

/*
=================
R_BuildLightmap

Combine and scale multiple lightmaps into the floating
format in r_blocklights
=================
*/
static void R_BuildLightMap( msurface_t *surf, byte *dest, int stride, qboolean dynamic )
{
	int		smax, tmax;
	uint		*bl, scale;
	int		i, map, size, s, t;
	int		sample_size;
	mextrasurf_t	*info = surf->info;
	color24		*lm;
	byte		color[3];

	sample_size = gEngfuncs.Mod_SampleSizeForFace( surf );
	smax = ( info->lightextents[0] / sample_size ) + 1;
	tmax = ( info->lightextents[1] / sample_size ) + 1;
	size = smax * tmax;

	lm = surf->samples;

	memset( r_blocklights, 0, sizeof( uint ) * size * 3 );

	// add all the lightmaps
	for( map = 0; map < MAXLIGHTMAPS && surf->styles[map] != 255 && lm; map++ )
	{
		scale = tr.lightstylevalue[surf->styles[map]];

		for( i = 0, bl = r_blocklights; i < size; i++, bl += 3, lm++ )
		{
			bl[0] += gEngfuncs.LightToTexGamma( lm->r ) * scale;
			bl[1] += gEngfuncs.LightToTexGamma( lm->g ) * scale;
			bl[2] += gEngfuncs.LightToTexGamma( lm->b ) * scale;
		}
	}

	// add all the dynamic lights
	if( surf->dlightframe == tr.framecount && dynamic )
		R_AddDynamicLights( surf );

	// Put into texture format
#if 0
	stride -= (smax << 2);
	bl = r_blocklights;

	for( t = 0; t < tmax; t++, dest += stride )
	{
		for( s = 0; s < smax; s++ )
		{
			dest[0] = Q_min((bl[0] >> 7), 255 );
			dest[1] = Q_min((bl[1] >> 7), 255 );
			dest[2] = Q_min((bl[2] >> 7), 255 );
			dest[3] = 255;

			bl += 3;
			dest += 4;
		}
	}
#else
	stride -= smax * LIGHTMAP_BPP;
	bl = r_blocklights;

	for( t = 0; t < tmax; t++, dest += stride )
	{
		for( s = 0; s < smax; s++ )
		{
			color[0] = Q_min((bl[0] >> 7), 255 );
			color[1] = Q_min((bl[1] >> 7), 255 );
			color[2] = Q_min((bl[2] >> 7), 255 );
			color[3] = 255;
#if LIGHTMAP_BPP == 1
			dest[0]  = ( color[0] >> 5 ) & 0x07;
			dest[0] |= ( color[1] >> 2 ) & 0x38;
			dest[0] |= ( color[2]      ) & 0xc0;
#elif LIGHTMAP_BPP == 2
			dest[0]  = ( color[0] >> 3 ) & 0x1f;
			dest[0] |= ( color[1] << 3 ) & 0xe0;
			dest[1]  = ( color[1] >> 5 ) & 0x07;
			dest[1] |= ( color[2]      ) & 0xf8;
#elif LIGHTMAP_BPP == 3
			dest[0] = color[0];
			dest[1] = color[1];
			dest[2] = color[2];
#else
			dest[0] = color[0];
			dest[1] = color[1];
			dest[2] = color[2];
			dest[3] = 255;
#endif
			bl += 3;
			dest += LIGHTMAP_BPP;
		}
	}
#endif
}

/*
================
DrawGLPoly
================
*/
void DrawGLPoly( glpoly_t *p, float xScale, float yScale )
{
	float		*v;
	float		sOffset, sy;
	float		tOffset, cy;
	cl_entity_t	*e = RI.currententity;
	int		i;
	qboolean	hasScale = false;
	qboolean	hasOffset = false;

	if( !p ) return;

	if( FBitSet( p->flags, SURF_DRAWTILED ))
		GL_ResetFogColor();

	if( p->flags & SURF_CONVEYOR )
	{
		float		flConveyorSpeed = 0.0f;
		float		flRate, flAngle;
		gl_texture_t	*texture;

		if( ENGINE_GET_PARM( PARM_QUAKE_COMPATIBLE ) && RI.currententity == gEngfuncs.GetEntityByIndex( 0 ) )
		{
			// same as doom speed
			flConveyorSpeed = -35.0f;
		}
		else
		{
			flConveyorSpeed = (e->curstate.rendercolor.g<<8|e->curstate.rendercolor.b) / 16.0f;
			if( e->curstate.rendercolor.r ) flConveyorSpeed = -flConveyorSpeed;
		}

		texture = R_GetTexture( glState.currentTexture );

		flRate = fabs( flConveyorSpeed ) / (float)texture->srcWidth;
		flAngle = ( flConveyorSpeed >= 0 ) ? 180 : 0;

		SinCos( flAngle * ( M_PI_F / 180.0f ), &sy, &cy );
		sOffset = gpGlobals->time * cy * flRate;
		tOffset = gpGlobals->time * sy * flRate;

		// make sure that we are positive
		if( sOffset < 0.0f ) sOffset += 1.0f + -(int)sOffset;
		if( tOffset < 0.0f ) tOffset += 1.0f + -(int)tOffset;

		// make sure that we are in a [0,1] range
		sOffset = sOffset - (int)sOffset;
		tOffset = tOffset - (int)tOffset;

		hasOffset = true;
	}

	if( xScale != 0.0f && yScale != 0.0f )
		hasScale = true;

	if( hasScale ) sceGuTexScale( xScale, yScale );
	if( hasOffset ) sceGuTexOffset( sOffset, tOffset );

	if ( GU_ClipIsRequired( &p->verts[0], p->numverts ) )
	{
		// clip the polygon.
		gu_vert_t*	cv;
		int		cvc;

		GU_Clip( &p->verts[0], p->numverts, &cv, &cvc );
		if( cvc )
		{
#if CLIPPING_DEBUGGING
			sceGuDisable(GU_TEXTURE_2D);
			sceGuColor( 0xff0000ff );
#endif
			sceGuDrawArray( GU_TRIANGLE_FAN, GU_TEXTURE_32BITF | GU_VERTEX_32BITF, cvc, 0, cv );
#if CLIPPING_DEBUGGING
			sceGuEnable(GU_TEXTURE_2D);
			sceGuColor( 0xffffffff );
#endif
		}
	}
	else sceGuDrawArray( GU_TRIANGLE_FAN, GU_TEXTURE_32BITF | GU_VERTEX_32BITF, p->numverts, 0, &p->verts[0] );

	if( hasScale ) sceGuTexScale( 1.0f, 1.0f );
	if( hasOffset ) sceGuTexOffset( 0.0f, 0.0f );

	if( FBitSet( p->flags, SURF_DRAWTILED ))
		GL_SetupFogColorForSurfaces();
}

/*
================
DrawGLPolyChain

Render lightmaps
================
*/
void DrawGLPolyChain( glpoly_t *p, float soffset, float toffset )
{
	qboolean	dynamic = true;

	if( soffset == 0.0f && toffset == 0.0f )
		dynamic = false;

	if( dynamic ) sceGuTexOffset( -soffset, -toffset );

	for( ; p != NULL; p = p->chain )
	{
		float	*v;
		int	i;

		if ( GU_ClipIsRequired( &p->verts[p->numverts], p->numverts ) )
		{
			// clip the polygon.
			gu_vert_t*	cv;
			int		cvc;

			GU_Clip( &p->verts[p->numverts], p->numverts, &cv, &cvc );
			if( cvc ) sceGuDrawArray( GU_TRIANGLE_FAN, GU_TEXTURE_32BITF | GU_VERTEX_32BITF, cvc, 0, cv );
		}
		else sceGuDrawArray( GU_TRIANGLE_FAN, GU_TEXTURE_32BITF | GU_VERTEX_32BITF, p->numverts, 0, &p->verts[p->numverts] );
	}

	if( dynamic ) sceGuTexOffset( 0.0f, 0.0f );
}

_inline qboolean R_HasLightmap( void )
{
	if( CVAR_TO_BOOL( r_fullbright ) || !WORLDMODEL->lightdata )
		return false;

	if( RI.currententity )
	{
		if( RI.currententity->curstate.effects & EF_FULLBRIGHT )
			return false;	// disabled by user

		// check for rendermode
		switch( RI.currententity->curstate.rendermode )
		{
		case kRenderTransTexture:
		case kRenderTransColor:
		case kRenderTransAdd:
		case kRenderGlow:
			return false; // no lightmaps
		}
	}

	return true;
}

/*
================
R_BlendLightmaps
================
*/
void R_BlendLightmaps( void )
{
	msurface_t	*surf, *newsurf = NULL;
	int		i;

	if( !R_HasLightmap() )
		return;

	GL_SetupFogColorForSurfaces ();

	if( !CVAR_TO_BOOL( r_lightmap ))
		sceGuEnable( GU_BLEND );
	else sceGuDisable( GU_BLEND );

	// lightmapped solid surfaces
	sceGuDepthMask( GU_TRUE );
	sceGuDepthFunc( GU_EQUAL );

	sceGuDisable( GU_ALPHA_TEST );
	sceGuBlendFunc( GU_ADD, GU_FIX, GU_SRC_COLOR, GUBLEND0, 0 );
	sceGuTexFunc( GU_TFX_MODULATE, GU_TCC_RGBA );

	// render static lightmaps first
	for( i = 0; i < MAX_LIGHTMAPS; i++ )
	{
		if( gl_lms.lightmap_surfaces[i] )
		{
			GL_Bind( XASH_TEXTURE0, tr.lightmapTextures[i] );

			for( surf = gl_lms.lightmap_surfaces[i]; surf != NULL; surf = surf->info->lightmapchain )
			{
				if( surf->polys ) DrawGLPolyChain( surf->polys, 0.0f, 0.0f );
			}
		}
	}

	// render dynamic lightmaps
	if( CVAR_TO_BOOL( r_dynamic ))
	{
		LM_InitBlock();
#if 0
		GL_Bind( XASH_TEXTURE0, tr.dlightTexture );
#endif
		newsurf = gl_lms.dynamic_surfaces;

		for( surf = gl_lms.dynamic_surfaces; surf != NULL; surf = surf->info->lightmapchain )
		{
			int		smax, tmax;
			int		sample_size;
			mextrasurf_t	*info = surf->info;
			byte		*base;

			sample_size = gEngfuncs.Mod_SampleSizeForFace( surf );
			smax = ( info->lightextents[0] / sample_size ) + 1;
			tmax = ( info->lightextents[1] / sample_size ) + 1;

			if( LM_AllocBlock( smax, tmax, &surf->info->dlight_s, &surf->info->dlight_t ))
			{
				base = gl_lms.lightmap_buffer;
				base += ( surf->info->dlight_t * BLOCK_SIZE + surf->info->dlight_s ) * LIGHTMAP_BPP;

				R_BuildLightMap( surf, base, BLOCK_SIZE * LIGHTMAP_BPP, true );
			}
			else
			{
				msurface_t	*drawsurf;

				// upload what we have so far
				LM_UploadBlock( true );

				// draw all surfaces that use this lightmap
				for( drawsurf = newsurf; drawsurf != surf; drawsurf = drawsurf->info->lightmapchain )
				{
					if( drawsurf->polys )
					{
						DrawGLPolyChain( drawsurf->polys,
						( drawsurf->light_s - drawsurf->info->dlight_s ) * ( 1.0f / (float)BLOCK_SIZE ),
						( drawsurf->light_t - drawsurf->info->dlight_t ) * ( 1.0f / (float)BLOCK_SIZE ));
					}
				}

				newsurf = drawsurf;

				// clear the block
				LM_InitBlock();

				// try uploading the block now
				if( !LM_AllocBlock( smax, tmax, &surf->info->dlight_s, &surf->info->dlight_t ))
					gEngfuncs.Host_Error( "AllocBlock: full\n" );

				base = gl_lms.lightmap_buffer;
				base += ( surf->info->dlight_t * BLOCK_SIZE + surf->info->dlight_s ) * LIGHTMAP_BPP;

				R_BuildLightMap( surf, base, BLOCK_SIZE * LIGHTMAP_BPP, true );
			}
		}

		// draw remainder of dynamic lightmaps that haven't been uploaded yet
		if( newsurf ) LM_UploadBlock( true );

		for( surf = newsurf; surf != NULL; surf = surf->info->lightmapchain )
		{
			if( surf->polys )
			{
				DrawGLPolyChain( surf->polys,
				( surf->light_s - surf->info->dlight_s ) * ( 1.0f / (float)BLOCK_SIZE ),
				( surf->light_t - surf->info->dlight_t ) * ( 1.0f / (float)BLOCK_SIZE ));
			}
		}
	}

	sceGuDisable( GU_BLEND );
	sceGuDepthMask( GU_FALSE );
	sceGuDepthFunc( GU_LEQUAL );
	sceGuTexFunc( GU_TFX_REPLACE, GU_TCC_RGBA );
	sceGuColor( 0xffffffff );

	// restore fog here
	GL_ResetFogColor();
}

/*
================
R_RenderFullbrights
================
*/
void R_RenderFullbrights( void )
{
	mextrasurf_t	*es, *p;
	int		i;

	if( !draw_fullbrights )
		return;

	R_AllowFog( false );

	sceGuEnable( GU_BLEND );
	sceGuDepthMask( GU_TRUE );
	sceGuDisable( GU_ALPHA_TEST );
	sceGuBlendFunc( GU_ADD, GU_FIX, GU_FIX, GUBLEND1, GUBLEND1 );
	sceGuTexFunc( GU_TFX_MODULATE, GU_TCC_RGBA );

	for( i = 1; i < MAX_TEXTURES; i++ )
	{
		es = fullbright_surfaces[i];
		if( !es ) continue;

		GL_Bind( XASH_TEXTURE0, i );

		for( p = es; p; p = p->lumachain )
			DrawGLPoly( p->surf->polys, 0.0f, 0.0f );

		fullbright_surfaces[i] = NULL;
		es->lumachain = NULL;
	}

	sceGuDisable( GU_BLEND );
	sceGuDepthMask( GU_FALSE );
	sceGuDisable( GU_ALPHA_TEST );
	sceGuTexFunc( GU_TFX_REPLACE, GU_TCC_RGBA );

	draw_fullbrights = false;
	R_AllowFog( true );
}

/*
================
R_RenderDetails
================
*/
void R_RenderDetails( void )
{
	gl_texture_t	*glt;
	mextrasurf_t	*es, *p;
	msurface_t	*fa;
	int		i;

	if( !draw_details )
		return;

	GL_SetupFogColorForSurfaces();

	sceGuEnable( GU_BLEND );
	sceGuBlendFunc( GU_ADD, GU_DST_COLOR, GU_SRC_COLOR, 0, 0 );
	sceGuTexFunc( GU_TFX_DECAL, GU_TCC_RGBA );
	sceGuDepthFunc( GU_EQUAL );

	for( i = 1; i < MAX_TEXTURES; i++ )
	{
		es = detail_surfaces[i];
		if( !es ) continue;

		GL_Bind( XASH_TEXTURE0, i );

		for( p = es; p; p = p->detailchain )
		{
			fa = p->surf;
			glt = R_GetTexture( fa->texinfo->texture->gl_texturenum ); // get texture scale

			DrawGLPoly( fa->polys, glt->xscale, glt->yscale );
		}

		detail_surfaces[i] = NULL;
		es->detailchain = NULL;
	}

	sceGuDisable( GU_BLEND );
	sceGuTexFunc( GU_TFX_REPLACE, GU_TCC_RGBA );
	sceGuDepthFunc( GU_LEQUAL );

	draw_details = false;

	// restore fog here
	GL_ResetFogColor();
}

/*
================
R_RenderBrushPoly
================
*/
void R_RenderBrushPoly( msurface_t *fa, int cull_type )
{
	qboolean	is_dynamic = false;
	int	maps;
	texture_t	*t;

	r_stats.c_world_polys++;

	if( fa->flags & SURF_DRAWSKY )
		return; // already handled

	t = R_TextureAnimation( fa );

	GL_Bind( XASH_TEXTURE0, t->gl_texturenum );

	if( FBitSet( fa->flags, SURF_DRAWTURB ))
	{
		// warp texture, no lightmaps
		EmitWaterPolys( fa, (cull_type == CULL_BACKSIDE));
		return;
	}

	if( t->fb_texturenum )
	{
		fa->info->lumachain = fullbright_surfaces[t->fb_texturenum];
		fullbright_surfaces[t->fb_texturenum] = fa->info;
		draw_fullbrights = true;
	}

	if( CVAR_TO_BOOL( r_detailtextures ))
	{
		if( glState.isFogEnabled )
		{
			// don't apply detail textures for windows in the fog
			if( RI.currententity->curstate.rendermode != kRenderTransTexture )
			{
				if( t->dt_texturenum )
				{
					fa->info->detailchain = detail_surfaces[t->dt_texturenum];
					detail_surfaces[t->dt_texturenum] = fa->info;
				}
				else
				{
					// draw stub detail texture for underwater surfaces
					fa->info->detailchain = detail_surfaces[tr.grayTexture];
					detail_surfaces[tr.grayTexture] = fa->info;
				}
				draw_details = true;
			}
		}
		else if( t->dt_texturenum )
		{
			fa->info->detailchain = detail_surfaces[t->dt_texturenum];
			detail_surfaces[t->dt_texturenum] = fa->info;
			draw_details = true;
		}
	}

	DrawGLPoly( fa->polys, 0.0f, 0.0f );

	if( RI.currententity->curstate.rendermode == kRenderNormal )
	{
		// batch decals to draw later
		if( tr.num_draw_decals < MAX_DECAL_SURFS && fa->pdecals )
			tr.draw_decals[tr.num_draw_decals++] = fa;
	}
	else
	{
		// if rendermode != kRenderNormal draw decals sequentially
		DrawSurfaceDecals( fa, true, (cull_type == CULL_BACKSIDE));
	}

	if( FBitSet( fa->flags, SURF_DRAWTILED ))
		return; // no lightmaps anyway

	// check for lightmap modification
	for( maps = 0; maps < MAXLIGHTMAPS && fa->styles[maps] != 255; maps++ )
	{
		if( tr.lightstylevalue[fa->styles[maps]] != fa->cached_light[maps] )
			goto dynamic;
	}

	// dynamic this frame or dynamic previously
	if( fa->dlightframe == tr.framecount )
	{
dynamic:
		// NOTE: at this point we have only valid textures
		if( r_dynamic->value ) is_dynamic = true;
	}

	if( is_dynamic )
	{
		if(( fa->styles[maps] >= 32 || fa->styles[maps] == 0 || fa->styles[maps] == 20 ) && ( fa->dlightframe != tr.framecount ))
		{
			byte		temp[132*132*LIGHTMAP_BPP];
			mextrasurf_t	*info = fa->info;
			int		sample_size;
			int		smax, tmax;

			sample_size = gEngfuncs.Mod_SampleSizeForFace( fa );
			smax = ( info->lightextents[0] / sample_size ) + 1;
			tmax = ( info->lightextents[1] / sample_size ) + 1;

			R_BuildLightMap( fa, temp, smax * LIGHTMAP_BPP, true );
			R_SetCacheState( fa );

#if 1
			GL_UpdateTexture( tr.lightmapTextures[fa->lightmaptexturenum], fa->light_s, fa->light_t, smax, tmax, temp );
#else
			GL_Bind( XASH_TEXTURE0, tr.lightmapTextures[fa->lightmaptexturenum] );

			pglTexSubImage2D( GL_TEXTURE_2D, 0, fa->light_s, fa->light_t, smax, tmax,
			GL_RGBA, GL_UNSIGNED_BYTE, temp );
#endif
			fa->info->lightmapchain = gl_lms.lightmap_surfaces[fa->lightmaptexturenum];
			gl_lms.lightmap_surfaces[fa->lightmaptexturenum] = fa;
		}
		else
		{
			fa->info->lightmapchain = gl_lms.dynamic_surfaces;
			gl_lms.dynamic_surfaces = fa;
		}
	}
	else
	{
		fa->info->lightmapchain = gl_lms.lightmap_surfaces[fa->lightmaptexturenum];
		gl_lms.lightmap_surfaces[fa->lightmaptexturenum] = fa;
	}
}

/*
================
R_DrawTextureChains
================
*/
void R_DrawTextureChains( void )
{
	int		i;
	msurface_t	*s;
	texture_t		*t;

	// make sure what color is reset
	sceGuColor( 0xffffffff );

	R_LoadIdentity();	// set identity matrix

	GL_SetupFogColorForSurfaces();

	// restore worldmodel
	RI.currententity = gEngfuncs.GetEntityByIndex( 0 );
	RI.currentmodel = RI.currententity->model;

	if( ENGINE_GET_PARM( PARM_SKY_SPHERE ) )
	{
		sceGuDisable( GU_TEXTURE_2D );
		sceGuColor( 0xffffffff );
	}

	// clip skybox surfaces
	for( s = skychain; s != NULL; s = s->texturechain )
		R_AddSkyBoxSurface( s );

	if( ENGINE_GET_PARM( PARM_SKY_SPHERE ) )
	{
		sceGuEnable( GU_TEXTURE_2D );
		if( skychain )
			R_DrawClouds();
		skychain = NULL;
	}

	for( i = 0; i < WORLDMODEL->numtextures; i++ )
	{
		t = WORLDMODEL->textures[i];
		if( !t ) continue;

		s = t->texturechain;

		if( !s || ( i == tr.skytexturenum ))
			continue;

		if(( s->flags & SURF_DRAWTURB ) && MOVEVARS->wateralpha < 1.0f )
			continue;	// draw translucent water later

		if( ENGINE_GET_PARM( PARM_QUAKE_COMPATIBLE ) && FBitSet( s->flags, SURF_TRANSPARENT ))
		{
			draw_alpha_surfaces = true;
			continue;	// draw transparent surfaces later
		}

		for( ; s != NULL; s = s->texturechain )
			R_RenderBrushPoly( s, CULL_VISIBLE );
		t->texturechain = NULL;
	}
}

/*
================
R_DrawAlphaTextureChains
================
*/
void R_DrawAlphaTextureChains( void )
{
	int		i;
	msurface_t	*s;
	texture_t		*t;

	if( !draw_alpha_surfaces )
		return;

	memset( gl_lms.lightmap_surfaces, 0, sizeof( gl_lms.lightmap_surfaces ));
	gl_lms.dynamic_surfaces = NULL;

	// make sure what color is reset
	sceGuColor( 0xffffffff );

	R_LoadIdentity(); // set identity matrix

	sceGuDisable( GU_BLEND );
	sceGuEnable( GU_ALPHA_TEST );
	sceGuAlphaFunc( GU_GREATER, 0x40, 0xff );

	GL_SetupFogColorForSurfaces();

	// restore worldmodel
	RI.currententity = gEngfuncs.GetEntityByIndex( 0 );
	RI.currentmodel = RI.currententity->model;
	RI.currententity->curstate.rendermode = kRenderTransAlpha;
	draw_alpha_surfaces = false;

	for( i = 0; i < WORLDMODEL->numtextures; i++ )
	{
		t = WORLDMODEL->textures[i];
		if( !t ) continue;

		s = t->texturechain;

		if( !s || !FBitSet( s->flags, SURF_TRANSPARENT ))
			continue;

		for( ; s != NULL; s = s->texturechain )
			R_RenderBrushPoly( s, CULL_VISIBLE );
		t->texturechain = NULL;
	}

	GL_ResetFogColor();
	R_BlendLightmaps();
	RI.currententity->curstate.rendermode = kRenderNormal; // restore world rendermode

	sceGuAlphaFunc( GU_GREATER, DEFAULT_ALPHATEST, 0xff );
}

/*
================
R_DrawWaterSurfaces
================
*/
void R_DrawWaterSurfaces( void )
{
	int		i;
	msurface_t	*s;
	texture_t		*t;

	if( !RI.drawWorld || RI.onlyClientDraw )
		return;

	// non-transparent water is already drawed
	if( MOVEVARS->wateralpha >= 1.0f )
		return;

	// restore worldmodel
	RI.currententity = gEngfuncs.GetEntityByIndex( 0 );
	RI.currentmodel = RI.currententity->model;

	// go back to the world matrix
	R_LoadIdentity();

	sceGuEnable( GU_BLEND );
	sceGuDepthMask( GU_TRUE );
	sceGuDisable( GU_ALPHA_TEST );
	sceGuBlendFunc( GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0 );
	sceGuTexFunc( GU_TFX_MODULATE, GU_TCC_RGBA );
	sceGuColor( GUCOLOR4F( 1.0f, 1.0f, 1.0f, MOVEVARS->wateralpha ) );

	for( i = 0; i < WORLDMODEL->numtextures; i++ )
	{
		t = WORLDMODEL->textures[i];
		if( !t ) continue;

		s = t->texturechain;
		if( !s ) continue;

		if( !FBitSet( s->flags, SURF_DRAWTURB ))
			continue;

		// set modulate mode explicitly
		GL_Bind( XASH_TEXTURE0, t->gl_texturenum );

		for( ; s; s = s->texturechain )
			EmitWaterPolys( s, false );

		t->texturechain = NULL;
	}

	sceGuDisable( GU_BLEND );
	sceGuDepthMask( GU_FALSE );
	sceGuDisable( GU_ALPHA_TEST );
	sceGuTexFunc( GU_TFX_REPLACE, GU_TCC_RGBA );
	sceGuColor( 0xffffffff );
}

/*
=================
R_SurfaceCompare

compare translucent surfaces
=================
*/
static int R_SurfaceCompare( const void *a, const void *b )
{
	msurface_t	*surf1, *surf2;
	vec3_t		org1, org2;
	float		len1, len2;

	surf1 = (msurface_t *)((sortedface_t *)a)->surf;
	surf2 = (msurface_t *)((sortedface_t *)b)->surf;

	VectorAdd( RI.currententity->origin, surf1->info->origin, org1 );
	VectorAdd( RI.currententity->origin, surf2->info->origin, org2 );

	// compare by plane dists
	len1 = DotProduct( org1, RI.vforward ) - RI.viewplanedist;
	len2 = DotProduct( org2, RI.vforward ) - RI.viewplanedist;

	if( len1 > len2 )
		return -1;
	if( len1 < len2 )
		return 1;

	return 0;
}

void R_SetRenderMode( cl_entity_t *e )
{
	switch( e->curstate.rendermode )
	{
	case kRenderNormal:
		sceGuColor( 0xffffffff );
		break;
	case kRenderTransColor:
		sceGuBlendFunc( GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0 );
		sceGuColor( GUCOLOR4UB( e->curstate.rendercolor.r, e->curstate.rendercolor.g, e->curstate.rendercolor.b, e->curstate.renderamt ) );
		sceGuTexFunc( GU_TFX_MODULATE, GU_TCC_RGBA );
		sceGuDisable( GU_TEXTURE_2D );
		sceGuEnable( GU_BLEND );
		break;
	case kRenderTransAdd:
		sceGuTexFunc( GU_TFX_MODULATE, GU_TCC_RGBA );
		sceGuColor( GUCOLOR4F( tr.blend, tr.blend, tr.blend, 1.0f ) );
		sceGuBlendFunc( GU_ADD, GU_FIX, GU_FIX, GUBLEND1, GUBLEND1 );
		sceGuDepthMask( GU_TRUE );
		sceGuEnable( GU_BLEND );
		break;
	case kRenderTransAlpha:
		sceGuEnable( GU_ALPHA_TEST );
		sceGuTexFunc( GU_TFX_MODULATE, GU_TCC_RGBA );
		if( ENGINE_GET_PARM( PARM_QUAKE_COMPATIBLE ))
		{
			sceGuBlendFunc( GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0 );
			sceGuColor( GUCOLOR4F( 1.0f, 1.0f, 1.0f, tr.blend ) );
			sceGuEnable( GU_BLEND );
		}
		else
		{
			sceGuColor( 0xffffffff );
			sceGuDisable( GU_BLEND );
		}
		sceGuAlphaFunc( GU_GREATER, 0x40, 0xff );
		break;
	default:
		sceGuTexFunc( GU_TFX_MODULATE, GU_TCC_RGBA );
		sceGuBlendFunc( GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0 );
		sceGuColor( GUCOLOR4F( 1.0f, 1.0f, 1.0f, tr.blend ) );
		sceGuDepthMask( GU_TRUE );
		sceGuEnable( GU_BLEND );
		break;
	}
}

/*
=================
R_DrawBrushModel
=================
*/
void R_DrawBrushModel( cl_entity_t *e )
{
	int		i, k, num_sorted;
	vec3_t		origin_l, oldorigin;
	int		old_rendermode;
	vec3_t		mins, maxs;
	int		cull_type;
	msurface_t	*psurf;
	model_t		*clmodel;
	qboolean		rotated;
	dlight_t		*l;

	if( !RI.drawWorld ) return;

	clmodel = e->model;

	if( !VectorIsNull( e->angles ))
	{
		for( i = 0; i < 3; i++ )
		{
			mins[i] = e->origin[i] - clmodel->radius;
			maxs[i] = e->origin[i] + clmodel->radius;
		}
		rotated = true;
	}
	else
	{
		VectorAdd( e->origin, clmodel->mins, mins );
		VectorAdd( e->origin, clmodel->maxs, maxs );
		rotated = false;
	}

	if( R_CullBox( mins, maxs ))
		return;

	memset( gl_lms.lightmap_surfaces, 0, sizeof( gl_lms.lightmap_surfaces ));
	old_rendermode = e->curstate.rendermode;
	gl_lms.dynamic_surfaces = NULL;

	if( rotated ) R_RotateForEntity( e );
	else R_TranslateForEntity( e );

	if( ENGINE_GET_PARM( PARM_QUAKE_COMPATIBLE ) && FBitSet( clmodel->flags, MODEL_TRANSPARENT ))
		e->curstate.rendermode = kRenderTransAlpha;

	e->visframe = tr.realframecount; // visible

	if( rotated ) Matrix4x4_VectorITransform( RI.objectMatrix, RI.cullorigin, tr.modelorg );
	else VectorSubtract( RI.cullorigin, e->origin, tr.modelorg );

	// calculate dynamic lighting for bmodel
	for( k = 0, l = gEngfuncs.GetDynamicLight( 0 ); k < MAX_DLIGHTS; k++, l++ )
	{
		if( l->die < gpGlobals->time || !l->radius )
			continue;

		VectorCopy( l->origin, oldorigin ); // save lightorigin
		Matrix4x4_VectorITransform( RI.objectMatrix, l->origin, origin_l );
		VectorCopy( origin_l, l->origin ); // move light in bmodel space
		R_MarkLights( l, 1<<k, clmodel->nodes + clmodel->hulls[0].firstclipnode );
		VectorCopy( oldorigin, l->origin ); // restore lightorigin
	}

	// setup the rendermode
	R_SetRenderMode( e );
	GL_SetupFogColorForSurfaces ();

	if( e->curstate.rendermode == kRenderTransAdd )
	{
		R_AllowFog( false );
	}

	psurf = &clmodel->surfaces[clmodel->firstmodelsurface];

	GU_ClipSetModelFrustum( RI.objectMatrix );

	// sorting is not required, +Z_Realloc in Mod_LoadSubmodels (mod_bmodel.c)
	for( i = 0; i < clmodel->nummodelsurfaces; i++, psurf++ )
	{
		if( FBitSet( psurf->flags, SURF_DRAWTURB ) && !ENGINE_GET_PARM( PARM_QUAKE_COMPATIBLE ))
		{
			if( psurf->plane->type != PLANE_Z && !FBitSet( e->curstate.effects, EF_WATERSIDES ))
				continue;
			if( mins[2] + 1.0f >= psurf->plane->dist )
				continue;
		}

		cull_type = R_CullSurface( psurf, &RI.frustum, RI.frustum.clipFlags );

		if( cull_type >= CULL_FRUSTUM )
			continue;

		if( cull_type == CULL_BACKSIDE )
		{
			if( !FBitSet( psurf->flags, SURF_DRAWTURB ) && !( psurf->pdecals && e->curstate.rendermode == kRenderTransTexture ))
				continue;
		}
		R_RenderBrushPoly( psurf, cull_type );
	}

	if( e->curstate.rendermode == kRenderTransColor )
		sceGuEnable( GU_TEXTURE_2D );

	DrawDecalsBatch();
	GL_ResetFogColor();
	R_BlendLightmaps();
	R_RenderFullbrights();
	R_RenderDetails();

	// restore fog here
	if( e->curstate.rendermode == kRenderTransAdd )
		R_AllowFog( true );

	e->curstate.rendermode = old_rendermode;

	sceGuDisable( GU_ALPHA_TEST );
	sceGuAlphaFunc( GU_GREATER, DEFAULT_ALPHATEST, 0xff );
	sceGuDisable( GU_BLEND );
	sceGuDepthMask( GU_FALSE );

	R_DrawModelHull();	// draw before restore
	GU_ClipRestoreWorldFrustum();
	R_LoadIdentity();	// restore worldmatrix
}

/*
=============================================================

	WORLD MODEL

=============================================================
*/
/*
================
R_RecursiveWorldNode
================
*/
void R_RecursiveWorldNode( mnode_t *node, uint clipflags )
{
	int		i, clipped;
	msurface_t	*surf, **mark;
	mleaf_t		*pleaf;
	int		c, side;
	float		dot;
loc0:
	if( node->contents == CONTENTS_SOLID )
		return; // hit a solid leaf

	if( node->visframe != tr.visframecount )
		return;

	if( clipflags && !CVAR_TO_BOOL( r_nocull ))
	{
		for( i = 0; i < 6; i++ )
		{
			const mplane_t	*p = &RI.frustum.planes[i];

			if( !FBitSet( clipflags, BIT( i )))
				continue;

			clipped = BoxOnPlaneSide( node->minmaxs, node->minmaxs + 3, p );
			if( clipped == 2 ) return;
			if( clipped == 1 ) ClearBits( clipflags, BIT( i ));
		}
	}

	// if a leaf node, draw stuff
	if( node->contents < 0 )
	{
		pleaf = (mleaf_t *)node;

		mark = pleaf->firstmarksurface;
		c = pleaf->nummarksurfaces;

		if( c )
		{
			do
			{
				(*mark)->visframe = tr.framecount;
				mark++;
			} while( --c );
		}

		// deal with model fragments in this leaf
		if( pleaf->efrags )
			gEngfuncs.R_StoreEfrags( &pleaf->efrags, tr.realframecount );

		r_stats.c_world_leafs++;
		return;
	}

	// node is just a decision point, so go down the apropriate sides

	// find which side of the node we are on
	dot = PlaneDiff( tr.modelorg, node->plane );
	side = (dot >= 0.0f) ? 0 : 1;

	// recurse down the children, front side first
	R_RecursiveWorldNode( node->children[side], clipflags );

	// draw stuff
	for( c = node->numsurfaces, surf = WORLDMODEL->surfaces + node->firstsurface; c; c--, surf++ )
	{
		if( R_CullSurface( surf, &RI.frustum, clipflags ))
			continue;

		if( surf->flags & SURF_DRAWSKY )
		{
			// make sky chain to right clip the skybox
			surf->texturechain = skychain;
			skychain = surf;
		}
		else
		{
			surf->texturechain = surf->texinfo->texture->texturechain;
			surf->texinfo->texture->texturechain = surf;
		}
	}

	// recurse down the back side
	node = node->children[!side];
	goto loc0;
}

/*
================
R_CullNodeTopView

cull node by user rectangle (simple scissor)
================
*/
qboolean R_CullNodeTopView( mnode_t *node )
{
	vec2_t	delta, size;
	vec3_t	center, half;

	// build the node center and half-diagonal
	VectorAverage( node->minmaxs, node->minmaxs + 3, center );
	VectorSubtract( node->minmaxs + 3, center, half );

	// cull against the screen frustum or the appropriate area's frustum.
	Vector2Subtract( center, world_orthocenter, delta );
	Vector2Add( half, world_orthohalf, size );

	return ( fabs( delta[0] ) > size[0] ) || ( fabs( delta[1] ) > size[1] );
}

/*
================
R_DrawTopViewLeaf
================
*/
static void R_DrawTopViewLeaf( mleaf_t *pleaf, uint clipflags )
{
	msurface_t	**mark, *surf;
	int		i;

	for( i = 0, mark = pleaf->firstmarksurface; i < pleaf->nummarksurfaces; i++, mark++ )
	{
		surf = *mark;

		// don't process the same surface twice
		if( surf->visframe == tr.framecount )
			continue;

		surf->visframe = tr.framecount;

		if( R_CullSurface( surf, &RI.frustum, clipflags ))
			continue;

		if(!( surf->flags & SURF_DRAWSKY ))
		{
			surf->texturechain = surf->texinfo->texture->texturechain;
			surf->texinfo->texture->texturechain = surf;
		}
	}

	// deal with model fragments in this leaf
	if( pleaf->efrags )
		gEngfuncs.R_StoreEfrags( &pleaf->efrags, tr.realframecount );

	r_stats.c_world_leafs++;
}

/*
================
R_DrawWorldTopView
================
*/
void R_DrawWorldTopView( mnode_t *node, uint clipflags )
{
	int		i, c, clipped;
	msurface_t	*surf;

	do
	{
		if( node->contents == CONTENTS_SOLID )
			return;	// hit a solid leaf

		if( node->visframe != tr.visframecount )
			return;

		if( clipflags && !r_nocull->value )
		{
			for( i = 0; i < 6; i++ )
			{
				const mplane_t	*p = &RI.frustum.planes[i];

				if( !FBitSet( clipflags, BIT( i )))
					continue;

				clipped = BoxOnPlaneSide( node->minmaxs, node->minmaxs + 3, p );
				if( clipped == 2 ) return;
				if( clipped == 1 ) ClearBits( clipflags, BIT( i ));
			}
		}

		// cull against the screen frustum or the appropriate area's frustum.
		if( R_CullNodeTopView( node ))
			return;

		// if a leaf node, draw stuff
		if( node->contents < 0 )
		{
			R_DrawTopViewLeaf( (mleaf_t *)node, clipflags );
			return;
		}

		// draw stuff
		for( c = node->numsurfaces, surf = WORLDMODEL->surfaces + node->firstsurface; c; c--, surf++ )
		{
			// don't process the same surface twice
			if( surf->visframe == tr.framecount )
				continue;

			surf->visframe = tr.framecount;

			if( R_CullSurface( surf, &RI.frustum, clipflags ))
				continue;

			if(!( surf->flags & SURF_DRAWSKY ))
			{
				surf->texturechain = surf->texinfo->texture->texturechain;
				surf->texinfo->texture->texturechain = surf;
			}
		}

		// recurse down both children, we don't care the order...
		R_DrawWorldTopView( node->children[0], clipflags );
		node = node->children[1];

	} while( node );
}

/*
=============
R_DrawTriangleOutlines
=============
*/
void R_DrawTriangleOutlines( void )
{
#if 0
	int		i, j;
	msurface_t	*surf;
	glpoly_t		*p;
	float		*v;

	if( !gl_wireframe->value )
		return;

	pglDisable( GL_TEXTURE_2D );
	pglDisable( GL_DEPTH_TEST );
	pglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	pglPolygonMode( GL_FRONT_AND_BACK, GL_LINE );

	// render static surfaces first
	for( i = 0; i < MAX_LIGHTMAPS; i++ )
	{
		for( surf = gl_lms.lightmap_surfaces[i]; surf != NULL; surf = surf->info->lightmapchain )
		{
			p = surf->polys;
			for( ; p != NULL; p = p->chain )
			{
				pglBegin( GL_POLYGON );
				v = p->verts[0];
				for( j = 0; j < p->numverts; j++, v += VERTEXSIZE )
					pglVertex3fv( v );
				pglEnd ();
			}
		}
	}

	// render surfaces with dynamic lightmaps
	for( surf = gl_lms.dynamic_surfaces; surf != NULL; surf = surf->info->lightmapchain )
	{
		p = surf->polys;

		for( ; p != NULL; p = p->chain )
		{
			pglBegin( GL_POLYGON );
			v = p->verts[0];
			for( j = 0; j < p->numverts; j++, v += VERTEXSIZE )
				pglVertex3fv( v );
			pglEnd ();
		}
	}

	pglPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
	pglEnable( GL_DEPTH_TEST );
	pglEnable( GL_TEXTURE_2D );
#endif
}

/*
=============
R_DrawWorld
=============
*/
void R_DrawWorld( void )
{
	double	start, end;

	// paranoia issues: when gl_renderer is "0" we need have something valid for currententity
	// to prevent crashing until HeadShield drawing.
	RI.currententity = gEngfuncs.GetEntityByIndex( 0 );
	RI.currentmodel = RI.currententity->model;

	if( !RI.drawWorld || RI.onlyClientDraw )
		return;

	VectorCopy( RI.cullorigin, tr.modelorg );
	memset( gl_lms.lightmap_surfaces, 0, sizeof( gl_lms.lightmap_surfaces ));
	memset( fullbright_surfaces, 0, sizeof( fullbright_surfaces ));
	memset( detail_surfaces, 0, sizeof( detail_surfaces ));

	gl_lms.dynamic_surfaces = NULL;

	sceGuDisable( GU_ALPHA_TEST );
	sceGuDisable( GU_BLEND );

	tr.blend = 1.0f;

	R_ClearSkyBox ();

	start = gEngfuncs.pfnTime();
	if( RI.drawOrtho )
		R_DrawWorldTopView( WORLDMODEL->nodes, RI.frustum.clipFlags );
	else R_RecursiveWorldNode( WORLDMODEL->nodes, RI.frustum.clipFlags );
	end = gEngfuncs.pfnTime();

	r_stats.t_world_node = end - start;

	start = gEngfuncs.pfnTime();

	R_DrawTextureChains();

	if( !ENGINE_GET_PARM( PARM_DEV_OVERVIEW ))
	{
		DrawDecalsBatch();
		GL_ResetFogColor();
		R_BlendLightmaps();
		R_RenderFullbrights();
		R_RenderDetails();

		if( skychain )
			R_DrawSkyBox();
	}

	end = gEngfuncs.pfnTime();

	r_stats.t_world_draw = end - start;
	tr.num_draw_decals = 0;
	skychain = NULL;

	R_DrawTriangleOutlines ();

	R_DrawWorldHull();
}

/*
===============
R_MarkLeaves

Mark the leaves and nodes that are in the PVS for the current leaf
===============
*/
void R_MarkLeaves( void )
{
	qboolean	novis = false;
	qboolean	force = false;
	mleaf_t	*leaf = NULL;
	mnode_t	*node;
	vec3_t	test;
	int	i;

	if( !RI.drawWorld ) return;

	if( FBitSet( r_novis->flags, FCVAR_CHANGED ) || tr.fResetVis )
	{
		// force recalc viewleaf
		ClearBits( r_novis->flags, FCVAR_CHANGED );
		tr.fResetVis = false;
		RI.viewleaf = NULL;
	}

	VectorCopy( RI.pvsorigin, test );

	if( RI.viewleaf != NULL )
	{
		// merge two leafs that can be a crossed-line contents
		if( RI.viewleaf->contents == CONTENTS_EMPTY )
		{
			VectorSet( test, RI.pvsorigin[0], RI.pvsorigin[1], RI.pvsorigin[2] - 16.0f );
			leaf = gEngfuncs.Mod_PointInLeaf( test, WORLDMODEL->nodes );
		}
		else
		{
			VectorSet( test, RI.pvsorigin[0], RI.pvsorigin[1], RI.pvsorigin[2] + 16.0f );
			leaf = gEngfuncs.Mod_PointInLeaf( test, WORLDMODEL->nodes );
		}

		if(( leaf->contents != CONTENTS_SOLID ) && ( RI.viewleaf != leaf ))
			force = true;
	}

	if( RI.viewleaf == RI.oldviewleaf && RI.viewleaf != NULL && !force )
		return;

	// development aid to let you run around
	// and see exactly where the pvs ends
	if( r_lockpvs->value ) return;

	RI.oldviewleaf = RI.viewleaf;
	tr.visframecount++;

	if( r_novis->value || RI.drawOrtho || !RI.viewleaf || !WORLDMODEL->visdata )
		novis = true;

	gEngfuncs.R_FatPVS( RI.pvsorigin, REFPVS_RADIUS, RI.visbytes, FBitSet( RI.params, RP_OLDVIEWLEAF ), novis );
	if( force && !novis ) gEngfuncs.R_FatPVS( test, REFPVS_RADIUS, RI.visbytes, true, novis );

	for( i = 0; i < WORLDMODEL->numleafs; i++ )
	{
		if( CHECKVISBIT( RI.visbytes, i ))
		{
			node = (mnode_t *)&WORLDMODEL->leafs[i+1];
			do
			{
				if( node->visframe == tr.visframecount )
					break;
				node->visframe = tr.visframecount;
				node = node->parent;
			} while( node );
		}
	}
}

/*
========================
GL_CreateSurfaceLightmap
========================
*/
void GL_CreateSurfaceLightmap( msurface_t *surf, model_t *loadmodel )
{
	int		smax, tmax;
	int		sample_size;
	mextrasurf_t	*info = surf->info;
	byte		*base;

	if( !loadmodel->lightdata )
		return;

	if( FBitSet( surf->flags, SURF_DRAWTILED ) )
		return;

	sample_size = gEngfuncs.Mod_SampleSizeForFace( surf );
	smax = ( info->lightextents[0] / sample_size ) + 1;
	tmax = ( info->lightextents[1] / sample_size ) + 1;

	if( !LM_AllocBlock( smax, tmax, &surf->light_s, &surf->light_t ) )
	{
		LM_UploadBlock( false );
		LM_InitBlock();

		if( !LM_AllocBlock( smax, tmax, &surf->light_s, &surf->light_t ) )
			gEngfuncs.Host_Error( "AllocBlock: full\n" );
	}

	surf->lightmaptexturenum = gl_lms.current_lightmap_texture;

	base = gl_lms.lightmap_buffer;
	base += ( surf->light_t * BLOCK_SIZE + surf->light_s ) * LIGHTMAP_BPP;

	R_SetCacheState( surf );
	R_BuildLightMap( surf, base, BLOCK_SIZE * LIGHTMAP_BPP, false );
}

/*
==================
GL_RebuildLightmaps

Rebuilds the lightmap texture
when gamma is changed
==================
*/
void GL_RebuildLightmaps( void )
{
	int	i, j;
	model_t	*m;

	if( !ENGINE_GET_PARM( PARM_CLIENT_ACTIVE ) )
		return; // wait for worldmodel

	ClearBits( vid_brightness->flags, FCVAR_CHANGED );
	ClearBits( vid_gamma->flags, FCVAR_CHANGED );

	// release old lightmaps
	for( i = 0; i < MAX_LIGHTMAPS; i++ )
	{
		if( !tr.lightmapTextures[i] ) break;
		GL_FreeTexture( tr.lightmapTextures[i] );
	}

	memset( tr.lightmapTextures, 0, sizeof( tr.lightmapTextures ) );
	gl_lms.current_lightmap_texture = 0;

	// setup all the lightstyles
	CL_RunLightStyles();

	LM_InitBlock();

	for( i = 0; i < ENGINE_GET_PARM( PARM_NUMMODELS ); i++ )
	{
		if( ( m = gEngfuncs.pfnGetModelByIndex( i + 1 ) ) == NULL )
			continue;

		if( m->name[0] == '*' || m->type != mod_brush )
			continue;

		for( j = 0; j < m->numsurfaces; j++ )
			GL_CreateSurfaceLightmap( m->surfaces + j, m );
	}
	LM_UploadBlock( false );

	if( gEngfuncs.drawFuncs->GL_BuildLightmaps )
	{
		// build lightmaps on the client-side
		gEngfuncs.drawFuncs->GL_BuildLightmaps( );
	}
}

/*
==================
GL_BuildLightmaps

Builds the lightmap texture
with all the surfaces from all brush models
==================
*/
void GL_BuildLightmaps( void )
{
	int	i, j;
	model_t	*m;

	// release old lightmaps
	for( i = 0; i < MAX_LIGHTMAPS; i++ )
	{
		if( !tr.lightmapTextures[i] ) break;
		GL_FreeTexture( tr.lightmapTextures[i] );
	}

	memset( tr.lightmapTextures, 0, sizeof( tr.lightmapTextures ));
	memset( &RI, 0, sizeof( RI ));
#if 1
	tr.block_size = BLOCK_SIZE_DEFAULT;
#else
	// update the lightmap blocksize
	if( FBitSet( ENGINE_GET_PARM( PARM_FEATURES ), ENGINE_LARGE_LIGHTMAPS ))
		tr.block_size = BLOCK_SIZE_MAX;
	else tr.block_size = BLOCK_SIZE_DEFAULT;
#endif
	skychain = NULL;

	tr.framecount = tr.visframecount = 1;	// no dlight cache
	gl_lms.current_lightmap_texture = 0;
	tr.modelviewIdentity = false;
	tr.realframecount = 1;
	nColinElim = 0;

	// setup the texture for dlights
	R_InitDlightTexture();

	// setup all the lightstyles
	CL_RunLightStyles();

	LM_InitBlock();

	for( i = 0; i < ENGINE_GET_PARM( PARM_NUMMODELS ); i++ )
	{
		if( ( m = gEngfuncs.pfnGetModelByIndex( i + 1 ) ) == NULL )
			continue;

		if( m->name[0] == '*' || m->type != mod_brush )
			continue;

		for( j = 0; j < m->numsurfaces; j++ )
		{
			// clearing all decal chains
			m->surfaces[j].pdecals = NULL;
			m->surfaces[j].visframe = 0;

			GL_CreateSurfaceLightmap( m->surfaces + j, m );

			if( m->surfaces[j].flags & SURF_DRAWTURB )
				continue;

			GL_BuildPolygonFromSurface( m, m->surfaces + j );
		}

		// clearing visframe
		for( j = 0; j < m->numleafs; j++ )
			m->leafs[j+1].visframe = 0;
		for( j = 0; j < m->numnodes; j++ )
			m->nodes[j].visframe = 0;
	}

	LM_UploadBlock( false );

	if( gEngfuncs.drawFuncs->GL_BuildLightmaps )
	{
		// build lightmaps on the client-side
		gEngfuncs.drawFuncs->GL_BuildLightmaps( );
	}

	// now gamma and brightness are valid
	ClearBits( vid_brightness->flags, FCVAR_CHANGED );
	ClearBits( vid_gamma->flags, FCVAR_CHANGED );
}

void GL_InitRandomTable( void )
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
