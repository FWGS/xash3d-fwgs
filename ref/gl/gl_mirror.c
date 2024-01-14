/*
gl_mirror.c - draw reflected surfaces
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

#include "gl_local.h"
#include "mod_local.h"
#include "xash3d_mathlib.h"

static void GL_LoadTexMatrix( const matrix4x4 m )
{
	pglMatrixMode( GL_TEXTURE );
	GL_LoadMatrix( m );
	glState.texIdentityMatrix[glState.activeTMU] = false;
}

static void Matrix4x4_CreateScale( matrix4x4 out, float x )
{
	out[0][0] = x;
	out[0][1] = out[0][2] = out[0][3] = 0.0f;
	out[1][0] = out[1][2] = out[1][3] = 0.0f;
	out[1][1] = x;
	out[2][0] = out[2][1] = out[2][3] = 0.0f;
	out[2][2] = x;
	out[3][0] = out[3][1] = out[3][2] = 0.0f;
	out[3][3] = 1.0f;
}

static void Matrix4x4_ConcatScale( matrix4x4 out, float x )
{
	matrix4x4       base, temp;

	Matrix4x4_Copy( base, out );
	Matrix4x4_CreateScale( temp, x );
	Matrix4x4_Concat( out, base, temp );
}


/*
================
R_BeginDrawMirror

Setup texture matrix for mirror texture
================
*/
void R_BeginDrawMirror( msurface_t *fa )
{
	matrix4x4		m1, m2, matrix;
	GLfloat		genVector[4][4];
	int		i;

	Matrix4x4_Copy( matrix, fa->info->mirrormatrix );

	Matrix4x4_LoadIdentity( m1 );
	Matrix4x4_ConcatScale( m1, 0.5f );
	Matrix4x4_Concat( m2, m1, matrix );

	Matrix4x4_LoadIdentity( m1 );
	Matrix4x4_ConcatTranslate( m1, 0.5f, 0.5f, 0.5f );
	Matrix4x4_Concat( matrix, m1, m2 );

	for( i = 0; i < 4; i++ )
	{
		genVector[0][i] = i == 0 ? 1 : 0;
		genVector[1][i] = i == 1 ? 1 : 0;
		genVector[2][i] = i == 2 ? 1 : 0;
		genVector[3][i] = i == 3 ? 1 : 0;
	}

	GL_TexGen( GL_S, GL_OBJECT_LINEAR );
	GL_TexGen( GL_T, GL_OBJECT_LINEAR );
	GL_TexGen( GL_R, GL_OBJECT_LINEAR );
	GL_TexGen( GL_Q, GL_OBJECT_LINEAR );

	pglTexGenfv( GL_S, GL_OBJECT_PLANE, genVector[0] );
	pglTexGenfv( GL_T, GL_OBJECT_PLANE, genVector[1] );
	pglTexGenfv( GL_R, GL_OBJECT_PLANE, genVector[2] );
	pglTexGenfv( GL_Q, GL_OBJECT_PLANE, genVector[3] );

	GL_LoadTexMatrix( matrix );
}

/*
================
R_EndDrawMirror

Restore identity texmatrix
================
*/
void R_EndDrawMirror( void )
{
	GL_CleanUpTextureUnits( 0 );
	pglMatrixMode( GL_MODELVIEW );
}

/*
=============================================================

	MIRROR RENDERING

=============================================================
*/
/*
================
R_PlaneForMirror

Get transformed mirrorplane and entity matrix
================
*/
static void R_PlaneForMirror( msurface_t *surf, mplane_t *out, matrix4x4 m )
{
	cl_entity_t	*ent;

	ent = RI.currententity;

	// setup mirror plane
	*out = *surf->plane;

	if( surf->flags & SURF_PLANEBACK )
	{
		VectorNegate( out->normal, out->normal );
		out->dist = -out->dist;
	}

	if( !VectorIsNull( ent->origin ) || !VectorIsNull( ent->angles ))
	{
		mplane_t	tmp;

		if( !VectorIsNull( ent->angles )) Matrix4x4_CreateFromEntity( m, ent->angles, ent->origin, 1.0f );
		else Matrix4x4_CreateFromEntity( m, vec3_origin, ent->origin, 1.0f );

		tmp = *out;

		// transform mirror plane by entity matrix
		Matrix4x4_TransformPositivePlane( m, tmp.normal, tmp.dist, out->normal, &out->dist );
	}
	else Matrix4x4_LoadIdentity( m );
}

/*
================
R_AllocateMirrorTexture

Allocate the screen texture and make copy
================
*/
static int R_AllocateMirrorTexture( void )
{
	rgbdata_t	r_screen;
	int	i, texture;
	char	txName[16];

	i = tr.num_mirrors_used;
	if( i >= MAX_MIRRORS )
	{
		gEngfuncs.Con_Printf( S_ERROR "%s: mirror textures limit exceeded!\n", __func__ );
		return 0;	// disable
	}

	texture = tr.mirrorTextures[i];
	tr.num_mirrors_used++;

	if( !texture )
	{
		// not initialized ?
		memset( &r_screen, 0, sizeof( r_screen ));
		Q_snprintf( txName, sizeof( txName ), "*screen%i", i );

		r_screen.width = RI.viewport[2];
		r_screen.height = RI.viewport[3];
		r_screen.type = PF_RGBA_32;
		r_screen.size = r_screen.width * r_screen.height * 4;
		r_screen.flags = IMAGE_HAS_COLOR;
		r_screen.buffer = NULL; // create empty texture for now
		tr.mirrorTextures[i] = GL_LoadTextureInternal( txName, &r_screen, TF_IMAGE );
		texture = tr.mirrorTextures[i];
	}

	GL_Bind( XASH_TEXTURE0, texture );
	pglCopyTexImage2D( GL_TEXTURE_2D, 0, GL_RGB, RI.viewport[0], RI.viewport[1], RI.viewport[2], RI.viewport[3], 0 );

	return texture;
}

/*
================
R_DrawMirrors

Draw all viewpasess from mirror position
Mirror textures will be drawn in normal pass
================
*/
void R_DrawMirrors( void )
{
	ref_instance_t	oldRI;
	mplane_t		plane;
	msurface_t	*surf, *surf2;
	int		i, oldframecount;
	mextrasurf_t	*es, *tmp, *mirrorchain;
	vec3_t		forward, right, up;
	vec3_t		origin, angles;
	matrix4x4		mirrormatrix;
	cl_entity_t	*e;
	model_t		*m;
	float		d;

	if( !tr.draw_list->num_mirror_entities ) return; // mo mirrors for this frame

	oldRI = RI; // make refinst backup
	oldframecount = tr.framecount;

	for( i = 0; i < tr.draw_list->num_mirror_entities; i++ )
	{
		mirrorchain = tr.draw_list->mirror_entities[i].chain;

		for( es = mirrorchain; es != NULL; es = es->mirrorchain )
		{
			RI.currententity = e = tr.draw_list->mirror_entities[i].ent;
			RI.currentmodel = m = RI.currententity->model;

			surf = es->surf;

			ASSERT( RI.currententity != NULL );
			ASSERT( RI.currentmodel != NULL );

			// NOTE: copy mirrortexture and mirrormatrix from another surfaces
			// from this entity\world that has same planes and reduce number of viewpasses

			// make sure what we have one pass at least
			if( es != mirrorchain )
			{
				for( tmp = mirrorchain; tmp != es; tmp = tmp->mirrorchain )
				{
					surf2 = tmp->surf;

					if( !tmp->mirrortexturenum )
						continue;	// not filled?

					if( surf->plane->dist != surf2->plane->dist )
						continue;

					if( !VectorCompare( surf->plane->normal, surf2->plane->normal ))
						continue;

					// found surface with same plane!
					break;
				}

				if( tmp != es && tmp && tmp->mirrortexturenum )
				{
					// just copy reflection texture from surface with same plane
					Matrix4x4_Copy( es->mirrormatrix, tmp->mirrormatrix );
					es->mirrortexturenum = tmp->mirrortexturenum;
					continue;	// pass skiped
				}
			} 

			R_PlaneForMirror( surf, &plane, mirrormatrix );

			d = -2.0f * ( DotProduct( RI.vieworg, plane.normal ) - plane.dist );
			VectorMA( RI.vieworg, d, plane.normal, origin );

			d = -2.0f * DotProduct( RI.vforward, plane.normal );
			VectorMA( RI.vforward, d, plane.normal, forward );
			VectorNormalize( forward );

			d = -2.0f * DotProduct( RI.vright, plane.normal );
			VectorMA( RI.vright, d, plane.normal, right );
			VectorNormalize( right );

			d = -2.0f * DotProduct( RI.vup, plane.normal );
			VectorMA( RI.vup, d, plane.normal, up );
			VectorNormalize( up );

			VectorsAngles( forward, right, up, angles );
			angles[ROLL] = -angles[ROLL];

			RI.params = RP_MIRRORVIEW|RP_CLIPPLANE|RP_OLDVIEWLEAF;
			RI.clipPlane = plane;

			GL_FrustumSetPlane( &RI.frustum, FRUSTUM_NEAR, plane.normal, plane.dist );

			RI.viewangles[0] = anglemod( angles[0] );
			RI.viewangles[1] = anglemod( angles[1] );
			RI.viewangles[2] = anglemod( angles[2] );
			VectorCopy( origin, RI.vieworg );
			VectorCopy( origin, RI.cullorigin );

			// put pvsorigin before the mirror plane to avoid get full visibility on world mirrors
			if( RI.currententity == tr.entities )
			{
				VectorMA( es->origin, 1.0f, plane.normal, origin );
			}
			else
			{
				Matrix4x4_VectorTransform( mirrormatrix, es->origin, origin );
				VectorMA( origin, 1.0f, plane.normal, origin );
			}

			VectorCopy( origin, RI.pvsorigin );

			if( GL_Support( GL_ARB_TEXTURE_NPOT_EXT ))
			{
				// allow screen size
				RI.viewport[2] = bound( 96, RI.viewport[2], 1024 );
				RI.viewport[3] = bound( 72, RI.viewport[3], 768 );
			}
			else
			{
				RI.viewport[2] = NearestPOW( RI.viewport[2], true );
				RI.viewport[3] = NearestPOW( RI.viewport[3], true );
				RI.viewport[2] = bound( 128, RI.viewport[2], 1024 );
				RI.viewport[3] = bound( 64, RI.viewport[3], 512 );
			}

			R_RenderScene();
			r_stats.c_mirror_passes++;

			es->mirrortexturenum = R_AllocateMirrorTexture();

			// create personal projection matrix for mirror
			if( VectorIsNull( e->origin ) && VectorIsNull( e->angles ))
			{
				Matrix4x4_Copy( es->mirrormatrix, RI.worldviewProjectionMatrix );
			}
			else
			{
				Matrix4x4_ConcatTransforms( RI.modelviewMatrix, RI.worldviewMatrix, mirrormatrix );
				Matrix4x4_Concat( es->mirrormatrix, RI.projectionMatrix, RI.modelviewMatrix );
			}			

			RI = oldRI; // restore ref instance
		}

		// clear chain for this entity
		for( es = mirrorchain; es != NULL; )
		{
			tmp = es->mirrorchain;
			es->mirrorchain = NULL;
			es = tmp;
		}

		tr.draw_list->mirror_entities[i].chain = NULL; // done
		tr.draw_list->mirror_entities[i].ent = NULL;
	}

	RI.viewleaf = NULL;		// force markleafs next frame
	tr.draw_list->num_mirror_entities = 0;
	tr.num_mirrors_used = 0;
}

/*
================
R_RecursiveMirrorNode
================
*/
static void R_RecursiveMirrorNode( mnode_t *node, uint clipflags )
{
	int		i, clipped;
	msurface_t	*surf, **mark;
	mleaf_t		*pleaf;
	int		c, side;
	float		dot;

	if( node->contents == CONTENTS_SOLID )
		return; // hit a solid leaf

	if( node->visframe != tr.visframecount )
		return;

	if( clipflags )
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
		return;
	}

	// node is just a decision point, so go down the apropriate sides

	// find which side of the node we are on
	dot = PlaneDiff( tr.modelorg, node->plane );
	side = (dot >= 0) ? 0 : 1;

	// recurse down the children, front side first
	R_RecursiveMirrorNode( node->children[side], clipflags );

	// draw stuff
	for( c = node->numsurfaces, surf = WORLDMODEL->surfaces + node->firstsurface; c; c--, surf++ )
	{
		if( !FBitSet( surf->flags, SURF_REFLECT ))
			continue;

		if( R_CullSurface( surf, &RI.frustum, clipflags ))
			continue;

		surf->info->mirrorchain = tr.draw_list->mirror_entities[0].chain;
		tr.draw_list->mirror_entities[0].chain = surf->info;
	}

	// recurse down the back side
	R_RecursiveMirrorNode( node->children[!side], clipflags );
}

/*
=================
R_FindBmodelMirrors

Check all bmodel surfaces and make personal mirror chain
=================
*/
static void R_FindBmodelMirrors( cl_entity_t *e, qboolean static_entity )
{
	vec3_t		mins, maxs;
	msurface_t	*psurf;
	model_t		*clmodel;
	qboolean		rotated;
	gl_frustum_t	*frustum = NULL;
	int		i;
	draw_list_t *const draw_list = tr.draw_list;

	if( draw_list->num_mirror_entities >= MAX_MIRROR_ENTITIES )
		return;

	clmodel = e->model;

	// don't draw any water reflections if we underwater
	if( ENGINE_GET_PARM( PARM_WATER_LEVEL ) >= 3 && FBitSet( clmodel->flags, MODEL_LIQUID ))
		return;

	if( static_entity )
	{
		Matrix4x4_LoadIdentity( RI.objectMatrix );

		if( R_CullBox( clmodel->mins, clmodel->maxs ))
			return;

		VectorCopy( RI.cullorigin, tr.modelorg );
		frustum = &RI.frustum;
	}
	else
	{
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

		if( !VectorIsNull( e->origin ) || !VectorIsNull( e->angles ))
		{
			if( rotated ) Matrix4x4_CreateFromEntity( RI.objectMatrix, e->angles, e->origin, 1.0f );
			else Matrix4x4_CreateFromEntity( RI.objectMatrix, vec3_origin, e->origin, 1.0f );
		}
		else Matrix4x4_LoadIdentity( RI.objectMatrix );

		e->visframe = tr.framecount; // visible

		if( rotated ) Matrix4x4_VectorITransform( RI.objectMatrix, RI.cullorigin, tr.modelorg );
		else VectorSubtract( RI.cullorigin, e->origin, tr.modelorg );
	}

	psurf = &clmodel->surfaces[clmodel->firstmodelsurface];
	for( i = 0; i < clmodel->nummodelsurfaces; i++, psurf++ )
	{
		if( !FBitSet( psurf->flags, SURF_REFLECT ))
			continue;

		if( R_CullSurface( psurf, frustum, 0 ))
			continue;

		psurf->info->mirrorchain = draw_list->mirror_entities[draw_list->num_mirror_entities].chain;
		draw_list->mirror_entities[draw_list->num_mirror_entities].chain = psurf->info;
	}

	// store new mirror entity
	if( !static_entity && draw_list->mirror_entities[draw_list->num_mirror_entities].chain != NULL )
	{
		draw_list->mirror_entities[draw_list->num_mirror_entities].ent = RI.currententity;
		draw_list->num_mirror_entities++;
	}
}

/*
=================
R_CheckEntitiesOnList

Check all bmodels for mirror surfaces
=================
*/
static void R_CheckEntitiesOnList( void )
{
	int	i;

	// world has mirror surfaces
	if( tr.draw_list->mirror_entities[0].chain != NULL )
	{
		tr.draw_list->mirror_entities[0].ent = tr.entities;
		tr.draw_list->num_mirror_entities++;
	}

	// check solid entities
	for( i = 0; i < tr.draw_list->num_solid_entities; i++ )
	{
		RI.currententity = tr.draw_list->solid_entities[i];
		RI.currentmodel = RI.currententity->model;
	
		ASSERT( RI.currententity != NULL );
		ASSERT( RI.currententity->model != NULL );

		switch( RI.currentmodel->type )
		{
		case mod_brush:
			R_FindBmodelMirrors( RI.currententity, false );
			break;
		}
	}

	// check translucent entities
	for( i = 0; i < tr.draw_list->num_trans_entities; i++ )
	{
		RI.currententity = tr.draw_list->trans_entities[i];
		RI.currentmodel = RI.currententity->model;
	
		ASSERT( RI.currententity != NULL );
		ASSERT( RI.currententity->model != NULL );

		switch( RI.currentmodel->type )
		{
		case mod_brush:
			R_FindBmodelMirrors( RI.currententity, false );
			break;
		}
	}
}

/*
================
R_FindMirrors

Build mirror chains for this frame
================
*/
void R_FindMirrors( void )
{
	if( !FBitSet( tr.world->flags, FWORLD_HAS_MIRRORS ) || RI.drawOrtho || !RI.drawWorld || RI.onlyClientDraw || !WORLDMODEL )
		return;

	// NOTE: we already has initial params at this point like vieworg, viewangles
	// all other will be sets into R_SetupFrustum
	R_FindViewLeaf ();

	// player is outside world. Don't update mirrors for speedup reasons
	if(( RI.viewleaf - WORLDMODEL->leafs - 1 ) == -1 )
		return;

	R_SetupFrustum ();
	R_MarkLeaves ();

	VectorCopy( RI.cullorigin, tr.modelorg );
	RI.currententity = tr.entities;
	RI.currentmodel = RI.currententity->model;

	R_RecursiveMirrorNode( WORLDMODEL->nodes, RI.frustum.clipFlags );

	R_CheckEntitiesOnList();
}
