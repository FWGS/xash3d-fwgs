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

#include "r_local.h"
#include "xash3d_mathlib.h"
#include "library.h"
// #include "beamdef.h"
// #include "particledef.h"
#include "entity_types.h"
#include "mod_local.h"
int r_cnumsurfs;
#define IsLiquidContents( cnt ) ( cnt == CONTENTS_WATER || cnt == CONTENTS_SLIME || cnt == CONTENTS_LAVA )

ref_instance_t RI;


// quake defines. will be refactored

// view origin
//

//
// screen size info
//
float xcenter, ycenter;
float xscale, yscale;
float xscaleinv, yscaleinv;
// float		xscaleshrink, yscaleshrink;
float aliasxscale, aliasyscale, aliasxcenter, aliasycenter;

int   r_screenwidth;




//
// refresh flags
//

// int             d_spanpixcount;
// int             r_polycount;
// int             r_drawnpolycount;
// int             r_wholepolycount;

int r_viewcluster, r_oldviewcluster;

CVAR_DEFINE_AUTO( sw_clearcolor, "48999", 0, "screen clear color" );
CVAR_DEFINE_AUTO( sw_drawflat, "0", FCVAR_CHEAT, "" );
CVAR_DEFINE_AUTO( sw_draworder, "0", FCVAR_CHEAT, "" );
CVAR_DEFINE_AUTO( sw_maxedges, "32", 0, "" );
static CVAR_DEFINE_AUTO( sw_maxsurfs, "0", 0, "" );
CVAR_DEFINE_AUTO( sw_mipscale, "1", FCVAR_GLCONFIG, "nothing" );
CVAR_DEFINE_AUTO( sw_mipcap, "0", FCVAR_GLCONFIG, "nothing" );
CVAR_DEFINE_AUTO( sw_surfcacheoverride, "0", 0, "" );
static CVAR_DEFINE_AUTO( sw_waterwarp, "1", FCVAR_GLCONFIG, "nothing" );
static CVAR_DEFINE_AUTO( sw_notransbrushes, "0", FCVAR_GLCONFIG, "do not apply transparency to water/glasses (faster)" );
CVAR_DEFINE_AUTO( sw_noalphabrushes, "0", FCVAR_GLCONFIG, "do not draw brush holes (faster)" );
CVAR_DEFINE_AUTO( r_traceglow, "0", FCVAR_GLCONFIG, "cull flares behind models" );
CVAR_DEFINE_AUTO( sw_texfilt, "0", FCVAR_GLCONFIG, "texture dither" );
static CVAR_DEFINE_AUTO( r_novis, "0", 0, "" );


DEFINE_ENGINE_SHARED_CVAR_LIST()

int r_viewcluster, r_oldviewcluster;

float        d_sdivzstepu, d_tdivzstepu, d_zistepu;
float        d_sdivzstepv, d_tdivzstepv, d_zistepv;
float        d_sdivzorigin, d_tdivzorigin, d_ziorigin;

fixed16_t    sadjust, tadjust, bbextents, bbextentt;

pixel_t      *cacheblock;
int          cachewidth;
pixel_t      *d_viewbuffer;
short        *d_pzbuffer;
unsigned int d_zrowbytes;
unsigned int d_zwidth;

mvertex_t    *r_pcurrentvertbase;

// int                     c_surf;
qboolean     r_surfsonstack;
int          r_clipflags;
byte         r_warpbuffer[WARP_WIDTH * WARP_HEIGHT];
int          r_numallocatededges;

float        r_aliasuvscale = 1.0;

static int R_RankForRenderMode( int rendermode )
{
	switch( rendermode )
	{
	case kRenderTransTexture:
		return 1; // draw second
	case kRenderTransAdd:
		return 2; // draw third
	case kRenderGlow:
		return 3; // must be last!
	}
	return 0;
}

void GAME_EXPORT R_AllowFog( qboolean allowed )
{
}

/*
===============
R_OpaqueEntity

Opaque entity can be brush or studio model but sprite
===============
*/
qboolean R_OpaqueEntity( cl_entity_t *ent )
{
	int rendermode = R_GetEntityRenderMode( ent );

	if( rendermode == kRenderNormal )
	{
		switch( ent->curstate.renderfx )
		{
		case kRenderFxNone:
		case kRenderFxDeadPlayer:
		case kRenderFxLightMultiplier:
		case kRenderFxExplode:
			return true;
		}
	}

	if( sw_notransbrushes.value && ent->model && ent->model->type == mod_brush && rendermode == kRenderTransTexture )
		return true;

	if( sw_noalphabrushes.value && ent->model && ent->model->type == mod_brush && rendermode == kRenderTransAlpha )
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
	cl_entity_t *ent1, *ent2;
	vec3_t      vecLen, org;
	float       dist1, dist2;
	int         rendermode1;
	int         rendermode2;

	ent1 = (cl_entity_t *)*a;
	ent2 = (cl_entity_t *)*b;
	rendermode1 = R_GetEntityRenderMode( ent1 );
	rendermode2 = R_GetEntityRenderMode( ent2 );

	// sort by distance
	if(( ent1->model && ent1->model->type != mod_brush ) || rendermode1 != kRenderTransAlpha )
	{
		VectorAverage( ent1->model->mins, ent1->model->maxs, org );
		VectorAdd( ent1->origin, org, org );
		VectorSubtract( RI.vieworg, org, vecLen );
		dist1 = DotProduct( vecLen, vecLen );
	}
	else
		dist1 = 1000000000;

	if(( ent1->model && ent2->model->type != mod_brush ) || rendermode2 != kRenderTransAlpha )
	{
		VectorAverage( ent2->model->mins, ent2->model->maxs, org );
		VectorAdd( ent2->origin, org, org );
		VectorSubtract( RI.vieworg, org, vecLen );
		dist2 = DotProduct( vecLen, vecLen );
	}
	else
		dist2 = 1000000000;

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
int R_WorldToScreen( const vec3_t point, vec3_t screen )
{
	matrix4x4 worldToScreen;
	qboolean  behind;
	float     w;

	if( !point || !screen )
		return true;

	Matrix4x4_Copy( worldToScreen, RI.worldviewProjectionMatrix );
	screen[0] = worldToScreen[0][0] * point[0] + worldToScreen[0][1] * point[1] + worldToScreen[0][2] * point[2] + worldToScreen[0][3];
	screen[1] = worldToScreen[1][0] * point[0] + worldToScreen[1][1] * point[1] + worldToScreen[1][2] * point[2] + worldToScreen[1][3];
	w = worldToScreen[3][0] * point[0] + worldToScreen[3][1] * point[1] + worldToScreen[3][2] * point[2] + worldToScreen[3][3];
	screen[2] = 0.0f; // just so we have something valid here

	if( w < 0.001f )
	{
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
void GAME_EXPORT R_ScreenToWorld( const vec3_t screen, vec3_t point )
{
	matrix4x4 screenToWorld;
	float     w;

	if( !point || !screen )
		return;

	Matrix4x4_Invert_Full( screenToWorld, RI.worldviewProjectionMatrix );

	point[0] = screen[0] * screenToWorld[0][0] + screen[1] * screenToWorld[0][1] + screen[2] * screenToWorld[0][2] + screenToWorld[0][3];
	point[1] = screen[0] * screenToWorld[1][0] + screen[1] * screenToWorld[1][1] + screen[2] * screenToWorld[1][2] + screenToWorld[1][3];
	point[2] = screen[0] * screenToWorld[2][0] + screen[1] * screenToWorld[2][1] + screen[2] * screenToWorld[2][2] + screenToWorld[2][3];
	w = screen[0] * screenToWorld[3][0] + screen[1] * screenToWorld[3][1] + screen[2] * screenToWorld[3][2] + screenToWorld[3][3];
	if( w != 0.0f )
		VectorScale( point, ( 1.0f / w ), point );
}

/*
===============
R_PushScene
===============
*/
void GAME_EXPORT R_PushScene( void )
{
	if( ++tr.draw_stack_pos >= MAX_DRAW_STACK )
		gEngfuncs.Host_Error( "draw stack overflow\n" );

	tr.draw_list = &tr.draw_stack[tr.draw_stack_pos];
}

/*
===============
R_PopScene
===============
*/
void GAME_EXPORT R_PopScene( void )
{
	if( --tr.draw_stack_pos < 0 )
		gEngfuncs.Host_Error( "draw stack underflow\n" );
	tr.draw_list = &tr.draw_stack[tr.draw_stack_pos];
}

/*
===============
R_ClearScene
===============
*/
void GAME_EXPORT R_ClearScene( void )
{
	tr.draw_list->num_solid_entities = 0;
	tr.draw_list->num_trans_entities = 0;
	tr.draw_list->num_beam_entities = 0;
	tr.draw_list->num_edge_entities = 0;

	// clear the scene befor start new frame
	if( gEngfuncs.drawFuncs->R_ClearScene != NULL )
		gEngfuncs.drawFuncs->R_ClearScene();

}

/*
===============
R_AddEntity
===============
*/
qboolean GAME_EXPORT R_AddEntity( struct cl_entity_s *clent, int type )
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
		if( clent->model->type == mod_brush )
		{
			if( tr.draw_list->num_edge_entities >= MAX_VISIBLE_PACKET )
				return false;

			tr.draw_list->edge_entities[tr.draw_list->num_edge_entities] = clent;
			tr.draw_list->num_edge_entities++;
			return true;
		}
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
	memset( vid.buffer, 0, vid.width * vid.height * 2 );
}

// =============================================================================
/*
===============
R_GetFarClip
===============
*/
static float R_GetFarClip( void )
{
	if( WORLDMODEL && RI.drawWorld )
		return tr.movevars->zmax * 1.73f;
	return 2048.0f;
}

/*
===============
R_SetupFrustum
===============
*/
void R_SetupFrustum( void )
{
	// build the transformation matrix for the given view angles
	AngleVectors( RI.viewangles, RI.vforward, RI.vright, RI.vup );

	{
		VectorCopy( RI.vieworg, RI.cullorigin );
		VectorCopy( RI.vforward, RI.cull_vforward );
		VectorCopy( RI.vright, RI.cull_vright );
		VectorCopy( RI.vup, RI.cull_vup );
	}
}

/*
=============
R_SetupProjectionMatrix
=============
*/
static void R_SetupProjectionMatrix( matrix4x4 m )
{
	float xMin, xMax, yMin, yMax, zNear, zFar;

	if( RI.drawOrtho )
	{
		const ref_overview_t *ov = gEngfuncs.GetOverviewParms();
		Matrix4x4_CreateOrtho( m, ov->xLeft, ov->xRight, ov->yTop, ov->yBottom, ov->zNear, ov->zFar );
		return;
	}

	RI.farClip = R_GetFarClip();

	zNear = 4.0f;
	zFar = Q_max( 256.0f, RI.farClip );

	yMax = zNear * tan( RI.fov_y * M_PI_F / 360.0f );
	yMin = -yMax;

	xMax = zNear * tan( RI.fov_x * M_PI_F / 360.0f );
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
}

/*
=============
R_RotateForEntity
=============
*/
void R_RotateForEntity( cl_entity_t *e )
{
}

/*
=============
R_TranslateForEntity
=============
*/
void R_TranslateForEntity( cl_entity_t *e )
{
}

/*
===============
R_FindViewLeaf
===============
*/
void R_FindViewLeaf( void )
{
	RI.oldviewleaf = RI.viewleaf;
	RI.viewleaf = gEngfuncs.Mod_PointInLeaf( RI.pvsorigin, WORLDMODEL->nodes );
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

//	if( !gl_nosort->value )
	{
		// sort translucents entities by rendermode and distance
		qsort( tr.draw_list->trans_entities, tr.draw_list->num_trans_entities, sizeof( cl_entity_t * ), (void *)R_TransEntityCompare );
	}

	// current viewleaf
	if( RI.drawWorld )
	{
		RI.isSkyVisible = false; // unknown at this moment
		R_FindViewLeaf();
	}

	// setup twice until globals fully refactored
	R_SetupFrameQ();
}

/*
=============
R_RecursiveFindWaterTexture

using to find source waterleaf with
watertexture to grab fog values from it
=============
*/
static image_t *R_RecursiveFindWaterTexture( const mnode_t *node, const mnode_t *ignore, qboolean down )
{
	image_t *tex = NULL;
	mnode_t *children[2];

	// assure the initial node is not null
	// we could check it here, but we would rather check it
	// outside the call to get rid of one additional recursion level
	Assert( node != NULL );

	// ignore solid nodes
	if( node->contents == CONTENTS_SOLID )
		return NULL;

	if( node->contents < 0 )
	{
		mleaf_t    *pleaf;
		msurface_t **mark;
		int        i, c;

		// ignore non-liquid leaves
		if( node->contents != CONTENTS_WATER && node->contents != CONTENTS_LAVA && node->contents != CONTENTS_SLIME )
			return NULL;

		// find texture
		pleaf = (mleaf_t *)node;
		mark = pleaf->firstmarksurface;
		c = pleaf->nummarksurfaces;

		for( i = 0; i < c; i++, mark++ )
		{
			if(( *mark )->flags & SURF_DRAWTURB && ( *mark )->texinfo && ( *mark )->texinfo->texture )
				return R_GetTexture(( *mark )->texinfo->texture->gl_texturenum );
		}

		// texture not found
		return NULL;
	}

	// this is a regular node
	// traverse children
	node_children( children, node, WORLDMODEL );

	if( children[0] && ( children[0] != ignore ))
	{
		tex = R_RecursiveFindWaterTexture( children[0], node, true );
		if( tex ) return tex;
	}

	if( children[1] && ( children[1] != ignore ))
	{
		tex = R_RecursiveFindWaterTexture( children[1], node, true );
		if( tex )	return tex;
	}

	// for down recursion, return immediately
	if( down )
		return NULL;

	// texture not found, step up if any
	if( node->parent )
		return R_RecursiveFindWaterTexture( node->parent, node, false );

	// top-level node, bail out
	return NULL;
}

/*
=============
R_DrawEntitiesOnList
=============
*/
static void R_DrawEntitiesOnList( void )
{
	int i;
	// extern int d_aflatcolor;
	// d_aflatcolor = 0;
	tr.blend = 1.0f;
//	GL_CheckForErrors();
	// RI.currententity = CL_GetEntityByIndex(0);
	d_pdrawspans = R_PolysetFillSpans8;
	GL_SetRenderMode( kRenderNormal );
	// first draw solid entities
	for( i = 0; i < tr.draw_list->num_solid_entities && !RI.onlyClientDraw; i++ )
	{
		RI.currententity = tr.draw_list->solid_entities[i];
		RI.currentmodel = RI.currententity->model;
		// d_aflatcolor += 500;

		Assert( RI.currententity != NULL );
		Assert( RI.currentmodel != NULL );

		switch( RI.currentmodel->type )
		{
		case mod_brush:
			R_DrawBrushModel( RI.currententity );
			break;
		case mod_alias:
			// R_DrawAliasModel( RI.currententity );
			break;
		case mod_studio:
			R_SetUpWorldTransform();
			R_DrawStudioModel( RI.currententity );
			break;
		default:
			break;
		}
	}

	R_SetUpWorldTransform();
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

	if( !RI.onlyClientDraw )
	{
		gEngfuncs.CL_DrawEFX( tr.frametime, false );
	}

	if( RI.drawWorld )
		gEngfuncs.pfnDrawNormalTriangles();

	d_pdrawspans = R_PolysetDrawSpans8_33;
	// then draw translucent entities
	for( i = 0; i < tr.draw_list->num_trans_entities && !RI.onlyClientDraw; i++ )
	{
		RI.currententity = tr.draw_list->trans_entities[i];
		RI.currentmodel = RI.currententity->model;

		// handle studiomodels with custom rendermodes on texture
		if( RI.currententity->curstate.rendermode != kRenderNormal )
			tr.blend = CL_FxBlend( RI.currententity ) / 255.0f;
		else
			tr.blend = 1.0f; // draw as solid but sorted by distance

		if( tr.blend <= 0.0f )
			continue;

		Assert( RI.currententity != NULL );
		Assert( RI.currentmodel != NULL );

		switch( RI.currentmodel->type )
		{
		case mod_brush:
			R_DrawBrushModel( RI.currententity );
			break;
		case mod_alias:
			// R_DrawAliasModel( RI.currententity );
			break;
		case mod_studio:
			R_SetUpWorldTransform();
			R_DrawStudioModel( RI.currententity );
			break;
		case mod_sprite:
			R_SetUpWorldTransform();
			R_DrawSpriteModel( RI.currententity );
			break;
		default:
			break;
		}
	}

	if( RI.drawWorld )
	{
		gEngfuncs.pfnDrawTransparentTriangles();
	}

	if( !RI.onlyClientDraw )
	{
		R_AllowFog( false );
		gEngfuncs.CL_DrawEFX( tr.frametime, true );
		R_AllowFog( true );
	}

	GL_SetRenderMode( kRenderNormal );
	R_SetUpWorldTransform();
	if( !RI.onlyClientDraw )
		R_DrawViewModel();
	gEngfuncs.CL_ExtraUpdate();

}

qboolean insubmodel;

/*
=============
R_BmodelCheckBBox
=============
*/
int R_BmodelCheckBBox( float *minmaxs )
{
	int    i, *pindex, clipflags;
	vec3_t acceptpt, rejectpt;
	float  d;

	clipflags = 0;

	for( i = 0; i < 4; i++ )
	{
		// generate accept and reject points
		// FIXME: do with fast look-ups or integer tests based on the sign bit
		// of the floating point values

		pindex = qfrustum.pfrustum_indexes[i];

		rejectpt[0] = minmaxs[pindex[0]];
		rejectpt[1] = minmaxs[pindex[1]];
		rejectpt[2] = minmaxs[pindex[2]];

		d = DotProduct( rejectpt, qfrustum.view_clipplanes[i].normal );
		d -= qfrustum.view_clipplanes[i].dist;

		if( d <= 0 )
			return BMODEL_FULLY_CLIPPED;

		acceptpt[0] = minmaxs[pindex[3 + 0]];
		acceptpt[1] = minmaxs[pindex[3 + 1]];
		acceptpt[2] = minmaxs[pindex[3 + 2]];

		d = DotProduct( acceptpt, qfrustum.view_clipplanes[i].normal );
		d -= qfrustum.view_clipplanes[i].dist;

		if( d <= 0 )
			clipflags |= ( 1 << i );
	}

	return clipflags;
}

/*
===================
R_FindTopNode
===================
*/
static mnode_t *R_FindTopnode( vec3_t mins, vec3_t maxs )
{
	mplane_t *splitplane;
	int      sides;
	mnode_t  *node;

	node = WORLDMODEL->nodes;

	while( 1 )
	{
		if( node->visframe != tr.visframecount )
			return NULL;                                    // not visible at all

		if( node->contents < 0 )
		{
			if( node->contents != CONTENTS_SOLID )
				return node;                                 // we've reached a non-solid leaf, so it's
			//  visible and not BSP clipped
			return NULL;                            // in solid, so not visible
		}

		splitplane = node->plane;
		sides = BOX_ON_PLANE_SIDE( mins, maxs, splitplane );

		if( sides == 3 )
			return node;                            // this is the splitter

		// not split yet; recurse down the contacted side
		if( sides & 1 )
			node = node_child( node, 0, WORLDMODEL );
		else
			node = node_child( node, 1, WORLDMODEL );
	}
}


/*
=============
RotatedBBox

Returns an axially aligned box that contains the input box at the given rotation
=============
*/
void RotatedBBox( vec3_t mins, vec3_t maxs, vec3_t angles, vec3_t tmins, vec3_t tmaxs )
{
	vec3_t tmp, v;
	int    i, j;
	vec3_t forward, right, up;

	if( !angles[0] && !angles[1] && !angles[2] )
	{
		VectorCopy( mins, tmins );
		VectorCopy( maxs, tmaxs );
		return;
	}

	for( i = 0; i < 3; i++ )
	{
		tmins[i] = 99999;
		tmaxs[i] = -99999;
	}

	AngleVectors( angles, forward, right, up );

	for( i = 0; i < 8; i++ )
	{
		if( i & 1 )
			tmp[0] = mins[0];
		else
			tmp[0] = maxs[0];

		if( i & 2 )
			tmp[1] = mins[1];
		else
			tmp[1] = maxs[1];

		if( i & 4 )
			tmp[2] = mins[2];
		else
			tmp[2] = maxs[2];


		VectorScale( forward, tmp[0], v );
		VectorMA( v, -tmp[1], right, v );
		VectorMA( v, tmp[2], up, v );

		for( j = 0; j < 3; j++ )
		{
			if( v[j] < tmins[j] )
				tmins[j] = v[j];
			if( v[j] > tmaxs[j] )
				tmaxs[j] = v[j];
		}
	}
}


/*
=============
R_DrawBEntitiesOnList
=============
*/
static void R_DrawBEntitiesOnList( void )
{
	int     i, clipflags;
	vec3_t  oldorigin;
	vec3_t  mins, maxs;
	float   minmaxs[6];
	mnode_t *topnode;

	VectorCopy( tr.modelorg, oldorigin );
	insubmodel = true;

	for( i = 0; i < tr.draw_list->num_edge_entities && !RI.onlyClientDraw; i++ )
	{
		int k;
		RI.currententity = tr.draw_list->edge_entities[i];
		RI.currentmodel = RI.currententity->model;
		if( !RI.currentmodel )
			continue;
		if( RI.currentmodel->nummodelsurfaces == 0 )
			continue; // clip brush only
		if( RI.currentmodel->type != mod_brush )
			continue;
		// see if the bounding box lets us trivially reject, also sets
		// trivial accept status
		RotatedBBox( RI.currentmodel->mins, RI.currentmodel->maxs,
			     RI.currententity->angles, mins, maxs );
		VectorAdd( mins, RI.currententity->origin, minmaxs );
		VectorAdd( maxs, RI.currententity->origin, ( minmaxs + 3 ));

		clipflags = R_BmodelCheckBBox( minmaxs );
		if( clipflags == BMODEL_FULLY_CLIPPED )
			continue; // off the edge of the screen
		// clipflags = 0;

		topnode = R_FindTopnode( minmaxs, minmaxs + 3 );
		if( !topnode )
			continue; // no part in a visible leaf

		VectorCopy( RI.currententity->origin, r_entorigin );
		VectorSubtract( RI.vieworg, r_entorigin, tr.modelorg );
		// VectorSubtract (r_origin, RI.currententity->origin, modelorg);
		r_pcurrentvertbase = RI.currentmodel->vertexes;

		// FIXME: stop transforming twice
		R_RotateBmodel();

		// calculate dynamic lighting for bmodel
		for( k = 0; k < MAX_DLIGHTS; k++ )
		{
			dlight_t *l = &tr.dlights[k];
			vec3_t   origin_l, oldorigin;

			if( l->die < gp_cl->time || !l->radius )
				continue;

			VectorCopy( l->origin, oldorigin ); // save lightorigin
			Matrix4x4_CreateFromEntity( RI.objectMatrix, RI.currententity->angles, RI.currententity->origin, 1 );
			Matrix4x4_VectorITransform( RI.objectMatrix, l->origin, origin_l );
			VectorCopy( origin_l, l->origin ); // move light in bmodel space
			R_MarkLights( l, 1 << k, RI.currentmodel->nodes + RI.currentmodel->hulls[0].firstclipnode );
			VectorCopy( oldorigin, l->origin ); // restore lightorigin
		}

		RI.currententity->topnode = topnode;
		if( topnode->contents >= 0 )
		{
			// not a leaf; has to be clipped to the world BSP
			r_clipflags = clipflags;
			R_DrawSolidClippedSubmodelPolygons( RI.currentmodel, topnode );
		}
		else
		{
			// falls entirely in one leaf, so we just put all the
			// edges in the edge list and let 1/z sorting handle
			// drawing order
			R_DrawSubmodelPolygons( RI.currentmodel, clipflags, topnode );
		}
		RI.currententity->topnode = NULL;

		// put back world rotation and frustum clipping
		// FIXME: R_RotateBmodel should just work off base_vxx
		VectorCopy( RI.base_vpn, RI.vforward );
		VectorCopy( RI.base_vup, RI.vup );
		VectorCopy( RI.base_vright, RI.vright );
		VectorCopy( oldorigin, tr.modelorg );
		R_TransformFrustum();
	}

	insubmodel = false;
}

extern qboolean alphaspans;
/*
=============
R_DrawBEntitiesOnList
=============
*/
void R_DrawBrushModel( cl_entity_t *pent )
{
	int     i, clipflags;
	vec3_t  oldorigin;
	vec3_t  mins, maxs;
	float   minmaxs[6];
	mnode_t *topnode;
	int     k;
	edge_t  ledges[NUMSTACKEDGES
		       + (( CACHE_SIZE - 1 ) / sizeof( edge_t )) + 1];
	surf_t  lsurfs[NUMSTACKSURFACES
		       + (( CACHE_SIZE - 1 ) / sizeof( surf_t )) + 1];

	if( !RI.drawWorld )
		return;

	if( auxedges )
	{
		r_edges = auxedges;
	}
	else
	{
		r_edges = (edge_t *)
			  (((uintptr_t)&ledges[0] + CACHE_SIZE - 1 ) & ~( CACHE_SIZE - 1 ));
	}

	if( r_surfsonstack )
	{
		surfaces = (surf_t *)(((uintptr_t)&lsurfs[0] + CACHE_SIZE - 1 ) & ~( CACHE_SIZE - 1 ));
		surf_max = &surfaces[r_cnumsurfs];
		// surface 0 doesn't really exist; it's just a dummy because index 0
		// is used to indicate no edge attached to surface
		memset( &surfaces[0], 0, sizeof( surf_t ));
		surfaces--;
		R_SurfacePatch();
	}


	R_BeginEdgeFrame();

	VectorCopy( tr.modelorg, oldorigin );
	insubmodel = true;

	if( !RI.currentmodel )
		return;
	if( RI.currentmodel->nummodelsurfaces == 0 )
		return;         // clip brush only
	if( RI.currentmodel->type != mod_brush )
		return;
	// see if the bounding box lets us trivially reject, also sets
	// trivial accept status
	RotatedBBox( RI.currentmodel->mins, RI.currentmodel->maxs,
		     RI.currententity->angles, mins, maxs );
	VectorAdd( mins, RI.currententity->origin, minmaxs );
	VectorAdd( maxs, RI.currententity->origin, ( minmaxs + 3 ));

	clipflags = R_BmodelCheckBBox( minmaxs );
	if( clipflags == BMODEL_FULLY_CLIPPED )
		return;         // off the edge of the screen
	// clipflags = 0;

	topnode = R_FindTopnode( minmaxs, minmaxs + 3 );
	if( !topnode )
		return;         // no part in a visible leaf

	alphaspans = true;
	VectorCopy( RI.currententity->origin, r_entorigin );
	VectorSubtract( RI.vieworg, r_entorigin, tr.modelorg );
	// VectorSubtract (r_origin, RI.currententity->origin, modelorg);
	r_pcurrentvertbase = RI.currentmodel->vertexes;

	// FIXME: stop transforming twice
	R_RotateBmodel();

	// calculate dynamic lighting for bmodel
	for( k = 0; k < MAX_DLIGHTS; k++ )
	{
		dlight_t *l = &tr.dlights[k];
		vec3_t   origin_l, oldorigin;

		if( l->die < gp_cl->time || !l->radius )
			continue;

		VectorCopy( l->origin, oldorigin );         // save lightorigin
		Matrix4x4_CreateFromEntity( RI.objectMatrix, RI.currententity->angles, RI.currententity->origin, 1 );
		Matrix4x4_VectorITransform( RI.objectMatrix, l->origin, origin_l );
		tr.modelviewIdentity = false;
		VectorCopy( origin_l, l->origin );         // move light in bmodel space
		R_MarkLights( l, 1 << k, RI.currentmodel->nodes + RI.currentmodel->hulls[0].firstclipnode );
		VectorCopy( oldorigin, l->origin );         // restore lightorigin*/
	}

	RI.currententity->topnode = topnode;
	if( topnode->contents >= 0 )
	{
		// not a leaf; has to be clipped to the world BSP
		r_clipflags = clipflags;
		R_DrawSolidClippedSubmodelPolygons( RI.currentmodel, topnode );
	}
	else
	{
		// falls entirely in one leaf, so we just put all the
		// edges in the edge list and let 1/z sorting handle
		// drawing order
		R_DrawSubmodelPolygons( RI.currentmodel, clipflags, topnode );
	}
	RI.currententity->topnode = NULL;

	// put back world rotation and frustum clipping
	// FIXME: R_RotateBmodel should just work off base_vxx
	VectorCopy( RI.base_vpn, RI.vforward );
	VectorCopy( RI.base_vup, RI.vup );
	VectorCopy( RI.base_vright, RI.vright );
	VectorCopy( oldorigin, tr.modelorg );
	R_TransformFrustum();


	insubmodel = false;
	R_ScanEdges();
	alphaspans = false;
}

/*
================
R_EdgeDrawing
================
*/
static void R_EdgeDrawing( void )
{
	edge_t ledges[NUMSTACKEDGES
		      + (( CACHE_SIZE - 1 ) / sizeof( edge_t )) + 1];
	surf_t lsurfs[NUMSTACKSURFACES
		      + (( CACHE_SIZE - 1 ) / sizeof( surf_t )) + 1];

	if( !RI.drawWorld )
		return;

	if( auxedges )
	{
		r_edges = auxedges;
	}
	else
	{
		r_edges = (edge_t *)
			  (((uintptr_t)&ledges[0] + CACHE_SIZE - 1 ) & ~( CACHE_SIZE - 1 ));
	}

	if( r_surfsonstack )
	{
		surfaces = (surf_t *)(((uintptr_t)&lsurfs + CACHE_SIZE - 1 ) & ~( CACHE_SIZE - 1 ));
		surf_max = &surfaces[r_cnumsurfs];

		// surface 0 doesn't really exist; it's just a dummy because index 0
		// is used to indicate no edge attached to surface

		memset( surfaces, 0, sizeof( surf_t ));
		surfaces--;
		R_SurfacePatch();
	}

	R_BeginEdgeFrame();

	// this will prepare edges
	R_RenderWorld();

	// move brushes to separate list to merge with edges?
	R_DrawBEntitiesOnList();

	// display all edges
	R_ScanEdges();
}

/*
===============
R_MarkLeaves
===============
*/
static void R_MarkLeaves( void )
{
	byte    *vis;
	mnode_t *node;
	int     i;

	if( r_oldviewcluster == r_viewcluster && !r_novis.value && r_viewcluster != -1 )
		return;

	tr.visframecount++;
	r_oldviewcluster = r_viewcluster;

	gEngfuncs.R_FatPVS( RI.pvsorigin, REFPVS_RADIUS, RI.visbytes, FBitSet( RI.params, RP_OLDVIEWLEAF ), false );
	vis = RI.visbytes;

	for( i = 0; i < WORLDMODEL->numleafs; i++ )
	{
		if( vis[i >> 3] & ( 1 << ( i & 7 )))
		{
			node = (mnode_t *) &WORLDMODEL->leafs[i + 1];
			do
			{
				if( node->visframe == tr.visframecount )
					break;
				node->visframe = tr.visframecount;
				node = node->parent;
			}
			while( node );
		}
	}
}

/*
================
R_RenderScene

R_SetupRefParams must be called right before
================
*/
void GAME_EXPORT R_RenderScene( void )
{
	if( !WORLDMODEL && RI.drawWorld )
		gEngfuncs.Host_Error( "%s: NULL worldmodel\n", __func__ );

	// frametime is valid only for normal pass
	if( RP_NORMALPASS( ))
		tr.frametime = gp_cl->time - gp_cl->oldtime;
	else
		tr.frametime = 0.0;

	// begin a new frame
	tr.framecount++;

	if( tr.map_unload )
	{
		D_FlushCaches();
		tr.map_unload = false;
	}


	R_SetupFrustum();
	R_SetupFrame();

	R_PushDlights();
	R_SetupModelviewMatrix( RI.worldviewMatrix );
	R_SetupProjectionMatrix( RI.projectionMatrix );

	Matrix4x4_Concat( RI.worldviewProjectionMatrix, RI.projectionMatrix, RI.worldviewMatrix );
	tr.modelviewIdentity = true;

//	R_SetupGL( true );
	// R_Clear( ~0 );

	R_MarkLeaves();
	// R_PushDlights (r_worldmodel); ??
	// R_DrawWorld();
	R_EdgeDrawing();

	gEngfuncs.CL_ExtraUpdate(); // don't let sound get messed up if going slow

	R_DrawEntitiesOnList();

//	R_DrawWaterSurfaces();

//	R_EndGL();
}

void R_GammaChanged( qboolean do_reset_gamma )
{
	if( do_reset_gamma ) // unused
		return;

	D_FlushCaches( );
}

/*
===============
R_BeginFrame
===============
*/
void GAME_EXPORT R_BeginFrame( qboolean clearScene )
{
	R_Set2DMode( true );

	// draw buffer stuff
	// pglDrawBuffer( GL_BACK );

	// update texture parameters
	// if( FBitSet( gl_texture_nearest->flags|gl_lightmap_nearest->flags|gl_texture_anisotropy->flags|gl_texture_lodbias->flags, FCVAR_CHANGED ))
	// R_SetTextureParameters();

	gEngfuncs.CL_ExtraUpdate();
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

	if( !FBitSet( rvp->flags, RF_DRAW_CUBEMAP ))
		RI.drawOrtho = FBitSet( rvp->flags, RF_DRAW_OVERVIEW );
	else
		RI.drawOrtho = false;

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
void GAME_EXPORT R_RenderFrame( const ref_viewpass_t *rvp )
{
	if( r_norefresh->value )
		return;

	// prevent cache overrun
	if( gpGlobals->height > vid.height || gpGlobals->width > vid.width )
		return;

	// setup the initial render params
	R_SetupRefParams( rvp );

	// completely override rendering
	if( gEngfuncs.drawFuncs->GL_RenderFrame != NULL )
	{
		tr.fCustomRendering = true;

		if( gEngfuncs.drawFuncs->GL_RenderFrame( rvp ))
		{
			// R_GatherPlayerLight();
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

	return;
}

/*
===============
R_EndFrame
===============
*/
void GAME_EXPORT R_EndFrame( void )
{
	// flush any remaining 2D bits
	R_Set2DMode( false );

	// blit pixels
	R_BlitScreen();
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

	RI.viewleaf = NULL; // force markleafs next frame
}

/*
===============
R_NewMap
===============
*/
void GAME_EXPORT R_NewMap( void )
{
	int     i;
	model_t *world = WORLDMODEL;

	r_viewcluster = -1;

	tr.draw_list->num_solid_entities = 0;
	tr.draw_list->num_trans_entities = 0;
	tr.draw_list->num_beam_entities = 0;
	tr.draw_list->num_edge_entities = 0;

	R_ClearDecals(); // clear all level decals
	R_StudioResetPlayerModels();

	if( FBitSet( world->flags, MODEL_QBSP2 ))
	{
		gEngfuncs.Host_Error( "Sorry, ref_soft can't load maps in BSP2 format.\n" );
		return;
	}

	r_cnumsurfs = sw_maxsurfs.value;

	if( r_cnumsurfs <= MINSURFACES )
		r_cnumsurfs = MINSURFACES;

	if( r_cnumsurfs > NUMSTACKSURFACES )
	{
		surfaces = Mem_Calloc( r_temppool, r_cnumsurfs * sizeof( surf_t ));
		surface_p = surfaces;
		surf_max = &surfaces[r_cnumsurfs];
		r_surfsonstack = false;
		// surface 0 doesn't really exist; it's just a dummy because index 0
		// is used to indicate no edge attached to surface
		surfaces--;
		R_SurfacePatch();
	}
	else
	{
		r_surfsonstack = true;
	}

	r_numallocatededges = sw_maxedges.value;

	if( r_numallocatededges < MINEDGES )
		r_numallocatededges = MINEDGES;

	if( r_numallocatededges <= NUMSTACKEDGES )
	{
		auxedges = NULL;
	}
	else
	{
		auxedges = Mem_Malloc( r_temppool, r_numallocatededges * sizeof( edge_t ));
	}

	// clear out efrags in case the level hasn't been reloaded
	for( i = 0; i < world->numleafs; i++ )
		world->leafs[i + 1].efrags = NULL;

	tr.sample_size = gEngfuncs.Mod_SampleSizeForFace( &world->surfaces[0] );

	for( i = 1; i < world->numsurfaces; i++ )
	{
		int sample_size = gEngfuncs.Mod_SampleSizeForFace( &world->surfaces[i] );
		if( sample_size != tr.sample_size )
		{
			tr.sample_size = -1;
			break;
		}
	}
	tr.sample_bits = -1;

	if( tr.sample_size != -1 )
	{
		uint sample_pot;

		tr.sample_bits = 0;

		for( sample_pot = 1; sample_pot < tr.sample_size; sample_pot <<= 1, tr.sample_bits++ )
			;
	}

	gEngfuncs.Con_Printf( "Map sample size is %d\n", tr.sample_size );

}

/*
================
R_InitTurb
================
*/
static void R_InitTurb( void )
{
	int i;

	for( i = 0; i < 1280; i++ )
	{
		sintable[i] = AMP + sin( i * 3.14159 * 2 / CYCLE ) * AMP;
		intsintable[i] = AMP2 + sin( i * 3.14159 * 2 / CYCLE ) * AMP2; // AMP2, not 20
		blanktable[i] = 0;                                             // PGM
	}
}



qboolean GAME_EXPORT R_Init( void )
{
	qboolean glblit = false;

	RETRIEVE_ENGINE_SHARED_CVAR_LIST();


	gEngfuncs.Cvar_RegisterVariable( &sw_clearcolor );
	gEngfuncs.Cvar_RegisterVariable( &sw_drawflat );
	gEngfuncs.Cvar_RegisterVariable( &sw_draworder );
	gEngfuncs.Cvar_RegisterVariable( &sw_maxedges );
	gEngfuncs.Cvar_RegisterVariable( &sw_maxsurfs );
	gEngfuncs.Cvar_RegisterVariable( &sw_mipscale );
	gEngfuncs.Cvar_RegisterVariable( &sw_mipcap );
	gEngfuncs.Cvar_RegisterVariable( &sw_surfcacheoverride );
	gEngfuncs.Cvar_RegisterVariable( &sw_waterwarp );
	gEngfuncs.Cvar_RegisterVariable( &sw_notransbrushes );
	gEngfuncs.Cvar_RegisterVariable( &sw_noalphabrushes );
	gEngfuncs.Cvar_RegisterVariable( &r_traceglow );
#ifndef DISABLE_TEXFILTER
	gEngfuncs.Cvar_RegisterVariable( &sw_texfilt );
#endif
	gEngfuncs.Cvar_RegisterVariable( &r_novis );
	gEngfuncs.Cvar_RegisterVariable( &r_studio_sort_textures );

	r_temppool = Mem_AllocPool( "ref_soft zone" );

	glblit = !!gEngfuncs.Sys_CheckParm( "-glblit" );

	// create the window and set up the context
	if( !glblit && !gEngfuncs.R_Init_Video( REF_SOFTWARE )) // request software blitter
	{
		gEngfuncs.R_Free_Video();
		gEngfuncs.Con_Printf( "failed to initialize software blitter, fallback to glblit\n" );
		glblit = true;
	}

	if( glblit && !gEngfuncs.R_Init_Video( REF_GL )) // request GL context
	{
		gEngfuncs.R_Free_Video();
		return false;
	}

	// see R_ProcessEntData for tr.entities initialization
	tr.movevars = (movevars_t *)ENGINE_GET_PARM( PARM_GET_MOVEVARS_PTR );
	tr.palette = (color24 *)ENGINE_GET_PARM( PARM_GET_PALETTE_PTR );
	tr.viewent = (cl_entity_t *)ENGINE_GET_PARM( PARM_GET_VIEWENT_PTR );
	tr.texgammatable = (byte *)ENGINE_GET_PARM( PARM_GET_TEXGAMMATABLE_PTR );
	tr.lightgammatable = (uint *)ENGINE_GET_PARM( PARM_GET_LIGHTGAMMATABLE_PTR );
	tr.screengammatable = (uint *)ENGINE_GET_PARM( PARM_GET_SCREENGAMMATABLE_PTR );
	tr.lineargammatable = (uint *)ENGINE_GET_PARM( PARM_GET_LINEARGAMMATABLE_PTR );
	tr.dlights = (dlight_t *)ENGINE_GET_PARM( PARM_GET_DLIGHTS_PTR );
	tr.elights = (dlight_t *)ENGINE_GET_PARM( PARM_GET_ELIGHTS_PTR );

	if( !R_InitBlit( glblit ))
	{
		gEngfuncs.R_Free_Video();
		return false;
	}

	R_InitImages();
	// init draw stack
	tr.draw_list = &tr.draw_stack[0];
	tr.draw_stack_pos = 0;
	qfrustum.view_clipplanes[0].leftedge = true;
	qfrustum.view_clipplanes[1].rightedge = true;
	qfrustum.view_clipplanes[1].leftedge = qfrustum.view_clipplanes[2].leftedge = qfrustum.view_clipplanes[3].leftedge = false;
	qfrustum.view_clipplanes[0].rightedge = qfrustum.view_clipplanes[2].rightedge = qfrustum.view_clipplanes[3].rightedge = false;
	R_StudioInit();
	R_SpriteInit();
	R_InitTurb();
	GL_InitRandomTable();

	return true;
}

void GAME_EXPORT R_Shutdown( void )
{
	R_ShutdownImages();
	gEngfuncs.R_Free_Video();
}


/*
===============
CL_FxBlend
===============
*/
int CL_FxBlend( cl_entity_t *e )
{
	int    blend = 0;
	float  offset, dist;
	vec3_t tmp;

	offset = ((int)e->index ) * 363.0f; // Use ent index to de-sync these fx

	switch( e->curstate.renderfx )
	{
	case kRenderFxPulseSlowWide:
		blend = e->curstate.renderamt + 0x40 * sin( gp_cl->time * 2 + offset );
		break;
	case kRenderFxPulseFastWide:
		blend = e->curstate.renderamt + 0x40 * sin( gp_cl->time * 8 + offset );
		break;
	case kRenderFxPulseSlow:
		blend = e->curstate.renderamt + 0x10 * sin( gp_cl->time * 2 + offset );
		break;
	case kRenderFxPulseFast:
		blend = e->curstate.renderamt + 0x10 * sin( gp_cl->time * 8 + offset );
		break;
	case kRenderFxFadeSlow:
		if( RP_NORMALPASS( ))
		{
			if( e->curstate.renderamt > 0 )
				e->curstate.renderamt -= 1;
			else
				e->curstate.renderamt = 0;
		}
		blend = e->curstate.renderamt;
		break;
	case kRenderFxFadeFast:
		if( RP_NORMALPASS( ))
		{
			if( e->curstate.renderamt > 3 )
				e->curstate.renderamt -= 4;
			else
				e->curstate.renderamt = 0;
		}
		blend = e->curstate.renderamt;
		break;
	case kRenderFxSolidSlow:
		if( RP_NORMALPASS( ))
		{
			if( e->curstate.renderamt < 255 )
				e->curstate.renderamt += 1;
			else
				e->curstate.renderamt = 255;
		}
		blend = e->curstate.renderamt;
		break;
	case kRenderFxSolidFast:
		if( RP_NORMALPASS( ))
		{
			if( e->curstate.renderamt < 252 )
				e->curstate.renderamt += 4;
			else
				e->curstate.renderamt = 255;
		}
		blend = e->curstate.renderamt;
		break;
	case kRenderFxStrobeSlow:
		blend = 20 * sin( gp_cl->time * 4 + offset );
		if( blend < 0 )
			blend = 0;
		else
			blend = e->curstate.renderamt;
		break;
	case kRenderFxStrobeFast:
		blend = 20 * sin( gp_cl->time * 16 + offset );
		if( blend < 0 )
			blend = 0;
		else
			blend = e->curstate.renderamt;
		break;
	case kRenderFxStrobeFaster:
		blend = 20 * sin( gp_cl->time * 36 + offset );
		if( blend < 0 )
			blend = 0;
		else
			blend = e->curstate.renderamt;
		break;
	case kRenderFxFlickerSlow:
		blend = 20 * ( sin( gp_cl->time * 2 ) + sin( gp_cl->time * 17 + offset ));
		if( blend < 0 )
			blend = 0;
		else
			blend = e->curstate.renderamt;
		break;
	case kRenderFxFlickerFast:
		blend = 20 * ( sin( gp_cl->time * 16 ) + sin( gp_cl->time * 23 + offset ));
		if( blend < 0 )
			blend = 0;
		else
			blend = e->curstate.renderamt;
		break;
	case kRenderFxHologram:
	case kRenderFxDistort:
		VectorCopy( e->origin, tmp );
		VectorSubtract( tmp, RI.vieworg, tmp );
		dist = DotProduct( tmp, RI.vforward );

		// turn off distance fade
		if( e->curstate.renderfx == kRenderFxDistort )
			dist = 1;

		if( dist <= 0 )
		{
			blend = 0;
		}
		else
		{
			e->curstate.renderamt = 180;
			if( dist <= 100 )
				blend = e->curstate.renderamt;
			else
				blend = (int) (( 1.0f - ( dist - 100 ) * ( 1.0f / 400.0f )) * e->curstate.renderamt );
			blend += gEngfuncs.COM_RandomLong( -32, 31 );
		}
		break;
	default:
		blend = e->curstate.renderamt;
		break;
	}

	blend = bound( 0, blend, 255 );

	return blend;
}

