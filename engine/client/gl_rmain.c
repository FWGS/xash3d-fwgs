/*
gl_rmain.c - renderer main loop
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
#include "mathlib.h"
#include "library.h"
#include "beamdef.h"
#include "particledef.h"
#include "entity_types.h"

#define IsLiquidContents( cnt )	( cnt == CONTENTS_WATER || cnt == CONTENTS_SLIME || cnt == CONTENTS_LAVA )

float		gldepthmin, gldepthmax;
ref_instance_t	RI;

static int R_RankForRenderMode( int rendermode )
{
	switch( rendermode )
	{
	case kRenderTransTexture:
		return 1;	// draw second
	case kRenderTransAdd:
		return 2;	// draw third
	case kRenderGlow:
		return 3;	// must be last!
	}
	return 0;
}

void R_AllowFog( int allowed )
{
	if( allowed )
	{
		if( glState.isFogEnabled )
			pglEnable( GL_FOG );
	}
	else
	{
		if( glState.isFogEnabled )
			pglDisable( GL_FOG );
	}
}

/*
===============
R_OpaqueEntity

Opaque entity can be brush or studio model but sprite
===============
*/
static qboolean R_OpaqueEntity( cl_entity_t *ent )
{
	if( R_GetEntityRenderMode( ent ) == kRenderNormal )
		return true;
	return false;
}

/*
===============
R_TransEntityCompare

Sorting translucent entities by rendermode then by distance
===============
*/
static int R_TransEntityCompare( const cl_entity_t **a, const cl_entity_t **b )
{
	cl_entity_t	*ent1, *ent2;
	vec3_t		vecLen, org;
	float		dist1, dist2;
	int		rendermode1;
	int		rendermode2;

	ent1 = (cl_entity_t *)*a;
	ent2 = (cl_entity_t *)*b;
	rendermode1 = R_GetEntityRenderMode( ent1 );
	rendermode2 = R_GetEntityRenderMode( ent2 );

	// sort by distance
	if( ent1->model->type != mod_brush || rendermode1 != kRenderTransAlpha )
	{
		VectorAverage( ent1->model->mins, ent1->model->maxs, org );
		VectorAdd( ent1->origin, org, org );
		VectorSubtract( RI.vieworg, org, vecLen );
		dist1 = DotProduct( vecLen, vecLen );
	}
	else dist1 = 1000000000;

	if( ent2->model->type != mod_brush || rendermode2 != kRenderTransAlpha )
	{
		VectorAverage( ent2->model->mins, ent2->model->maxs, org );
		VectorAdd( ent2->origin, org, org );
		VectorSubtract( RI.vieworg, org, vecLen );
		dist2 = DotProduct( vecLen, vecLen );
	}
	else dist2 = 1000000000;

	if( dist1 > dist2 )
		return -1;
	if( dist1 < dist2 )
		return 1;

	// then sort by rendermode
	if( R_RankForRenderMode( rendermode1 ) > R_RankForRenderMode( rendermode2 ))
		return 1;
	if( R_RankForRenderMode( rendermode1 ) < R_RankForRenderMode( rendermode2 ))
		return -1;

	return 0;
}

/*
===============
R_WorldToScreen

Convert a given point from world into screen space
Returns true if we behind to screen
===============
*/
qboolean R_WorldToScreen( const vec3_t point, vec3_t screen )
{
	matrix4x4	worldToScreen;
	qboolean	behind;
	float	w;

	if( !point || !screen )
		return true;

	Matrix4x4_Copy( worldToScreen, RI.worldviewProjectionMatrix );
	screen[0] = worldToScreen[0][0] * point[0] + worldToScreen[0][1] * point[1] + worldToScreen[0][2] * point[2] + worldToScreen[0][3];
	screen[1] = worldToScreen[1][0] * point[0] + worldToScreen[1][1] * point[1] + worldToScreen[1][2] * point[2] + worldToScreen[1][3];
	w = worldToScreen[3][0] * point[0] + worldToScreen[3][1] * point[1] + worldToScreen[3][2] * point[2] + worldToScreen[3][3];
	screen[2] = 0.0f; // just so we have something valid here

	if( w < 0.001f )
	{
		screen[0] *= 100000;
		screen[1] *= 100000;
		behind = true;
	}
	else
	{
		float invw = 1.0f / w;
		screen[0] *= invw;
		screen[1] *= invw;
		behind = false;
	}

	return behind;
}

/*
===============
R_ScreenToWorld

Convert a given point from screen into world space
===============
*/
void R_ScreenToWorld( const vec3_t screen, vec3_t point )
{
	matrix4x4	screenToWorld;
	float	w;

	if( !point || !screen )
		return;

	Matrix4x4_Invert_Full( screenToWorld, RI.worldviewProjectionMatrix );

	point[0] = screen[0] * screenToWorld[0][0] + screen[1] * screenToWorld[0][1] + screen[2] * screenToWorld[0][2] + screenToWorld[0][3];
	point[1] = screen[0] * screenToWorld[1][0] + screen[1] * screenToWorld[1][1] + screen[2] * screenToWorld[1][2] + screenToWorld[1][3];
	point[2] = screen[0] * screenToWorld[2][0] + screen[1] * screenToWorld[2][1] + screen[2] * screenToWorld[2][2] + screenToWorld[2][3];
	w = screen[0] * screenToWorld[3][0] + screen[1] * screenToWorld[3][1] + screen[2] * screenToWorld[3][2] + screenToWorld[3][3];
	if( w != 0.0f ) VectorScale( point, ( 1.0f / w ), point );
}

/*
===============
R_PushScene
===============
*/
void R_PushScene( void )
{
	if( ++tr.draw_stack_pos >= MAX_DRAW_STACK )
		Host_Error( "draw stack overflow\n" );

	tr.draw_list = &tr.draw_stack[tr.draw_stack_pos];
}

/*
===============
R_PushScene
===============
*/
void R_PopScene( void )
{
	if( --tr.draw_stack_pos < 0 )
		Host_Error( "draw stack underflow\n" );
	tr.draw_list = &tr.draw_stack[tr.draw_stack_pos];
}

/*
===============
R_ClearScene
===============
*/
void R_ClearScene( void )
{
	tr.draw_list->num_solid_entities = 0;
	tr.draw_list->num_trans_entities = 0;
	tr.draw_list->num_beam_entities = 0;
}

/*
===============
R_AddEntity
===============
*/
qboolean R_AddEntity( struct cl_entity_s *clent, int type )
{
	if( !r_drawentities->value )
		return false; // not allow to drawing

	if( !clent || !clent->model )
		return false; // if set to invisible, skip

	if( FBitSet( clent->curstate.effects, EF_NODRAW ))
		return false; // done

	if( !R_ModelOpaque( clent->curstate.rendermode ) && CL_FxBlend( clent ) <= 0 )
		return true; // invisible

	if( type == ET_FRAGMENTED )
		r_stats.c_client_ents++;

	if( R_OpaqueEntity( clent ))
	{
		// opaque
		if( tr.draw_list->num_solid_entities >= MAX_VISIBLE_PACKET )
			return false;

		tr.draw_list->solid_entities[tr.draw_list->num_solid_entities] = clent;
		tr.draw_list->num_solid_entities++;
	}
	else
	{
		// translucent
		if( tr.draw_list->num_trans_entities >= MAX_VISIBLE_PACKET )
			return false;

		tr.draw_list->trans_entities[tr.draw_list->num_trans_entities] = clent;
		tr.draw_list->num_trans_entities++;
	}

	return true;
}

/*
=============
R_Clear
=============
*/
static void R_Clear( int bitMask )
{
	int	bits;

	if( CL_IsDevOverviewMode( ))
		pglClearColor( 0.0f, 1.0f, 0.0f, 1.0f ); // green background (Valve rules)
	else pglClearColor( 0.5f, 0.5f, 0.5f, 1.0f );

	bits = GL_DEPTH_BUFFER_BIT;

	if( glState.stencilEnabled )
		bits |= GL_STENCIL_BUFFER_BIT;

	bits &= bitMask;

	pglClear( bits );

	// change ordering for overview
	if( RI.drawOrtho )
	{
		gldepthmin = 1.0f;
		gldepthmax = 0.0f;
	}
	else
	{
		gldepthmin = 0.0f;
		gldepthmax = 1.0f;
	}

	pglDepthFunc( GL_LEQUAL );
	pglDepthRange( gldepthmin, gldepthmax );
}

//=============================================================================
/*
===============
R_GetFarClip
===============
*/
static float R_GetFarClip( void )
{
	if( cl.worldmodel && RI.drawWorld )
		return clgame.movevars.zmax * 1.73f;
	return 2048.0f;
}

/*
===============
R_SetupFrustum
===============
*/
void R_SetupFrustum( void )
{
	ref_overview_t	*ov = &clgame.overView;

	if( RP_NORMALPASS() && ( cl.local.waterlevel >= 3 ))
	{
		RI.fov_x = atan( tan( DEG2RAD( RI.fov_x ) / 2 ) * ( 0.97 + sin( cl.time * 1.5 ) * 0.03 )) * 2 / (M_PI / 180.0);
		RI.fov_y = atan( tan( DEG2RAD( RI.fov_y ) / 2 ) * ( 1.03 - sin( cl.time * 1.5 ) * 0.03 )) * 2 / (M_PI / 180.0);
	}

	// build the transformation matrix for the given view angles
	AngleVectors( RI.viewangles, RI.vforward, RI.vright, RI.vup );

	if( !r_lockfrustum->value )
	{
		VectorCopy( RI.vieworg, RI.cullorigin );
		VectorCopy( RI.vforward, RI.cull_vforward );
		VectorCopy( RI.vright, RI.cull_vright );
		VectorCopy( RI.vup, RI.cull_vup );
	}

	if( RI.drawOrtho )
		GL_FrustumInitOrtho( &RI.frustum, ov->xLeft, ov->xRight, ov->yTop, ov->yBottom, ov->zNear, ov->zFar );
	else GL_FrustumInitProj( &RI.frustum, 0.0f, R_GetFarClip(), RI.fov_x, RI.fov_y ); // NOTE: we ignore nearplane here (mirrors only)
}

/*
=============
R_SetupProjectionMatrix
=============
*/
static void R_SetupProjectionMatrix( matrix4x4 m )
{
	GLdouble	xMin, xMax, yMin, yMax, zNear, zFar;

	if( RI.drawOrtho )
	{
		ref_overview_t *ov = &clgame.overView;
		Matrix4x4_CreateOrtho( m, ov->xLeft, ov->xRight, ov->yTop, ov->yBottom, ov->zNear, ov->zFar );
		return;
	}

	RI.farClip = R_GetFarClip();

	zNear = 4.0f;
	zFar = max( 256.0f, RI.farClip );

	yMax = zNear * tan( RI.fov_y * M_PI / 360.0 );
	yMin = -yMax;

	xMax = zNear * tan( RI.fov_x * M_PI / 360.0 );
	xMin = -xMax;

	Matrix4x4_CreateProjection( m, xMax, xMin, yMax, yMin, zNear, zFar );
}

/*
=============
R_SetupModelviewMatrix
=============
*/
static void R_SetupModelviewMatrix( matrix4x4 m )
{
	Matrix4x4_CreateModelview( m );
	Matrix4x4_ConcatRotate( m, -RI.viewangles[2], 1, 0, 0 );
	Matrix4x4_ConcatRotate( m, -RI.viewangles[0], 0, 1, 0 );
	Matrix4x4_ConcatRotate( m, -RI.viewangles[1], 0, 0, 1 );
	Matrix4x4_ConcatTranslate( m, -RI.vieworg[0], -RI.vieworg[1], -RI.vieworg[2] );
}

/*
=============
R_LoadIdentity
=============
*/
void R_LoadIdentity( void )
{
	if( tr.modelviewIdentity ) return;

	Matrix4x4_LoadIdentity( RI.objectMatrix );
	Matrix4x4_Copy( RI.modelviewMatrix, RI.worldviewMatrix );

	pglMatrixMode( GL_MODELVIEW );
	GL_LoadMatrix( RI.modelviewMatrix );
	tr.modelviewIdentity = true;
}

/*
=============
R_RotateForEntity
=============
*/
void R_RotateForEntity( cl_entity_t *e )
{
	float	scale = 1.0f;

	if( e == clgame.entities )
	{
		R_LoadIdentity();
		return;
	}

	if( e->model->type != mod_brush && e->curstate.scale > 0.0f )
		scale = e->curstate.scale;

	Matrix4x4_CreateFromEntity( RI.objectMatrix, e->angles, e->origin, scale );
	Matrix4x4_ConcatTransforms( RI.modelviewMatrix, RI.worldviewMatrix, RI.objectMatrix );

	pglMatrixMode( GL_MODELVIEW );
	GL_LoadMatrix( RI.modelviewMatrix );
	tr.modelviewIdentity = false;
}

/*
=============
R_TranslateForEntity
=============
*/
void R_TranslateForEntity( cl_entity_t *e )
{
	float	scale = 1.0f;

	if( e == clgame.entities )
	{
		R_LoadIdentity();
		return;
	}

	if( e->model->type != mod_brush && e->curstate.scale > 0.0f )
		scale = e->curstate.scale;

	Matrix4x4_CreateFromEntity( RI.objectMatrix, vec3_origin, e->origin, scale );
	Matrix4x4_ConcatTransforms( RI.modelviewMatrix, RI.worldviewMatrix, RI.objectMatrix );

	pglMatrixMode( GL_MODELVIEW );
	GL_LoadMatrix( RI.modelviewMatrix );
	tr.modelviewIdentity = false;
}

/*
===============
R_FindViewLeaf
===============
*/
void R_FindViewLeaf( void )
{
	RI.oldviewleaf = RI.viewleaf;
	RI.viewleaf = Mod_PointInLeaf( RI.pvsorigin, cl.worldmodel->nodes );
}

/*
===============
R_SetupFrame
===============
*/
static void R_SetupFrame( void )
{
	// setup viewplane dist
	RI.viewplanedist = DotProduct( RI.vieworg, RI.vforward );

	// NOTE: this request is the fps-killer on some NVidia drivers
	glState.isFogEnabled = pglIsEnabled( GL_FOG );

	if( !gl_nosort->value )
	{
		// sort translucents entities by rendermode and distance
		qsort( tr.draw_list->trans_entities, tr.draw_list->num_trans_entities, sizeof( cl_entity_t* ), R_TransEntityCompare );
	}

	// current viewleaf
	if( RI.drawWorld )
	{
		RI.isSkyVisible = false; // unknown at this moment
		R_FindViewLeaf();
	}
}

/*
=============
R_SetupGL
=============
*/
void R_SetupGL( qboolean set_gl_state )
{
	R_SetupModelviewMatrix( RI.worldviewMatrix );
	R_SetupProjectionMatrix( RI.projectionMatrix );

	Matrix4x4_Concat( RI.worldviewProjectionMatrix, RI.projectionMatrix, RI.worldviewMatrix );

	if( !set_gl_state ) return;

	if( RP_NORMALPASS( ))
	{
		int	x, x2, y, y2;

		// set up viewport (main, playersetup)
		x = floor( RI.viewport[0] * glState.width / glState.width );
		x2 = ceil(( RI.viewport[0] + RI.viewport[2] ) * glState.width / glState.width );
		y = floor( glState.height - RI.viewport[1] * glState.height / glState.height );
		y2 = ceil( glState.height - ( RI.viewport[1] + RI.viewport[3] ) * glState.height / glState.height );

		pglViewport( x, y2, x2 - x, y - y2 );
	}
	else
	{
		// envpass, mirrorpass
		pglViewport( RI.viewport[0], RI.viewport[1], RI.viewport[2], RI.viewport[3] );
	}

	pglMatrixMode( GL_PROJECTION );
	GL_LoadMatrix( RI.projectionMatrix );

	pglMatrixMode( GL_MODELVIEW );
	GL_LoadMatrix( RI.worldviewMatrix );

	if( FBitSet( RI.params, RP_CLIPPLANE ))
	{
		GLdouble	clip[4];
		mplane_t	*p = &RI.clipPlane;

		clip[0] = p->normal[0];
		clip[1] = p->normal[1];
		clip[2] = p->normal[2];
		clip[3] = -p->dist;

		pglClipPlane( GL_CLIP_PLANE0, clip );
		pglEnable( GL_CLIP_PLANE0 );
	}

	GL_Cull( GL_FRONT );

	pglDisable( GL_BLEND );
	pglDisable( GL_ALPHA_TEST );
	pglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
}

/*
=============
R_EndGL
=============
*/
static void R_EndGL( void )
{
	if( RI.params & RP_CLIPPLANE )
		pglDisable( GL_CLIP_PLANE0 );
}

/*
=============
R_RecursiveFindWaterTexture

using to find source waterleaf with
watertexture to grab fog values from it
=============
*/
static gl_texture_t *R_RecursiveFindWaterTexture( const mnode_t *node, const mnode_t *ignore, qboolean down )
{
	gl_texture_t *tex = NULL;

	// assure the initial node is not null
	// we could check it here, but we would rather check it 
	// outside the call to get rid of one additional recursion level
	Assert( node != NULL );

	// ignore solid nodes
	if( node->contents == CONTENTS_SOLID )
		return NULL;

	if( node->contents < 0 )
	{
		mleaf_t		*pleaf;
		msurface_t	**mark;
		int		i, c;

		// ignore non-liquid leaves
		if( node->contents != CONTENTS_WATER && node->contents != CONTENTS_LAVA && node->contents != CONTENTS_SLIME )
			 return NULL;

		// find texture
		pleaf = (mleaf_t *)node;
		mark = pleaf->firstmarksurface;
		c = pleaf->nummarksurfaces;	

		for( i = 0; i < c; i++, mark++ )
		{
			if( (*mark)->flags & SURF_DRAWTURB && (*mark)->texinfo && (*mark)->texinfo->texture )
				return R_GetTexture( (*mark)->texinfo->texture->gl_texturenum );
		}

		// texture not found
		return NULL;
	}

	// this is a regular node
	// traverse children
	if( node->children[0] && ( node->children[0] != ignore ))
	{
		tex = R_RecursiveFindWaterTexture( node->children[0], node, true );
		if( tex ) return tex;
	}

	if( node->children[1] && ( node->children[1] != ignore ))
	{
		tex = R_RecursiveFindWaterTexture( node->children[1], node, true );
		if( tex )	return tex;
	}

	// for down recursion, return immediately
	if( down ) return NULL;

	// texture not found, step up if any
	if( node->parent )
		return R_RecursiveFindWaterTexture( node->parent, node, false );

	// top-level node, bail out
	return NULL;
}

/*
=============
R_CheckFog

check for underwater fog
Using backward recursion to find waterline leaf
from underwater leaf (idea: XaeroX)
=============
*/
static void R_CheckFog( void )
{
	cl_entity_t	*ent;
	gl_texture_t	*tex;
	int		i, cnt, count;

	// quake global fog
	if( CL_IsQuakeCompatible( ))
	{
		if( !clgame.movevars.fog_settings )
		{
			if( pglIsEnabled( GL_FOG ))
				pglDisable( GL_FOG );
			RI.fogEnabled = false;
			return;
		}

		// quake-style global fog
		RI.fogColor[0] = ((clgame.movevars.fog_settings & 0xFF000000) >> 24) / 255.0f;
		RI.fogColor[1] = ((clgame.movevars.fog_settings & 0xFF0000) >> 16) / 255.0f;
		RI.fogColor[2] = ((clgame.movevars.fog_settings & 0xFF00) >> 8) / 255.0f;
		RI.fogDensity = ((clgame.movevars.fog_settings & 0xFF) / 255.0f) * 0.01f;
		RI.fogStart = RI.fogEnd = 0.0f;
		RI.fogColor[3] = 1.0f;
		RI.fogCustom = false;
		RI.fogEnabled = true;
		RI.fogSkybox = true;
		return;
	}

	RI.fogEnabled = false;

	if( RI.onlyClientDraw || cl.local.waterlevel < 3 || !RI.drawWorld || !RI.viewleaf )
	{
		if( RI.cached_waterlevel == 3 )
                    {
			// in some cases waterlevel jumps from 3 to 1. Catch it
			RI.cached_waterlevel = cl.local.waterlevel;
			RI.cached_contents = CONTENTS_EMPTY;
			if( !RI.fogCustom )
			{
				glState.isFogEnabled = false;
				pglDisable( GL_FOG );
			}
		}
		return;
	}

	ent = CL_GetWaterEntity( RI.vieworg );
	if( ent && ent->model && ent->model->type == mod_brush && ent->curstate.skin < 0 )
		cnt = ent->curstate.skin;
	else cnt = RI.viewleaf->contents;

	RI.cached_waterlevel = cl.local.waterlevel;

	if( !IsLiquidContents( RI.cached_contents ) && IsLiquidContents( cnt ))
	{
		tex = NULL;

		// check for water texture
		if( ent && ent->model && ent->model->type == mod_brush )
		{
			msurface_t	*surf;
	
			count = ent->model->nummodelsurfaces;

			for( i = 0, surf = &ent->model->surfaces[ent->model->firstmodelsurface]; i < count; i++, surf++ )
			{
				if( surf->flags & SURF_DRAWTURB && surf->texinfo && surf->texinfo->texture )
				{
					tex = R_GetTexture( surf->texinfo->texture->gl_texturenum );
					RI.cached_contents = ent->curstate.skin;
					break;
				}
			}
		}
		else
		{
			tex = R_RecursiveFindWaterTexture( RI.viewleaf->parent, NULL, false );
			if( tex ) RI.cached_contents = RI.viewleaf->contents;
		}

		if( !tex ) return;	// no valid fogs

		// copy fog params
		RI.fogColor[0] = tex->fogParams[0] / 255.0f;
		RI.fogColor[1] = tex->fogParams[1] / 255.0f;
		RI.fogColor[2] = tex->fogParams[2] / 255.0f;
		RI.fogDensity = tex->fogParams[3] * 0.000025f;
		RI.fogStart = RI.fogEnd = 0.0f;
		RI.fogColor[3] = 1.0f;
		RI.fogCustom = false;
		RI.fogEnabled = true;
		RI.fogSkybox = true;
	}
	else
	{
		RI.fogCustom = false;
		RI.fogEnabled = true;
		RI.fogSkybox = true;
	}
}

/*
=============
R_CheckGLFog

special condition for Spirit 1.9
that used direct calls of glFog-functions
=============
*/
static void R_CheckGLFog( void )
{
#ifdef HACKS_RELATED_HLMODS
	if(( !RI.fogEnabled && !RI.fogCustom ) && pglIsEnabled( GL_FOG ) && VectorIsNull( RI.fogColor ))
	{
		// fill the fog color from GL-state machine
		pglGetFloatv( GL_FOG_COLOR, RI.fogColor );
		RI.fogSkybox = true;
	}
#endif
}

/*
=============
R_DrawFog

=============
*/
void R_DrawFog( void )
{
	if( !RI.fogEnabled ) return;

	pglEnable( GL_FOG );
	if( CL_IsQuakeCompatible( ))
		pglFogi( GL_FOG_MODE, GL_EXP2 );
	else pglFogi( GL_FOG_MODE, GL_EXP );
	pglFogf( GL_FOG_DENSITY, RI.fogDensity );
	pglFogfv( GL_FOG_COLOR, RI.fogColor );
	pglHint( GL_FOG_HINT, GL_NICEST );
}

/*
=============
R_DrawEntitiesOnList
=============
*/
void R_DrawEntitiesOnList( void )
{
	int	i;

	tr.blend = 1.0f;
	GL_CheckForErrors();

	// first draw solid entities
	for( i = 0; i < tr.draw_list->num_solid_entities && !RI.onlyClientDraw; i++ )
	{
		RI.currententity = tr.draw_list->solid_entities[i];
		RI.currentmodel = RI.currententity->model;

		Assert( RI.currententity != NULL );
		Assert( RI.currentmodel != NULL );

		switch( RI.currentmodel->type )
		{
		case mod_brush:
			R_DrawBrushModel( RI.currententity );
			break;
		case mod_alias:
			R_DrawAliasModel( RI.currententity );
			break;
		case mod_studio:
			R_DrawStudioModel( RI.currententity );
			break;
		default:
			break;
		}
	}

	GL_CheckForErrors();

	// quake-specific feature
	R_DrawAlphaTextureChains();

	GL_CheckForErrors();

	// draw sprites seperately, because of alpha blending
	for( i = 0; i < tr.draw_list->num_solid_entities && !RI.onlyClientDraw; i++ )
	{
		RI.currententity = tr.draw_list->solid_entities[i];
		RI.currentmodel = RI.currententity->model;

		Assert( RI.currententity != NULL );
		Assert( RI.currentmodel != NULL );

		switch( RI.currentmodel->type )
		{
		case mod_sprite:
			R_DrawSpriteModel( RI.currententity );
			break;
		}
	}

	GL_CheckForErrors();

	if( !RI.onlyClientDraw )
          {
		CL_DrawBeams( false );
	}

	GL_CheckForErrors();

	if( RI.drawWorld )
		clgame.dllFuncs.pfnDrawNormalTriangles();

	GL_CheckForErrors();

	// then draw translucent entities
	for( i = 0; i < tr.draw_list->num_trans_entities && !RI.onlyClientDraw; i++ )
	{
		RI.currententity = tr.draw_list->trans_entities[i];
		RI.currentmodel = RI.currententity->model;

		// handle studiomodels with custom rendermodes on texture
		if( RI.currententity->curstate.rendermode != kRenderNormal )
			tr.blend = CL_FxBlend( RI.currententity ) / 255.0f;
		else tr.blend = 1.0f; // draw as solid but sorted by distance

		if( tr.blend <= 0.0f ) continue;
	
		Assert( RI.currententity != NULL );
		Assert( RI.currentmodel != NULL );

		switch( RI.currentmodel->type )
		{
		case mod_brush:
			R_DrawBrushModel( RI.currententity );
			break;
		case mod_alias:
			R_DrawAliasModel( RI.currententity );
			break;
		case mod_studio:
			R_DrawStudioModel( RI.currententity );
			break;
		case mod_sprite:
			R_DrawSpriteModel( RI.currententity );
			break;
		default:
			break;
		}
	}

	GL_CheckForErrors();

	if( RI.drawWorld )
	{
		pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
		clgame.dllFuncs.pfnDrawTransparentTriangles ();
	}

	GL_CheckForErrors();

	if( !RI.onlyClientDraw )
	{
		R_AllowFog( false );
		CL_DrawBeams( true );
		CL_DrawParticles( tr.frametime );
		CL_DrawTracers( tr.frametime );
		R_AllowFog( true );
	}

	GL_CheckForErrors();

	pglDisable( GL_BLEND );	// Trinity Render issues

	if( !RI.onlyClientDraw )
		R_DrawViewModel();
	CL_ExtraUpdate();

	GL_CheckForErrors();
}

/*
================
R_RenderScene

R_SetupRefParams must be called right before
================
*/
void R_RenderScene( void )
{
	if( !cl.worldmodel && RI.drawWorld )
		Host_Error( "R_RenderView: NULL worldmodel\n" );

	// frametime is valid only for normal pass
	if( RP_NORMALPASS( ))
		tr.frametime = cl.time - cl.oldtime;
	else tr.frametime = 0.0;

	// begin a new frame
	tr.framecount++;

	R_PushDlights();

	R_SetupFrustum();
	R_SetupFrame();
	R_SetupGL( true );
	R_Clear( ~0 );

	R_MarkLeaves();
	R_DrawFog ();

	R_CheckGLFog();	
	R_DrawWorld();
	R_CheckFog();

	CL_ExtraUpdate ();	// don't let sound get messed up if going slow

	R_DrawEntitiesOnList();

	R_DrawWaterSurfaces();

	R_EndGL();
}

/*
===============
R_DoResetGamma

gamma will be reset for
some type of screenshots
===============
*/
qboolean R_DoResetGamma( void )
{
	switch( cls.scrshot_action )
	{
	case scrshot_envshot:
	case scrshot_skyshot:
		return true;
	default:
		return false;
	}
}

/*
===============
R_CheckGamma
===============
*/
void R_CheckGamma( void )
{
	if( R_DoResetGamma( ))
	{
		// paranoia cubemaps uses this
		BuildGammaTable( 1.8f, 0.0f );

		// paranoia cubemap rendering
		if( clgame.drawFuncs.GL_BuildLightmaps )
			clgame.drawFuncs.GL_BuildLightmaps( );
	}
	else if( FBitSet( vid_gamma->flags, FCVAR_CHANGED ) || FBitSet( vid_brightness->flags, FCVAR_CHANGED ))
	{
		BuildGammaTable( vid_gamma->value, vid_brightness->value );
		glConfig.softwareGammaUpdate = true;
		GL_RebuildLightmaps();
		glConfig.softwareGammaUpdate = false;
	}
}

/*
===============
R_BeginFrame
===============
*/
void R_BeginFrame( qboolean clearScene )
{
	glConfig.softwareGammaUpdate = false;	// in case of possible fails

	if(( gl_clear->value || CL_IsDevOverviewMode( )) && clearScene && cls.state != ca_cinematic )
	{
		pglClear( GL_COLOR_BUFFER_BIT );
	}

	R_CheckGamma();

	R_Set2DMode( true );

	// draw buffer stuff
	pglDrawBuffer( GL_BACK );

	// update texture parameters
	if( FBitSet( gl_texture_nearest->flags|gl_lightmap_nearest->flags|gl_texture_anisotropy->flags|gl_texture_lodbias->flags, FCVAR_CHANGED ))
		R_SetTextureParameters();

	// swapinterval stuff
	GL_UpdateSwapInterval();

	CL_ExtraUpdate ();
}

/*
===============
R_SetupRefParams

set initial params for renderer
===============
*/
void R_SetupRefParams( const ref_viewpass_t *rvp )
{
	RI.params = RP_NONE;
	RI.drawWorld = FBitSet( rvp->flags, RF_DRAW_WORLD );
	RI.onlyClientDraw = FBitSet( rvp->flags, RF_ONLY_CLIENTDRAW );
	RI.farClip = 0;

	if( !FBitSet( rvp->flags, RF_DRAW_CUBEMAP ))
	{
		RI.drawOrtho = FBitSet( rvp->flags, RF_DRAW_OVERVIEW );
	}
	else 
	{
		SetBits( RI.params, RP_ENVVIEW );
		RI.drawOrtho = false;
	}

	// setup viewport
	RI.viewport[0] = rvp->viewport[0];
	RI.viewport[1] = rvp->viewport[1];
	RI.viewport[2] = rvp->viewport[2];
	RI.viewport[3] = rvp->viewport[3];

	// calc FOV
	RI.fov_x = rvp->fov_x;
	RI.fov_y = rvp->fov_y;

	VectorCopy( rvp->vieworigin, RI.vieworg );
	VectorCopy( rvp->viewangles, RI.viewangles );
	VectorCopy( rvp->vieworigin, RI.pvsorigin );
}

/*
===============
R_RenderFrame
===============
*/
void R_RenderFrame( const ref_viewpass_t *rvp )
{
	if( r_norefresh->value )
		return;

	// setup the initial render params
	R_SetupRefParams( rvp );

	if( gl_finish->value && RI.drawWorld )
		pglFinish();

	if( glConfig.max_multisamples > 1 && FBitSet( gl_msaa->flags, FCVAR_CHANGED ))
	{
		if( CVAR_TO_BOOL( gl_msaa ))
			pglEnable( GL_MULTISAMPLE_ARB );
		else pglDisable( GL_MULTISAMPLE_ARB );
		ClearBits( gl_msaa->flags, FCVAR_CHANGED );
	}

	// completely override rendering
	if( clgame.drawFuncs.GL_RenderFrame != NULL )
	{
		tr.fCustomRendering = true;

		if( clgame.drawFuncs.GL_RenderFrame( rvp ))
		{
			R_GatherPlayerLight();
			tr.realframecount++;
			tr.fResetVis = true;
			return;
		}
	}

	tr.fCustomRendering = false;
	if( !RI.onlyClientDraw )
		R_RunViewmodelEvents();

	tr.realframecount++; // right called after viewmodel events
	R_RenderScene();
}

/*
===============
R_EndFrame
===============
*/
void R_EndFrame( void )
{
	// flush any remaining 2D bits
	R_Set2DMode( false );

	if( !pwglSwapBuffers( glw_state.hDC ))
		Sys_Error( "failed to swap buffers\nCheck your video driver and as possible of reinstall it" );
}

/*
===============
R_DrawCubemapView
===============
*/
void R_DrawCubemapView( const vec3_t origin, const vec3_t angles, int size )
{
	ref_viewpass_t rvp;

	// basic params
	rvp.flags = rvp.viewentity = 0;
	SetBits( rvp.flags, RF_DRAW_WORLD );
	SetBits( rvp.flags, RF_DRAW_CUBEMAP );

	rvp.viewport[0] = rvp.viewport[1] = 0;
	rvp.viewport[2] = rvp.viewport[3] = size;
	rvp.fov_x = rvp.fov_y = 90.0f; // this is a final fov value

	// setup origin & angles
	VectorCopy( origin, rvp.vieworigin );
	VectorCopy( angles, rvp.viewangles );

	R_RenderFrame( &rvp );

	RI.viewleaf = NULL;		// force markleafs next frame
}

static int GL_RenderGetParm( int parm, int arg )
{
	gl_texture_t *glt;

	switch( parm )
	{
	case PARM_TEX_WIDTH:
		glt = R_GetTexture( arg );
		return glt->width;
	case PARM_TEX_HEIGHT:
		glt = R_GetTexture( arg );
		return glt->height;
	case PARM_TEX_SRC_WIDTH:
		glt = R_GetTexture( arg );
		return glt->srcWidth;
	case PARM_TEX_SRC_HEIGHT:
		glt = R_GetTexture( arg );
		return glt->srcHeight;
	case PARM_TEX_GLFORMAT:
		glt = R_GetTexture( arg );
		return glt->format;
	case PARM_TEX_ENCODE:
		glt = R_GetTexture( arg );
		return glt->encode;
	case PARM_TEX_MIPCOUNT:
		glt = R_GetTexture( arg );
		return glt->numMips;
	case PARM_TEX_DEPTH:
		glt = R_GetTexture( arg );
		return glt->depth;
	case PARM_BSP2_SUPPORTED:
#ifdef SUPPORT_BSP2_FORMAT
		return 1;
#endif
		return 0;
	case PARM_TEX_SKYBOX:
		Assert( arg >= 0 && arg < 6 );
		return tr.skyboxTextures[arg];
	case PARM_TEX_SKYTEXNUM:
		return tr.skytexturenum;
	case PARM_TEX_LIGHTMAP:
		arg = bound( 0, arg, MAX_LIGHTMAPS - 1 );
		return tr.lightmapTextures[arg];
	case PARM_SKY_SPHERE:
		return FBitSet( world.flags, FWORLD_SKYSPHERE ) && !FBitSet( world.flags, FWORLD_CUSTOM_SKYBOX );
	case PARAM_GAMEPAUSED:
		return cl.paused;
	case PARM_WIDESCREEN:
		return glState.wideScreen;
	case PARM_FULLSCREEN:
		return glState.fullScreen;
	case PARM_SCREEN_WIDTH:
		return glState.width;
	case PARM_SCREEN_HEIGHT:
		return glState.height;
	case PARM_CLIENT_INGAME:
		return CL_IsInGame();
	case PARM_MAX_ENTITIES:
		return clgame.maxEntities;
	case PARM_TEX_TARGET:
		glt = R_GetTexture( arg );
		return glt->target;
	case PARM_TEX_TEXNUM:
		glt = R_GetTexture( arg );
		return glt->texnum;
	case PARM_TEX_FLAGS:
		glt = R_GetTexture( arg );
		return glt->flags;
	case PARM_FEATURES:
		return host.features;
	case PARM_ACTIVE_TMU:
		return glState.activeTMU;
	case PARM_LIGHTSTYLEVALUE:
		arg = bound( 0, arg, MAX_LIGHTSTYLES - 1 );
		return tr.lightstylevalue[arg];
	case PARM_MAP_HAS_DELUXE:
		return FBitSet( world.flags, FWORLD_HAS_DELUXEMAP );
	case PARM_MAX_IMAGE_UNITS:
		return GL_MaxTextureUnits();
	case PARM_CLIENT_ACTIVE:
		return (cls.state == ca_active);
	case PARM_REBUILD_GAMMA:
		return glConfig.softwareGammaUpdate;
	case PARM_DEDICATED_SERVER:
		return (host.type == HOST_DEDICATED);
	case PARM_SURF_SAMPLESIZE:
		if( arg >= 0 && arg < cl.worldmodel->numsurfaces )
			return Mod_SampleSizeForFace( &cl.worldmodel->surfaces[arg] );
		return LM_SAMPLE_SIZE;
	case PARM_GL_CONTEXT_TYPE:
		return glConfig.context;
	case PARM_GLES_WRAPPER:
		return glConfig.wrapper;
	case PARM_STENCIL_ACTIVE:
		return glState.stencilEnabled;
	case PARM_WATER_ALPHA:
		return FBitSet( world.flags, FWORLD_WATERALPHA );
	case PARM_TEX_MEMORY:
		return GL_TexMemory();
	case PARM_DELUXEDATA:
		return *(int *)&world.deluxedata;
	case PARM_SHADOWDATA:
		return *(int *)&world.shadowdata;
	}
	return 0;
}

static void R_GetDetailScaleForTexture( int texture, float *xScale, float *yScale )
{
	gl_texture_t *glt = R_GetTexture( texture );

	if( xScale ) *xScale = glt->xscale;
	if( yScale ) *yScale = glt->yscale;
}

static void R_GetExtraParmsForTexture( int texture, byte *red, byte *green, byte *blue, byte *density )
{
	gl_texture_t *glt = R_GetTexture( texture );

	if( red ) *red = glt->fogParams[0];
	if( green ) *green = glt->fogParams[1];
	if( blue ) *blue = glt->fogParams[2];
	if( density ) *density = glt->fogParams[3];
}

/*
=================
R_EnvShot

=================
*/
static void R_EnvShot( const float *vieworg, const char *name, int skyshot, int shotsize )
{
	static vec3_t viewPoint;

	if( !COM_CheckString( name ))
		return; 

	if( cls.scrshot_action != scrshot_inactive )
	{
		if( cls.scrshot_action != scrshot_skyshot && cls.scrshot_action != scrshot_envshot )
			Con_Printf( S_ERROR "R_%sShot: subsystem is busy, try for next frame.\n", skyshot ? "Sky" : "Env" );
		return;
	}

	cls.envshot_vieworg = NULL; // use client view
	Q_strncpy( cls.shotname, name, sizeof( cls.shotname ));

	if( vieworg )
	{
		// make sure what viewpoint don't temporare
		VectorCopy( vieworg, viewPoint );
		cls.envshot_vieworg = viewPoint;
		cls.envshot_disable_vis = true;
	}

	// make request for envshot
	if( skyshot ) cls.scrshot_action = scrshot_skyshot;
	else cls.scrshot_action = scrshot_envshot;

	// catch negative values
	cls.envshot_viewsize = max( 0, shotsize );
}

static void R_SetCurrentEntity( cl_entity_t *ent )
{
	RI.currententity = ent;

	// set model also
	if( RI.currententity != NULL )
	{
		RI.currentmodel = RI.currententity->model;
	}
}

static void R_SetCurrentModel( model_t *mod )
{
	RI.currentmodel = mod;
}

static int R_FatPVS( const vec3_t org, float radius, byte *visbuffer, qboolean merge, qboolean fullvis )
{
	return Mod_FatPVS( org, radius, visbuffer, world.visbytes, merge, fullvis );
}

static lightstyle_t *CL_GetLightStyle( int number )
{
	Assert( number >= 0 && number < MAX_LIGHTSTYLES );
	return &cl.lightstyles[number];
}

static dlight_t *CL_GetDynamicLight( int number )
{
	Assert( number >= 0 && number < MAX_DLIGHTS );
	return &cl_dlights[number];
}

static dlight_t *CL_GetEntityLight( int number )
{
	Assert( number >= 0 && number < MAX_ELIGHTS );
	return &cl_elights[number];
}

static float R_GetFrameTime( void )
{
	return tr.frametime;
}

static const char *GL_TextureName( unsigned int texnum )
{
	return R_GetTexture( texnum )->name;	
}

const byte *GL_TextureData( unsigned int texnum )
{
	rgbdata_t *pic = R_GetTexture( texnum )->original;

	if( pic != NULL )
		return pic->buffer;
	return NULL;	
}

static const ref_overview_t *GL_GetOverviewParms( void )
{
	return &clgame.overView;
}

static void *R_Mem_Alloc( size_t cb, const char *filename, const int fileline )
{
	return _Mem_Alloc( cls.mempool, cb, true, filename, fileline );
}

static void R_Mem_Free( void *mem, const char *filename, const int fileline )
{
	if( !mem ) return;
	_Mem_Free( mem, filename, fileline );
}

/*
=========
pfnGetFilesList

=========
*/
static char **pfnGetFilesList( const char *pattern, int *numFiles, int gamedironly )
{
	static search_t	*t = NULL;

	if( t ) Mem_Free( t ); // release prev search

	t = FS_Search( pattern, true, gamedironly );

	if( !t )
	{
		if( numFiles ) *numFiles = 0;
		return NULL;
	}

	if( numFiles ) *numFiles = t->numfilenames;
	return t->filenames;
}

static uint pfnFileBufferCRC32( const void *buffer, const int length )
{
	uint	modelCRC = 0;

	if( !buffer || length <= 0 )
		return modelCRC;

	CRC32_Init( &modelCRC );
	CRC32_ProcessBuffer( &modelCRC, buffer, length );
	return CRC32_Final( modelCRC );
}

/*
=============
CL_GenericHandle

=============
*/
const char *CL_GenericHandle( int fileindex )
{
	if( fileindex < 0 || fileindex >= MAX_CUSTOM )
		return 0;
	return cl.files_precache[fileindex];
}
	
static render_api_t gRenderAPI =
{
	GL_RenderGetParm,
	R_GetDetailScaleForTexture,
	R_GetExtraParmsForTexture,
	CL_GetLightStyle,
	CL_GetDynamicLight,
	CL_GetEntityLight,
	LightToTexGamma,
	R_GetFrameTime,
	R_SetCurrentEntity,
	R_SetCurrentModel,
	R_FatPVS,
	R_StoreEfrags,
	GL_FindTexture,
	GL_TextureName,
	GL_TextureData,
	GL_LoadTexture,
	GL_CreateTexture,
	GL_LoadTextureArray,
	GL_CreateTextureArray,
	GL_FreeTexture,
	DrawSingleDecal,
	R_DecalSetupVerts,
	R_EntityRemoveDecals,
	AVI_LoadVideo,
	AVI_GetVideoInfo,
	AVI_GetVideoFrameNumber,
	AVI_GetVideoFrame,
	R_UploadStretchRaw,
	AVI_FreeVideo,
	AVI_IsActive,
	S_StreamAviSamples,
	NULL,
	NULL,
	GL_Bind,
	GL_SelectTexture,
	GL_LoadTexMatrixExt,
	GL_LoadIdentityTexMatrix,
	GL_CleanUpTextureUnits,
	GL_TexGen,
	GL_TextureTarget,
	GL_SetTexCoordArrayMode,
	GL_GetProcAddress,
	GL_UpdateTexSize,
	NULL,
	NULL,
	CL_DrawParticlesExternal,
	R_EnvShot,
	pfnSPR_LoadExt,
	R_LightVec,
	R_StudioGetTexture,
	GL_GetOverviewParms,
	CL_GenericHandle,
	COM_SaveFile,
	NULL,
	R_Mem_Alloc,
	R_Mem_Free,
	pfnGetFilesList,
	pfnFileBufferCRC32,
	COM_CompareFileTime,
	Host_Error,
	CL_ModelHandle,
	pfnTime,
	Cvar_Set,
	S_FadeMusicVolume,
	COM_SetRandomSeed,
};

/*
===============
R_InitRenderAPI

Initialize client external rendering
===============
*/
qboolean R_InitRenderAPI( void )
{
	// make sure what render functions is cleared
	memset( &clgame.drawFuncs, 0, sizeof( clgame.drawFuncs ));
	glConfig.fCustomRenderer = false;

	if( clgame.dllFuncs.pfnGetRenderInterface )
	{
		if( clgame.dllFuncs.pfnGetRenderInterface( CL_RENDER_INTERFACE_VERSION, &gRenderAPI, &clgame.drawFuncs ))
		{
			Con_Reportf( "CL_LoadProgs: ^2initailized extended RenderAPI ^7ver. %i\n", CL_RENDER_INTERFACE_VERSION );
			glConfig.fCustomRenderer = true;
			return true;
		}

		// make sure what render functions is cleared
		memset( &clgame.drawFuncs, 0, sizeof( clgame.drawFuncs ));

		return false; // just tell user about problems
	}

	// render interface is missed
	return true;
}