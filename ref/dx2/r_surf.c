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
===============
R_TextureAnimation

Returns the proper texture for a given time and surface
===============
*/
static texture_t *R_TextureAnimation( msurface_t *s )
{
	texture_t	*base = s->texinfo->texture;
	// TODO
	return base;
}

static void DrawGLPoly( glpoly2_t *p, float xScale, float yScale )
{
	float		*v;
	float		sOffset, sy;
	float		tOffset, cy;
	cl_entity_t	*e = RI.currententity;
	int		i, hasScale = false;

	if( !p ) return;

	{
		sOffset = tOffset = 0.0f;
	}

	if( xScale != 0.0f && yScale != 0.0f )
		hasScale = true;

	d3dEBContext_t ebc = { 0 };
	if( !D3D_StartExecuteBuffer( &ebc ))
		return;

	void *cur = ebc.data;

	D3DLVERTEX* verts = (D3DLVERTEX*)cur;

	for( i = 0, v = p->verts[0]; i < p->numverts; i++, v += VERTEXSIZE )
	{
		D3DLVERTEX *dv = verts + i;
		if( hasScale )
		{
			dv->tu = ( v[3] + sOffset ) * xScale;
			dv->tv = ( v[4] + tOffset ) * yScale;
		}
		else
		{
			dv->tu = v[3] + sOffset;
			dv->tv = v[4] + tOffset;
		}

		dv->x = v[0];
		dv->y = v[1];
		dv->z = v[2];
		dv->color = 0xFFFFFFFF;
	}

	cur = (void*)(((D3DTLVERTEX*)cur) + p->numverts);

	void* insStart = cur;

	D3D_PutProcessVertices(&cur, D3DPROCESSVERTICES_TRANSFORM | D3DPROCESSVERTICES_UPDATEEXTENTS, 0, p->numverts);
	// align
	if (!(((ULONG)cur) & 7)) {
		D3D_PutInstruction(&cur, D3DOP_TRIANGLE, sizeof(D3DTRIANGLE), 0);
	}

	D3D_PutInstruction(&cur, D3DOP_STATETRANSFORM, sizeof(D3DSTATE), 1);
	D3D_PutTransformState(&cur, D3DTRANSFORMSTATE_WORLD, dxc.mtxWorld);

	D3D_PutInstruction(&cur, D3DOP_STATERENDER, sizeof(D3DSTATE), 4);
	D3D_PutRenderState(&cur, D3DRENDERSTATE_TEXTUREHANDLE, dxc.currentTexture);
	D3D_PutRenderState(&cur, D3DRENDERSTATE_TEXTUREMAPBLEND, D3DTBLEND_DECAL);
	D3D_PutRenderState(&cur, D3DRENDERSTATE_BLENDENABLE, FALSE);
	D3D_PutRenderState(&cur, D3DRENDERSTATE_ALPHATESTENABLE, FALSE);

	D3D_PutInstruction(&cur, D3DOP_TRIANGLE, sizeof(D3DTRIANGLE), p->numverts - 2);

	for (i = 0; i < p->numverts; i++)
	{
		D3D_SetTri(cur, 0, i + 1, i + 2);
		cur = (void*)(((D3DTRIANGLE*)cur) + 1);
	}

	D3D_PutInstruction(&cur, D3DOP_EXIT, 0, 0);

	ebc.vertCount = p->numverts;
	ebc.insOffset = ((char*)insStart - (char*)ebc.data);
	ebc.insLength = ((char*)cur - (char*)insStart);

	if( !D3D_EndExecuteBuffer( &ebc ))
		return;

	DXCheck(IDirect3DDevice_BeginScene(dxc.pd3dd));
	DXCheck(IDirect3DDevice_Execute(dxc.pd3dd, ebc.pd3deb, dxc.viewport, D3DEXECUTE_CLIPPED));
	DXCheck(IDirect3DDevice_EndScene(dxc.pd3dd));

	D3D_ReleaseExecuteBuffer( &ebc );

	//if( FBitSet( p->flags, SURF_DRAWTILED ))
	//	GL_SetupFogColorForSurfaces();
}

static void R_RenderBrushPoly( msurface_t *fa, int cull_type )
{
	texture_t	*t;

	//r_stats.c_world_polys++;

	if( fa->flags & SURF_DRAWSKY )
		return; // already handled

	t = R_TextureAnimation( fa );
		GL_Bind( XASH_TEXTURE0, t->gl_texturenum );
	DrawGLPoly( fa->polys, 0.0f, 0.0f );
}

static void R_DrawTextureChains( void )
{
	int		i;
	msurface_t	*s;
	texture_t		*t;
	model_t *WORLDMODEL = (gp_cl->models[1]);

	// make sure what color is reset
	Vector4Set( dxc.currentColor, 1, 1, 1, 1 );
	R_LoadIdentity();	// set identity matrix

	//GL_SetupFogColorForSurfaces();

	// restore worldmodel
	RI.currententity = CL_GetEntityByIndex( 0 );
	RI.currentmodel = RI.currententity->model;


	for( i = 0; i < WORLDMODEL->numtextures; i++ )
	{
		t = WORLDMODEL->textures[i];
		if( !t ) continue;

		s = t->texturechain;

		if( !s || ( i == tr.skytexturenum ))
			continue;

		for( ; s != NULL; s = s->texturechain )
			R_RenderBrushPoly( s, CULL_VISIBLE );
		t->texturechain = NULL;
	}
}

/*
=================
R_CullSurface

cull invisible surfaces
=================
*/
static int R_CullSurface( msurface_t *surf, gl_frustum_t *frustum, uint clipflags )
{
	cl_entity_t	*e = RI.currententity;

	if( !surf || !surf->texinfo || !surf->texinfo->texture )
		return CULL_OTHER;

	//TODO
	return CULL_VISIBLE;
}

static void R_RecursiveWorldNode( mnode_t *node, uint clipflags )
{
	int		i, clipped;
	msurface_t	*surf, **mark;
	mleaf_t		*pleaf;
	int		c, side;
	float		dot;
	mnode_t *children[2];
	int numsurfaces, firstsurface;
	model_t *WORLDMODEL = (gp_cl->models[1]);

loc0:
	if( node->contents == CONTENTS_SOLID )
		return; // hit a solid leaf

	if( node->visframe != tr.visframecount )
		return;

	if( clipflags && !r_nocull.value )
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

//		r_stats.c_world_leafs++;
		return;
	}

	// node is just a decision point, so go down the apropriate sides

	// find which side of the node we are on
	dot = PlaneDiff( tr.modelorg, node->plane );
	side = (dot >= 0.0f) ? 0 : 1;

	// recurse down the children, front side first
	node_children( children, node, WORLDMODEL );
	R_RecursiveWorldNode( children[side], clipflags );

	firstsurface = node_firstsurface( node, WORLDMODEL );
	numsurfaces = node_numsurfaces( node, WORLDMODEL );

	// draw stuff
	for( c = numsurfaces, surf = WORLDMODEL->surfaces + firstsurface; c; c--, surf++ )
	{
		if( R_CullSurface( surf, &RI.frustum, clipflags ))
			continue;

		if( surf->flags & SURF_DRAWSKY )
		{
#if 0
			// make sky chain to right clip the skybox
			surf->texturechain = skychain;
			skychain = surf;
#endif
		}
		else
		{
			surf->texturechain = surf->texinfo->texture->texturechain;
			surf->texinfo->texture->texturechain = surf;
		}
	}

	// recurse down the back side
	node = children[!side];
	goto loc0;
}


void R_DrawWorld( void )
{
	double	start, end;
	model_t *WORLDMODEL = (gp_cl->models[1]);

	// paranoia issues: when gl_renderer is "0" we need have something valid for currententity
	// to prevent crashing until HeadShield drawing.
	RI.currententity = CL_GetEntityByIndex( 0 );
	if( !RI.currententity )
		return;

	RI.currentmodel = RI.currententity->model;
	if( !RI.drawWorld || RI.onlyClientDraw )
		return;

	VectorCopy( RI.cullorigin, tr.modelorg );
	//memset( gl_lms.lightmap_surfaces, 0, sizeof( gl_lms.lightmap_surfaces ));
	//memset( fullbright_surfaces, 0, sizeof( fullbright_surfaces ));
	//memset( detail_surfaces, 0, sizeof( detail_surfaces ));

	//gl_lms.dynamic_surfaces = NULL;
	tr.blend = 1.0f;

	//R_ClearSkyBox ();

	start = gEngfuncs.pfnTime();
	if( RI.drawOrtho )
	{
		//R_DrawWorldTopView( WORLDMODEL->nodes, RI.frustum.clipFlags );
	}
	else R_RecursiveWorldNode( WORLDMODEL->nodes, RI.frustum.clipFlags );
	end = gEngfuncs.pfnTime();

	//r_stats.t_world_node = end - start;

	start = gEngfuncs.pfnTime();

	R_DrawTextureChains();

	end = gEngfuncs.pfnTime();

	//r_stats.t_world_draw = end - start;
	//tr.num_draw_decals = 0;
	//skychain = NULL;

	gEngfuncs.R_DrawWorldHull();
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
	model_t *WORLDMODEL = (gp_cl->models[1]);

	if( !RI.drawWorld ) return;

	if( FBitSet( r_novis.flags, FCVAR_CHANGED ) || tr.fResetVis )
	{
		// force recalc viewleaf
		ClearBits( r_novis.flags, FCVAR_CHANGED );
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
	if( r_lockpvs.value ) return;

	RI.oldviewleaf = RI.viewleaf;
	tr.visframecount++;

	if( r_novis.value || RI.drawOrtho || !RI.viewleaf || !WORLDMODEL->visdata )
		novis = true;

	gEngfuncs.R_FatPVS( RI.pvsorigin, r_pvs_radius->value, RI.visbytes, FBitSet( RI.params, RP_OLDVIEWLEAF ), novis );
	if( force && !novis )
		gEngfuncs.R_FatPVS( test, r_pvs_radius->value, RI.visbytes, true, novis );

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
