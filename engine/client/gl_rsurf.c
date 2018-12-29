/*
gl_rsurf.c - surface-related refresh code
Copyright (C) 2010 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "common.h"
#include "client.h"
#include "gl_local.h"
#include "mod_local.h"
#include "mathlib.h"
			
typedef struct
{
	int		allocated[BLOCK_SIZE_MAX];
	int		current_lightmap_texture;
	msurface_t	*dynamic_surfaces;
	msurface_t	*lightmap_surfaces[MAX_LIGHTMAPS];
	byte		lightmap_buffer[BLOCK_SIZE_MAX*BLOCK_SIZE_MAX*4];
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

static void LM_UploadBlock( int lightmapnum );

byte *Mod_GetCurrentVis( void )
{
	if( clgame.drawFuncs.Mod_GetCurrentVis && tr.fCustomRendering )
		return clgame.drawFuncs.Mod_GetCurrentVis();
	return RI.visbytes;
}

void Mod_SetOrthoBounds( float *mins, float *maxs )
{
	if( clgame.drawFuncs.GL_OrthoBounds )
	{
		clgame.drawFuncs.GL_OrthoBounds( mins, maxs );
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

	if( numverts > ( SUBDIVIDE_SIZE - 4 ))
		Host_Error( "Mod_SubdividePolygon: too many vertexes on face ( %i )\n", numverts );

	sample_size = Mod_SampleSizeForFace( warpface );
	BoundPoly( numverts, verts, mins, maxs );

	for( i = 0; i < 3; i++ )
	{
		m = ( mins[i] + maxs[i] ) * 0.5f;
		m = SUBDIVIDE_SIZE * floor( m / SUBDIVIDE_SIZE + 0.5f );
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
	poly = Mem_Calloc( loadmodel->mempool, sizeof( glpoly_t ) + (numverts - 4) * VERTEXSIZE * sizeof( float ));
	poly->next = warpface->polys;
	poly->flags = warpface->flags;
	warpface->polys = poly;
	poly->numverts = numverts;

	for( i = 0; i < numverts; i++, verts += 3 )
	{
		VectorCopy( verts, poly->verts[i] );

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

		poly->verts[i][3] = s;
		poly->verts[i][4] = t;

		// for speed reasons
		if( !FBitSet( warpface->flags, SURF_DRAWTURB ))
		{
			// lightmap texture coordinates
			s = DotProduct( verts, warpinfo->lmvecs[0] ) + warpinfo->lmvecs[0][3];
			s -= warpinfo->lightmapmins[0];
			s += warpface->light_s * sample_size;
			s += sample_size * 0.5;
			s /= BLOCK_SIZE * sample_size; //fa->texinfo->texture->width;

			t = DotProduct( verts, warpinfo->lmvecs[1] ) + warpinfo->lmvecs[1][3];
			t -= warpinfo->lightmapmins[1];
			t += warpface->light_t * sample_size;
			t += sample_size * 0.5;
			t /= BLOCK_SIZE * sample_size; //fa->texinfo->texture->height;

			poly->verts[i][5] = s;
			poly->verts[i][6] = t;
		}
	}
}

void GL_SetupFogColorForSurfaces( void )
{
	vec3_t	fogColor;
	float	factor, div;

	if( !pglIsEnabled( GL_FOG ))
		return;

	if( RI.currententity && RI.currententity->curstate.rendermode == kRenderTransTexture )
          {
		pglFogfv( GL_FOG_COLOR, RI.fogColor );
		return;
	}

	div = (r_detailtextures->value) ? 2.0f : 1.0f;
	factor = (r_detailtextures->value) ? 3.0f : 2.0f;
	fogColor[0] = pow( RI.fogColor[0] / div, ( 1.0f / factor ));
	fogColor[1] = pow( RI.fogColor[1] / div, ( 1.0f / factor ));
	fogColor[2] = pow( RI.fogColor[2] / div, ( 1.0f / factor ));
	pglFogfv( GL_FOG_COLOR, fogColor );
}

void GL_ResetFogColor( void )
{
	// restore fog here
	if( pglIsEnabled( GL_FOG ))
		pglFogfv( GL_FOG_COLOR, RI.fogColor );
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
	int		i, lindex, lnumverts;
	medge_t		*pedges, *r_pedge;
	mextrasurf_t	*info = fa->info;
	float		sample_size;
	texture_t		*tex;
	gl_texture_t	*glt;
	float		*vec;
	float		s, t;
	glpoly_t		*poly;

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

	sample_size = Mod_SampleSizeForFace( fa );

	// reconstruct the polygon
	pedges = mod->edges;
	lnumverts = fa->numedges;

	// detach if already created, reconstruct again
	poly = fa->polys;
	fa->polys = NULL;

	// quake simple models (healthkits etc) need to be reconstructed their polys because LM coords has changed after the map change
	poly = Mem_Realloc( mod->mempool, poly, sizeof( glpoly_t ) + ( lnumverts - 4 ) * VERTEXSIZE * sizeof( float ));
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

		VectorCopy( vec, poly->verts[i] );
		poly->verts[i][3] = s;
		poly->verts[i][4] = t;

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

		poly->verts[i][5] = s;
		poly->verts[i][6] = t;
	}

	// remove co-linear points - Ed
	if( !CVAR_TO_BOOL( gl_keeptjunctions ) && !FBitSet( fa->flags, SURF_UNDERWATER ))
	{
		for( i = 0; i < lnumverts; i++ )
		{
			vec3_t	v1, v2;
			float	*prev, *this, *next;

			prev = poly->verts[(i + lnumverts - 1) % lnumverts];
			next = poly->verts[(i + 1) % lnumverts];
			this = poly->verts[i];

			VectorSubtract( this, prev, v1 );
			VectorNormalize( v1 );
			VectorSubtract( next, prev, v2 );
			VectorNormalize( v2 );

			// skip co-linear points
			if(( fabs( v1[0] - v2[0] ) <= 0.001f) && (fabs( v1[1] - v2[1] ) <= 0.001f) && (fabs( v1[2] - v2[2] ) <= 0.001f))
			{
				int	j, k;

				for( j = i + 1; j < lnumverts; j++ )
				{
					for( k = 0; k < VERTEXSIZE; k++ )
						poly->verts[j-1][k] = poly->verts[j][k];
				}

				// retry next vertex next time, which is now current vertex
				lnumverts--;
				nColinElim++;
				i--;
			}
		}
	}

	poly->numverts = lnumverts;
}

/*
===============
R_TextureAnimation

Returns the proper texture for a given time and base texture
===============
*/
texture_t *R_TextureAnimation( msurface_t *s )
{
	texture_t	*base = s->texinfo->texture;
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

		reletive = (int)(cl.time * speed) % base->anim_total;
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

	sample_size = Mod_SampleSizeForFace( surf );
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

	for( lnum = 0; lnum < MAX_DLIGHTS; lnum++ )
	{
		if( !FBitSet( surf->dlightbits, BIT( lnum )))
			continue;	// not lit by this light

		dl = &cl_dlights[lnum];

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
					bl[0] += ((int)((rad - dist) * 256) * LightToTexGamma( dl->color.r )) / 256;
					bl[1] += ((int)((rad - dist) * 256) * LightToTexGamma( dl->color.g )) / 256;
					bl[2] += ((int)((rad - dist) * 256) * LightToTexGamma( dl->color.b )) / 256;
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

		GL_Bind( GL_TEXTURE0, tr.dlightTexture );
		pglTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, BLOCK_SIZE, height, GL_RGBA, GL_UNSIGNED_BYTE, gl_lms.lightmap_buffer );
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
		r_lightmap.type = PF_RGBA_32;
		r_lightmap.size = r_lightmap.width * r_lightmap.height * 4;
		r_lightmap.flags = IMAGE_HAS_COLOR;
		r_lightmap.buffer = gl_lms.lightmap_buffer;
		tr.lightmapTextures[i] = GL_LoadTextureInternal( lmName, &r_lightmap, TF_FONT|TF_ATLAS_PAGE );

		if( ++gl_lms.current_lightmap_texture == MAX_LIGHTMAPS )
			Host_Error( "AllocBlock: full\n" );
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

	sample_size = Mod_SampleSizeForFace( surf );
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
			bl[0] += LightToTexGamma( lm->r ) * scale;
			bl[1] += LightToTexGamma( lm->g ) * scale;
			bl[2] += LightToTexGamma( lm->b ) * scale;
		}
	}

	// add all the dynamic lights
	if( surf->dlightframe == tr.framecount && dynamic )
		R_AddDynamicLights( surf );

	// Put into texture format
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
	int		i, hasScale = false;

	if( !p ) return;

	if( FBitSet( p->flags, SURF_DRAWTILED ))
		GL_ResetFogColor();

	if( p->flags & SURF_CONVEYOR )
	{
		float		flConveyorSpeed = 0.0f;
		float		flRate, flAngle;
		gl_texture_t	*texture;

		if( CL_IsQuakeCompatible() && RI.currententity == clgame.entities )
		{
			// same as doom speed
			flConveyorSpeed = -35.0f;
		}
		else
		{
			flConveyorSpeed = (e->curstate.rendercolor.g<<8|e->curstate.rendercolor.b) / 16.0f;
			if( e->curstate.rendercolor.r ) flConveyorSpeed = -flConveyorSpeed;
		}
		texture = R_GetTexture( glState.currentTextures[glState.activeTMU] );

		flRate = abs( flConveyorSpeed ) / (float)texture->srcWidth;
		flAngle = ( flConveyorSpeed >= 0 ) ? 180 : 0;

		SinCos( flAngle * ( M_PI / 180.0f ), &sy, &cy );
		sOffset = cl.time * cy * flRate;
		tOffset = cl.time * sy * flRate;
	
		// make sure that we are positive
		if( sOffset < 0.0f ) sOffset += 1.0f + -(int)sOffset;
		if( tOffset < 0.0f ) tOffset += 1.0f + -(int)tOffset;

		// make sure that we are in a [0,1] range
		sOffset = sOffset - (int)sOffset;
		tOffset = tOffset - (int)tOffset;
	}
	else
	{
		sOffset = tOffset = 0.0f;
	}

	if( xScale != 0.0f && yScale != 0.0f )
		hasScale = true;

	pglBegin( GL_POLYGON );

	for( i = 0, v = p->verts[0]; i < p->numverts; i++, v += VERTEXSIZE )
	{
		if( hasScale )
			pglTexCoord2f(( v[3] + sOffset ) * xScale, ( v[4] + tOffset ) * yScale );
		else pglTexCoord2f( v[3] + sOffset, v[4] + tOffset );

		pglVertex3fv( v );
	}

	pglEnd();

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

	for( ; p != NULL; p = p->chain )
	{
		float	*v;
		int	i;

		pglBegin( GL_POLYGON );

		v = p->verts[0];
		for( i = 0; i < p->numverts; i++, v += VERTEXSIZE )
		{
			if( !dynamic ) pglTexCoord2f( v[5], v[6] );
			else pglTexCoord2f( v[5] - soffset, v[6] - toffset );
			pglVertex3fv( v );
		}
		pglEnd ();
	}
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

	if( CVAR_TO_BOOL( r_fullbright ) || !cl.worldmodel->lightdata )
		return;

	if( RI.currententity )
	{
		if( RI.currententity->curstate.effects & EF_FULLBRIGHT )
			return;	// disabled by user

		// check for rendermode
		switch( RI.currententity->curstate.rendermode )
		{
		case kRenderTransTexture:
		case kRenderTransColor:
		case kRenderTransAdd:
		case kRenderGlow:
			return; // no lightmaps
		}
	}

	GL_SetupFogColorForSurfaces ();

	if( !CVAR_TO_BOOL( r_lightmap ))
		pglEnable( GL_BLEND );
	else pglDisable( GL_BLEND );

	// lightmapped solid surfaces
	pglDepthMask( GL_FALSE );
	pglDepthFunc( GL_EQUAL );

	pglDisable( GL_ALPHA_TEST );
	pglBlendFunc( GL_ZERO, GL_SRC_COLOR );
	pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );

	// render static lightmaps first
	for( i = 0; i < MAX_LIGHTMAPS; i++ )
	{
		if( gl_lms.lightmap_surfaces[i] )
		{
			GL_Bind( GL_TEXTURE0, tr.lightmapTextures[i] );

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

		GL_Bind( GL_TEXTURE0, tr.dlightTexture );
		newsurf = gl_lms.dynamic_surfaces;

		for( surf = gl_lms.dynamic_surfaces; surf != NULL; surf = surf->info->lightmapchain )
		{
			int		smax, tmax;
			int		sample_size;
			mextrasurf_t	*info = surf->info;
			byte		*base;

			sample_size = Mod_SampleSizeForFace( surf );
			smax = ( info->lightextents[0] / sample_size ) + 1;
			tmax = ( info->lightextents[1] / sample_size ) + 1;

			if( LM_AllocBlock( smax, tmax, &surf->info->dlight_s, &surf->info->dlight_t ))
			{
				base = gl_lms.lightmap_buffer;
				base += ( surf->info->dlight_t * BLOCK_SIZE + surf->info->dlight_s ) * 4;

				R_BuildLightMap( surf, base, BLOCK_SIZE * 4, true );
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
					Host_Error( "AllocBlock: full\n" );

				base = gl_lms.lightmap_buffer;
				base += ( surf->info->dlight_t * BLOCK_SIZE + surf->info->dlight_s ) * 4;

				R_BuildLightMap( surf, base, BLOCK_SIZE * 4, true );
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

	pglDisable( GL_BLEND );
	pglDepthMask( GL_TRUE );
	pglDepthFunc( GL_LEQUAL );
	pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE );
	pglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );

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
	pglEnable( GL_BLEND );
	pglDepthMask( GL_FALSE );
	pglDisable( GL_ALPHA_TEST );
	pglBlendFunc( GL_ONE, GL_ONE );
	pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );

	for( i = 1; i < MAX_TEXTURES; i++ )
	{
		es = fullbright_surfaces[i];
		if( !es ) continue;

		GL_Bind( GL_TEXTURE0, i );

		for( p = es; p; p = p->lumachain )
			DrawGLPoly( p->surf->polys, 0.0f, 0.0f );

		fullbright_surfaces[i] = NULL;
		es->lumachain = NULL;
	}

	pglDisable( GL_BLEND );
	pglDepthMask( GL_TRUE );
	pglDisable( GL_ALPHA_TEST );
	pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE );

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

	pglEnable( GL_BLEND );
	pglBlendFunc( GL_DST_COLOR, GL_SRC_COLOR );
	pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL );
	pglDepthFunc( GL_EQUAL );

	for( i = 1; i < MAX_TEXTURES; i++ )
	{
		es = detail_surfaces[i];
		if( !es ) continue;

		GL_Bind( GL_TEXTURE0, i );

		for( p = es; p; p = p->detailchain )
		{
			fa = p->surf;
			glt = R_GetTexture( fa->texinfo->texture->gl_texturenum ); // get texture scale
			DrawGLPoly( fa->polys, glt->xscale, glt->yscale );
                    }

		detail_surfaces[i] = NULL;
		es->detailchain = NULL;		
	}

	pglDisable( GL_BLEND );
	pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE );
	pglDepthFunc( GL_LEQUAL );

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

	GL_Bind( GL_TEXTURE0, t->gl_texturenum );

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
		if( pglIsEnabled( GL_FOG ))
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
	if(( fa->dlightframe == tr.framecount ))
	{
dynamic:
		// NOTE: at this point we have only valid textures
		if( r_dynamic->value ) is_dynamic = true;
	}

	if( is_dynamic )
	{
		if(( fa->styles[maps] >= 32 || fa->styles[maps] == 0 || fa->styles[maps] == 20 ) && ( fa->dlightframe != tr.framecount ))
		{
			byte		temp[132*132*4];
			mextrasurf_t	*info = fa->info;
			int		sample_size;
			int		smax, tmax;

			sample_size = Mod_SampleSizeForFace( fa );
			smax = ( info->lightextents[0] / sample_size ) + 1;
			tmax = ( info->lightextents[1] / sample_size ) + 1;

			R_BuildLightMap( fa, temp, smax * 4, true );
			R_SetCacheState( fa );
                              
			GL_Bind( GL_TEXTURE0, tr.lightmapTextures[fa->lightmaptexturenum] );

			pglTexSubImage2D( GL_TEXTURE_2D, 0, fa->light_s, fa->light_t, smax, tmax,
			GL_RGBA, GL_UNSIGNED_BYTE, temp );

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
	pglColor4ub( 255, 255, 255, 255 );
	R_LoadIdentity();	// set identity matrix

	GL_SetupFogColorForSurfaces();

	// restore worldmodel
	RI.currententity = clgame.entities;
	RI.currentmodel = RI.currententity->model;

	if( FBitSet( world.flags, FWORLD_SKYSPHERE ) && !FBitSet( world.flags, FWORLD_CUSTOM_SKYBOX ))
	{
		pglDisable( GL_TEXTURE_2D );
		pglColor3f( 1.0f, 1.0f, 1.0f );
	}

	// clip skybox surfaces
	for( s = skychain; s != NULL; s = s->texturechain )
		R_AddSkyBoxSurface( s );

	if( FBitSet( world.flags, FWORLD_SKYSPHERE ) && !FBitSet( world.flags, FWORLD_CUSTOM_SKYBOX ))
	{
		pglEnable( GL_TEXTURE_2D );
		if( skychain )
			R_DrawClouds();
		skychain = NULL;
	}

	for( i = 0; i < cl.worldmodel->numtextures; i++ )
	{
		t = cl.worldmodel->textures[i];
		if( !t ) continue;

		s = t->texturechain;

		if( !s || ( i == tr.skytexturenum ))
			continue;

		if(( s->flags & SURF_DRAWTURB ) && clgame.movevars.wateralpha < 1.0f )
			continue;	// draw translucent water later

		if( CL_IsQuakeCompatible() && FBitSet( s->flags, SURF_TRANSPARENT ))
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
	pglColor4ub( 255, 255, 255, 255 );
	R_LoadIdentity(); // set identity matrix

	pglDisable( GL_BLEND );
	pglEnable( GL_ALPHA_TEST );
	pglAlphaFunc( GL_GREATER, 0.25f );

	GL_SetupFogColorForSurfaces();

	// restore worldmodel
	RI.currententity = clgame.entities;
	RI.currentmodel = RI.currententity->model;
	RI.currententity->curstate.rendermode = kRenderTransAlpha;
	draw_alpha_surfaces = false;

	for( i = 0; i < cl.worldmodel->numtextures; i++ )
	{
		t = cl.worldmodel->textures[i];
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
	pglAlphaFunc( GL_GREATER, DEFAULT_ALPHATEST );
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
	if( clgame.movevars.wateralpha >= 1.0f )
		return;

	// restore worldmodel
	RI.currententity = clgame.entities;
	RI.currentmodel = RI.currententity->model;

	// go back to the world matrix
	R_LoadIdentity();

	pglEnable( GL_BLEND );
	pglDepthMask( GL_FALSE );
	pglDisable( GL_ALPHA_TEST );
	pglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
	pglColor4f( 1.0f, 1.0f, 1.0f, clgame.movevars.wateralpha );

	for( i = 0; i < cl.worldmodel->numtextures; i++ )
	{
		t = cl.worldmodel->textures[i];
		if( !t ) continue;

		s = t->texturechain;
		if( !s ) continue;

		if( !FBitSet( s->flags, SURF_DRAWTURB ))
			continue;

		// set modulate mode explicitly
		GL_Bind( GL_TEXTURE0, t->gl_texturenum );

		for( ; s; s = s->texturechain )
			EmitWaterPolys( s, false );
			
		t->texturechain = NULL;
	}

	pglDisable( GL_BLEND );
	pglDepthMask( GL_TRUE );
	pglDisable( GL_ALPHA_TEST );
	pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE );
	pglColor4ub( 255, 255, 255, 255 );
}

/*
=================
R_SurfaceCompare

compare translucent surfaces
=================
*/
static int R_SurfaceCompare( const sortedface_t *a, const sortedface_t *b )
{
	msurface_t	*surf1, *surf2;
	vec3_t		org1, org2;
	float		len1, len2;

	surf1 = (msurface_t *)a->surf;
	surf2 = (msurface_t *)b->surf;

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
		pglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
		break;
	case kRenderTransColor:
		pglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
		pglColor4ub( e->curstate.rendercolor.r, e->curstate.rendercolor.g, e->curstate.rendercolor.b, e->curstate.renderamt );
		pglTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
		pglDisable( GL_TEXTURE_2D );
		pglEnable( GL_BLEND );
		break;
	case kRenderTransAdd:
		pglTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
		pglColor4f( tr.blend, tr.blend, tr.blend, 1.0f );
		pglBlendFunc( GL_ONE, GL_ONE );
		pglDepthMask( GL_FALSE );
		pglEnable( GL_BLEND );
		break;
	case kRenderTransAlpha:
		pglEnable( GL_ALPHA_TEST );
		pglTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
		if( CL_IsQuakeCompatible( ))
		{
			pglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
			pglColor4f( 1.0f, 1.0f, 1.0f, tr.blend );
			pglEnable( GL_BLEND );
		}
		else
		{
			pglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
			pglDisable( GL_BLEND );
		}
		pglAlphaFunc( GL_GREATER, 0.25f );
		break;
	default:
		pglTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
		pglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
		pglColor4f( 1.0f, 1.0f, 1.0f, tr.blend );
		pglDepthMask( GL_FALSE );
		pglEnable( GL_BLEND );
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

	if( CL_IsQuakeCompatible() && FBitSet( clmodel->flags, MODEL_TRANSPARENT ))
		e->curstate.rendermode = kRenderTransAlpha;

	e->visframe = tr.realframecount; // visible

	if( rotated ) Matrix4x4_VectorITransform( RI.objectMatrix, RI.cullorigin, tr.modelorg );
	else VectorSubtract( RI.cullorigin, e->origin, tr.modelorg );

	// calculate dynamic lighting for bmodel
	for( k = 0, l = cl_dlights; k < MAX_DLIGHTS; k++, l++ )
	{
		if( l->die < cl.time || !l->radius )
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
		R_AllowFog( false );

	psurf = &clmodel->surfaces[clmodel->firstmodelsurface];
	num_sorted = 0;

	for( i = 0; i < clmodel->nummodelsurfaces; i++, psurf++ )
	{
		if( FBitSet( psurf->flags, SURF_DRAWTURB ) && !CL_IsQuakeCompatible( ))
		{
			if( psurf->plane->type != PLANE_Z && !FBitSet( e->curstate.effects, EF_WATERSIDES ))
				continue;
			if( mins[2] + 1.0 >= psurf->plane->dist )
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

		if( num_sorted < world.max_surfaces )
		{
			world.draw_surfaces[num_sorted].surf = psurf;
			world.draw_surfaces[num_sorted].cull = cull_type;
			num_sorted++;
		}
	}

	// sort faces if needs
	if( !FBitSet( clmodel->flags, MODEL_LIQUID ) && e->curstate.rendermode == kRenderTransTexture && !CVAR_TO_BOOL( gl_nosort ))
		qsort( world.draw_surfaces, num_sorted, sizeof( sortedface_t ), R_SurfaceCompare );

	// draw sorted translucent surfaces
	for( i = 0; i < num_sorted; i++ )
		R_RenderBrushPoly( world.draw_surfaces[i].surf, world.draw_surfaces[i].cull );

	if( e->curstate.rendermode == kRenderTransColor )
		pglEnable( GL_TEXTURE_2D );

	DrawDecalsBatch();
	GL_ResetFogColor();
	R_BlendLightmaps();
	R_RenderFullbrights();
	R_RenderDetails();

	// restore fog here
	if( e->curstate.rendermode == kRenderTransAdd )
		R_AllowFog( true );

	e->curstate.rendermode = old_rendermode;
	pglDisable( GL_ALPHA_TEST );
	pglAlphaFunc( GL_GREATER, DEFAULT_ALPHATEST );
	pglDisable( GL_BLEND );
	pglDepthMask( GL_TRUE );
	R_DrawModelHull();	// draw before restore
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
			R_StoreEfrags( &pleaf->efrags, tr.realframecount );

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
	for( c = node->numsurfaces, surf = cl.worldmodel->surfaces + node->firstsurface; c; c--, surf++ )
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
		R_StoreEfrags( &pleaf->efrags, tr.realframecount );

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
		for( c = node->numsurfaces, surf = cl.worldmodel->surfaces + node->firstsurface; c; c--, surf++ )
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
	RI.currententity = clgame.entities;
	RI.currentmodel = RI.currententity->model;

	if( !RI.drawWorld || RI.onlyClientDraw )
		return;

	VectorCopy( RI.cullorigin, tr.modelorg );
	memset( gl_lms.lightmap_surfaces, 0, sizeof( gl_lms.lightmap_surfaces ));
	memset( fullbright_surfaces, 0, sizeof( fullbright_surfaces ));
	memset( detail_surfaces, 0, sizeof( detail_surfaces ));

	gl_lms.dynamic_surfaces = NULL;
	pglDisable( GL_ALPHA_TEST );
	pglDisable( GL_BLEND );
	tr.blend = 1.0f;

	R_ClearSkyBox ();

	start = Sys_DoubleTime();
	if( RI.drawOrtho )
		R_DrawWorldTopView( cl.worldmodel->nodes, RI.frustum.clipFlags );
	else R_RecursiveWorldNode( cl.worldmodel->nodes, RI.frustum.clipFlags );
	end = Sys_DoubleTime();

	r_stats.t_world_node = end - start;

	start = Sys_DoubleTime();
	R_DrawTextureChains();

	if( !CL_IsDevOverviewMode( ))
	{
		DrawDecalsBatch();
		GL_ResetFogColor();
		R_BlendLightmaps();
		R_RenderFullbrights();
		R_RenderDetails();

		if( skychain )
			R_DrawSkyBox();
	}

	end = Sys_DoubleTime();

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
			leaf = Mod_PointInLeaf( test, cl.worldmodel->nodes );
		}
		else
		{
			VectorSet( test, RI.pvsorigin[0], RI.pvsorigin[1], RI.pvsorigin[2] + 16.0f );
			leaf = Mod_PointInLeaf( test, cl.worldmodel->nodes );
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

	if( r_novis->value || RI.drawOrtho || !RI.viewleaf || !cl.worldmodel->visdata )
		novis = true;

	Mod_FatPVS( RI.pvsorigin, REFPVS_RADIUS, RI.visbytes, world.visbytes, FBitSet( RI.params, RP_OLDVIEWLEAF ), novis );
	if( force && !novis ) Mod_FatPVS( test, REFPVS_RADIUS, RI.visbytes, world.visbytes, true, novis );

	for( i = 0; i < cl.worldmodel->numleafs; i++ )
	{
		if( CHECKVISBIT( RI.visbytes, i ))
		{
			node = (mnode_t *)&cl.worldmodel->leafs[i+1];
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
void GL_CreateSurfaceLightmap( msurface_t *surf )
{
	int		smax, tmax;
	int		sample_size;
	mextrasurf_t	*info = surf->info;
	byte		*base;

	if( !loadmodel->lightdata )
		return;

	if( FBitSet( surf->flags, SURF_DRAWTILED ))
		return;

	sample_size = Mod_SampleSizeForFace( surf );
	smax = ( info->lightextents[0] / sample_size ) + 1;
	tmax = ( info->lightextents[1] / sample_size ) + 1;

	if( !LM_AllocBlock( smax, tmax, &surf->light_s, &surf->light_t ))
	{
		LM_UploadBlock( false );
		LM_InitBlock();

		if( !LM_AllocBlock( smax, tmax, &surf->light_s, &surf->light_t ))
			Host_Error( "AllocBlock: full\n" );
	}

	surf->lightmaptexturenum = gl_lms.current_lightmap_texture;

	base = gl_lms.lightmap_buffer;
	base += ( surf->light_t * BLOCK_SIZE + surf->light_s ) * 4;

	R_SetCacheState( surf );
	R_BuildLightMap( surf, base, BLOCK_SIZE * 4, false );
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

	if( !cl.video_prepped )
		return; // wait for worldmodel

	ClearBits( vid_brightness->flags, FCVAR_CHANGED );
	ClearBits( vid_gamma->flags, FCVAR_CHANGED );

	// release old lightmaps
	for( i = 0; i < MAX_LIGHTMAPS; i++ )
	{
		if( !tr.lightmapTextures[i] ) break;
		GL_FreeTexture( tr.lightmapTextures[i] );
	}

	memset( tr.lightmapTextures, 0, sizeof( tr.lightmapTextures ));
	gl_lms.current_lightmap_texture = 0;

	// setup all the lightstyles
	CL_RunLightStyles();

	LM_InitBlock();	

	for( i = 0; i < cl.nummodels; i++ )
	{
		if(( m = CL_ModelHandle( i + 1 )) == NULL )
			continue;

		if( m->name[0] == '*' || m->type != mod_brush )
			continue;

		loadmodel = m;

		for( j = 0; j < m->numsurfaces; j++ )
			GL_CreateSurfaceLightmap( m->surfaces + j );
	}
	LM_UploadBlock( false );

	if( clgame.drawFuncs.GL_BuildLightmaps )
	{
		// build lightmaps on the client-side
		clgame.drawFuncs.GL_BuildLightmaps( );
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

	// update the lightmap blocksize
	if( FBitSet( host.features, ENGINE_LARGE_LIGHTMAPS ))
		tr.block_size = BLOCK_SIZE_MAX;
	else tr.block_size = BLOCK_SIZE_DEFAULT;
	
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

	for( i = 0; i < cl.nummodels; i++ )
	{
		if(( m = CL_ModelHandle( i + 1 )) == NULL )
			continue;

		if( m->name[0] == '*' || m->type != mod_brush )
			continue;

		for( j = 0; j < m->numsurfaces; j++ )
		{
			// clearing all decal chains
			m->surfaces[j].pdecals = NULL;
			m->surfaces[j].visframe = 0;
			loadmodel = m;

			GL_CreateSurfaceLightmap( m->surfaces + j );

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

	if( clgame.drawFuncs.GL_BuildLightmaps )
	{
		// build lightmaps on the client-side
		clgame.drawFuncs.GL_BuildLightmaps( );
	}

	// now gamma and brightness are valid
	ClearBits( vid_brightness->flags, FCVAR_CHANGED );
	ClearBits( vid_gamma->flags, FCVAR_CHANGED );
}

void GL_InitRandomTable( void )
{
	int	tu, tv;

	// make random predictable
	COM_SetRandomSeed( 255 );

	for( tu = 0; tu < MOD_FRAMES; tu++ )
	{
		for( tv = 0; tv < MOD_FRAMES; tv++ )
		{
			rtable[tu][tv] = COM_RandomLong( 0, 0x7FFF );
		}
	}

	COM_SetRandomSeed( 0 );
}