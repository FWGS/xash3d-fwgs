/*
r_surf.c - surface-related refresh code
Copyright (C) 2025-2026 lewa_j

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "r_local.h"
#include "xash3d_mathlib.h"


void GL_OrthoBounds( const float *mins, const float *maxs )
{
	;
}

byte *Mod_GetCurrentVis( void )
{
	return NULL;
}


void R_LightmapCoord( const vec3_t v, const msurface_t *surf, const float sample_size, vec2_t coords )
{
	const mextrasurf_t *info = surf->info;
	float s, t;

#define BLOCK_SIZE 256

	s = DotProduct( v, info->lmvecs[0] ) + info->lmvecs[0][3] - info->lightmapmins[0];
	s += surf->light_s * sample_size;
	s += sample_size * 0.5f;
	s /= BLOCK_SIZE * sample_size;

	t = DotProduct( v, info->lmvecs[1] ) + info->lmvecs[1][3] - info->lightmapmins[1];
	t += surf->light_t * sample_size;
	t += sample_size * 0.5f;
	t /= BLOCK_SIZE * sample_size;

#undef BLOCK_SIZE

	Vector2Set( coords, s, t );
}

static void R_TextureCoord( const vec3_t v, const msurface_t *surf, vec2_t coords )
{
	const mtexinfo_t *info = surf->texinfo;
	float s, t;

	s = DotProduct( v, info->vecs[0] );
	t = DotProduct( v, info->vecs[1] );

	if( !FBitSet( surf->flags, SURF_DRAWTURB ))
	{
		s = ( s + info->vecs[0][3] ) / info->texture->width;
		t = ( t + info->vecs[1][3] ) / info->texture->height;
	}

	Vector2Set( coords, s, t );
}

static void R_GetEdgePosition( const model_t *mod, const msurface_t *fa, int i, vec3_t vec )
{
	const int lindex = mod->surfedges[fa->firstedge + i];

	if( FBitSet( mod->flags, MODEL_QBSP2 ))
	{
		const medge32_t *pedges = mod->edges32;

		if( lindex > 0 )
			VectorCopy( mod->vertexes[pedges[lindex].v[0]].position, vec );
		else
			VectorCopy( mod->vertexes[pedges[-lindex].v[1]].position, vec );
	}
	else
	{
		const medge16_t *pedges = mod->edges16;

		if( lindex > 0 )
			VectorCopy( mod->vertexes[pedges[lindex].v[0]].position, vec );
		else
			VectorCopy( mod->vertexes[pedges[-lindex].v[1]].position, vec );
	}
}

void GL_SubdivideSurface( model_t *mod, msurface_t *fa )
{
	;
}

/*
================
GL_BuildPolygonFromSurface
================
*/
static int GL_BuildPolygonFromSurface( model_t *mod, msurface_t *fa )
{
	int		i, lnumverts, nColinElim = 0;
	float		sample_size;
	texture_t		*tex;
	dx_texture_t	*dxt;
	glpoly2_t		*poly;

	if( !mod || !fa->texinfo || !fa->texinfo->texture )
		return nColinElim; // bad polygon ?

	if( FBitSet( fa->flags, SURF_CONVEYOR ) && fa->texinfo->texture->gl_texturenum != 0 )
	{
		dxt = R_GetTexture( fa->texinfo->texture->gl_texturenum );
		tex = fa->texinfo->texture;
		Assert( dxt != NULL && tex != NULL );

		// update conveyor widths for keep properly speed of scrolling
		dxt->srcWidth = tex->width;
		dxt->srcHeight = tex->height;
	}

	sample_size = gEngfuncs.Mod_SampleSizeForFace( fa );

	// reconstruct the polygon
	lnumverts = fa->numedges;

	// detach if already created, reconstruct again
	poly = fa->polys;
	fa->polys = NULL;

	// quake simple models (healthkits etc) need to be reconstructed their polys because LM coords has changed after the map change
	poly = Mem_Realloc( mod->mempool, poly, sizeof( glpoly2_t ) + lnumverts * VERTEXSIZE * sizeof( float ));
	poly->next = fa->polys;
	poly->flags = fa->flags;
	fa->polys = poly;
	poly->numverts = lnumverts;

	for( i = 0; i < lnumverts; i++ )
	{
		R_GetEdgePosition( mod, fa, i, poly->verts[i] );
		R_TextureCoord( poly->verts[i], fa, &poly->verts[i][3] );
		R_LightmapCoord( poly->verts[i], fa, sample_size, &poly->verts[i][5] );
	}

	// TODO or not TODO gl_keeptjunctions?

	poly->numverts = lnumverts;
	return nColinElim;
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
	int	i, j, nColinElim = 0;
	model_t	*m;
#if 0
	// release old lightmaps
	for( i = 0; i < MAX_LIGHTMAPS; i++ )
	{
		if( !tr.lightmapTextures[i] ) break;
		GL_FreeTexture( tr.lightmapTextures[i] );
	}

	memset( tr.lightmapTextures, 0, sizeof( tr.lightmapTextures ));
#endif
	memset( &RI, 0, sizeof( RI ));
#if 0
	// update the lightmap blocksize
	if( FBitSet( gp_host->features, ENGINE_LARGE_LIGHTMAPS ) || tr.world->version == QBSP2_VERSION || r_large_lightmaps.value )
		tr.block_size = BLOCK_SIZE_MAX;
	else tr.block_size = BLOCK_SIZE_DEFAULT;

	skychain = NULL;
#endif
	tr.framecount = tr.visframecount = 1;	// no dlight cache
	//gl_lms.current_lightmap_texture = 0;
	tr.modelviewIdentity = false;
	tr.realframecount = 1;
#if 0
	// setup the texture for dlights
	R_InitDlightTexture();

	// setup all the lightstyles
	CL_RunLightStyles((lightstyle_t *)ENGINE_GET_PARM( PARM_GET_LIGHTSTYLES_PTR ));

	LM_InitBlock();
#endif
	for( i = 0; i < gp_cl->nummodels; i++ )
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

			//GL_CreateSurfaceLightmap( m->surfaces + j, m );

			if( m->surfaces[j].flags & SURF_DRAWTURB )
				continue;

			nColinElim += GL_BuildPolygonFromSurface( m, m->surfaces + j );
		}

		// clearing visframe
		for( j = 0; j < m->numleafs; j++ )
			m->leafs[j+1].visframe = 0;
		for( j = 0; j < m->numnodes; j++ )
			m->nodes[j].visframe = 0;
	}

	//LM_UploadBlock( false );

	if( gEngfuncs.drawFuncs->GL_BuildLightmaps )
	{
		// build lightmaps on the client-side
		gEngfuncs.drawFuncs->GL_BuildLightmaps( );
	}
}
