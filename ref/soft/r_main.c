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
//#include "beamdef.h"
//#include "particledef.h"
#include "entity_types.h"
#include "mod_local.h"
int r_cnumsurfs;
#define IsLiquidContents( cnt )	( cnt == CONTENTS_WATER || cnt == CONTENTS_SLIME || cnt == CONTENTS_LAVA )

ref_instance_t	RI;


// quake defines. will be refactored

// view origin
//

//
// screen size info
//
float		xcenter, ycenter;
float		xscale, yscale;
float		xscaleinv, yscaleinv;
//float		xscaleshrink, yscaleshrink;
float		aliasxscale, aliasyscale, aliasxcenter, aliasycenter;

int		r_screenwidth;




//
// refresh flags
//

//int             d_spanpixcount;
//int             r_polycount;
//int             r_drawnpolycount;
//int             r_wholepolycount;

int                     r_viewcluster, r_oldviewcluster;

cvar_t	*sw_aliasstats;
cvar_t	*sw_allow_modex;
cvar_t	*sw_clearcolor;
cvar_t	*sw_drawflat;
cvar_t	*sw_draworder;
cvar_t	*sw_maxedges;
cvar_t	*sw_maxsurfs;
cvar_t	*sw_reportedgeout;
cvar_t	*sw_reportsurfout;
cvar_t  *sw_stipplealpha;
cvar_t	*sw_surfcacheoverride;
cvar_t	*sw_waterwarp;
cvar_t	*sw_texfilt;
cvar_t	*sw_notransbrushes;
cvar_t	*sw_noalphabrushes;

cvar_t	*r_drawworld;
cvar_t	*r_dspeeds;
cvar_t  *r_lerpmodels;
cvar_t  *r_novis;
cvar_t	*r_traceglow;

cvar_t	*r_lightlevel;	//FIXME HACK

//PGM
cvar_t	*sw_lockpvs;
//PGM

DEFINE_ENGINE_SHARED_CVAR_LIST()

int	r_viewcluster, r_oldviewcluster;

float   d_sdivzstepu, d_tdivzstepu, d_zistepu;
float   d_sdivzstepv, d_tdivzstepv, d_zistepv;
float   d_sdivzorigin, d_tdivzorigin, d_ziorigin;

fixed16_t       sadjust, tadjust, bbextents, bbextentt;

pixel_t                 *cacheblock;
int                             cachewidth;
pixel_t                 *d_viewbuffer;
short                   *d_pzbuffer;
unsigned int    d_zrowbytes;
unsigned int    d_zwidth;

mvertex_t       *r_pcurrentvertbase;

//int                     c_surf;
qboolean        r_surfsonstack;
int                     r_clipflags;
byte            r_warpbuffer[WARP_WIDTH * WARP_HEIGHT];
int                     r_numallocatededges;

float           r_aliasuvscale = 1.0;

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
#if 0
/*
================
R_GetEntityRenderMode

check for texture flags
================
*/
int R_GetEntityRenderMode( cl_entity_t *ent )
{
	int		i, opaque, trans;
	mstudiotexture_t	*ptexture;
	cl_entity_t	*oldent;
	model_t		*model;
	studiohdr_t	*phdr;

	oldent = RI.currententity;
	RI.currententity = ent;
	return ent->curstate.rendermode;
}
#endif

void GAME_EXPORT R_AllowFog( qboolean allowed )
{
}

/*
===============
R_OpaqueEntity

Opaque entity can be brush or studio model but sprite
===============
*/
static qboolean R_OpaqueEntity( cl_entity_t *ent )
{
	int rendermode = R_GetEntityRenderMode( ent );

	if( rendermode == kRenderNormal )
		return true;

	if( sw_notransbrushes->value && ent->model && ent->model->type == mod_brush && rendermode == kRenderTransTexture )
		return true;

	if( sw_noalphabrushes->value && ent->model && ent->model->type == mod_brush && rendermode == kRenderTransAlpha )
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
	if( ( ent1->model && ent1->model->type != mod_brush ) || rendermode1 != kRenderTransAlpha )
	{
		VectorAverage( ent1->model->mins, ent1->model->maxs, org );
		VectorAdd( ent1->origin, org, org );
		VectorSubtract( RI.vieworg, org, vecLen );
		dist1 = DotProduct( vecLen, vecLen );
	}
	else dist1 = 1000000000;

	if( ( ent1->model && ent2->model->type != mod_brush ) || rendermode2 != kRenderTransAlpha )
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

#if 1

/*
===============
R_WorldToScreen

Convert a given point from world into screen space
Returns true if we behind to screen
===============
*/
int R_WorldToScreen( const vec3_t point, vec3_t screen )
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

#endif

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
	int	bits;
#if 0
	if( gEngfuncs.CL_IsDevOverviewMode( ))
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
#endif
	memset( vid.buffer, 0, vid.width * vid.height *2);
}

//=============================================================================
/*
===============
R_GetFarClip
===============
*/
static float R_GetFarClip( void )
{
	if( WORLDMODEL && RI.drawWorld )
		return MOVEVARS->zmax * 1.73f;
	return 2048.0f;
}

/*
===============
R_SetupFrustum
===============
*/
void R_SetupFrustum( void )
{
#if 1
	//ref_overview_t	*ov = gEngfuncs.GetOverviewParms();

	/*if( RP_NORMALPASS() && ( ENGINE_GET_PARM( PARM_WATER_LEVEL ) >= 3 ) && ENGINE_GET_PARM( PARM_QUAKE_COMPATIBLE ))
	{
		RI.fov_x = atan( tan( DEG2RAD( RI.fov_x ) / 2 ) * ( 0.97 + sin( gpGlobals->time * 1.5 ) * 0.03 )) * 2 / (M_PI / 180.0);
		RI.fov_y = atan( tan( DEG2RAD( RI.fov_y ) / 2 ) * ( 1.03 - sin( gpGlobals->time * 1.5 ) * 0.03 )) * 2 / (M_PI / 180.0);
	}*/

	// build the transformation matrix for the given view angles
	AngleVectors( RI.viewangles, RI.vforward, RI.vright, RI.vup );

	//if( !r_lockfrustum->value )
	{
		VectorCopy( RI.vieworg, RI.cullorigin );
		VectorCopy( RI.vforward, RI.cull_vforward );
		VectorCopy( RI.vright, RI.cull_vright );
		VectorCopy( RI.vup, RI.cull_vup );
	}

//	if( RI.drawOrtho )
//		GL_FrustumInitOrtho( &RI.frustum, ov->xLeft, ov->xRight, ov->yTop, ov->yBottom, ov->zNear, ov->zFar );
//	else GL_FrustumInitProj( &RI.frustum, 0.0f, R_GetFarClip(), RI.fov_x, RI.fov_y ); // NOTE: we ignore nearplane here (mirrors only)
#endif
}

/*
=============
R_SetupProjectionMatrix
=============
*/
static void R_SetupProjectionMatrix( matrix4x4 m )
{
#if 1
	float	xMin, xMax, yMin, yMax, zNear, zFar;

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
#endif
}

/*
=============
R_SetupModelviewMatrix
=============
*/
static void R_SetupModelviewMatrix( matrix4x4 m )
{
#if 1
	Matrix4x4_CreateModelview( m );
	Matrix4x4_ConcatRotate( m, -RI.viewangles[2], 1, 0, 0 );
	Matrix4x4_ConcatRotate( m, -RI.viewangles[0], 0, 1, 0 );
	Matrix4x4_ConcatRotate( m, -RI.viewangles[1], 0, 0, 1 );
	Matrix4x4_ConcatTranslate( m, -RI.vieworg[0], -RI.vieworg[1], -RI.vieworg[2] );
#endif
}

/*
=============
R_LoadIdentity
=============
*/
void R_LoadIdentity( void )
{
#if 0
	if( tr.modelviewIdentity ) return;

	Matrix4x4_LoadIdentity( RI.objectMatrix );
	Matrix4x4_Copy( RI.modelviewMatrix, RI.worldviewMatrix );

	pglMatrixMode( GL_MODELVIEW );
	GL_LoadMatrix( RI.modelviewMatrix );
	tr.modelviewIdentity = true;
#endif
}

/*
=============
R_RotateForEntity
=============
*/
void R_RotateForEntity( cl_entity_t *e )
{
#if 0
	float	scale = 1.0f;

	if( e == gEngfuncs.GetEntityByIndex( 0 ) )
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
#endif
}

/*
=============
R_TranslateForEntity
=============
*/
void R_TranslateForEntity( cl_entity_t *e )
{
#if 0
	float	scale = 1.0f;

	if( e == gEngfuncs.GetEntityByIndex( 0 ) )
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
#endif
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
		qsort( tr.draw_list->trans_entities, tr.draw_list->num_trans_entities, sizeof( cl_entity_t* ), (void*)R_TransEntityCompare );
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
#if 0

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
		x = floor( RI.viewport[0] * gpGlobals->width / gpGlobals->width );
		x2 = ceil(( RI.viewport[0] + RI.viewport[2] ) * gpGlobals->width / gpGlobals->width );
		y = floor( gpGlobals->height - RI.viewport[1] * gpGlobals->height / gpGlobals->height );
		y2 = ceil( gpGlobals->height - ( RI.viewport[1] + RI.viewport[3] ) * gpGlobals->height / gpGlobals->height );

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

#endif

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

extern void R_PolysetFillSpans8 ( void * );
extern void R_PolysetDrawSpansConstant8_33( void *pspanpackage);
extern void R_PolysetDrawSpans8_33( void *pspanpackage);

/*
=============
R_DrawEntitiesOnList
=============
*/
void R_DrawEntitiesOnList( void )
{
	extern void	(*d_pdrawspans)(void *);
	int	i;
	//extern int d_aflatcolor;
	//d_aflatcolor = 0;
	tr.blend = 1.0f;
//	GL_CheckForErrors();
	//RI.currententity = gEngfuncs.GetEntityByIndex(0);
	d_pdrawspans = R_PolysetFillSpans8;
	GL_SetRenderMode(kRenderNormal);
	// first draw solid entities
	for( i = 0; i < tr.draw_list->num_solid_entities && !RI.onlyClientDraw; i++ )
	{
		RI.currententity = tr.draw_list->solid_entities[i];
		RI.currentmodel = RI.currententity->model;
		//d_aflatcolor += 500;

		Assert( RI.currententity != NULL );
		Assert( RI.currentmodel != NULL );

		switch( RI.currentmodel->type )
		{
		case mod_brush:
			R_DrawBrushModel( RI.currententity );
			break;
		case mod_alias:
			//R_DrawAliasModel( RI.currententity );
			break;
		case mod_studio:
			R_SetUpWorldTransform();
			R_DrawStudioModel( RI.currententity );
		#if 0
			// gradient debug (for colormap testing)
		{finalvert_t fv[3];
			void R_AliasSetUpTransform (void);
			extern void	(*d_pdrawspans)(void *);
			extern void R_PolysetFillSpans8 ( void * );
			d_pdrawspans = R_PolysetFillSpans8;
			//RI.currententity = gEngfuncs.GetEntityByIndex(0);
			R_AliasSetUpTransform();
			image_t *image = R_GetTexture(GL_LoadTexture("gfx/env/desertbk", NULL, 0, 0));
			r_affinetridesc.pskin = image->pixels[0];
			r_affinetridesc.skinwidth = image->width;
			r_affinetridesc.skinheight = image->height;
			R_SetupFinalVert( &fv[0], 0, -50, 50, 31 << 8, 0, 0);
			R_SetupFinalVert( &fv[1], 0, 50, 50, 0 << 8, image->width, 0);
			R_SetupFinalVert( &fv[2], 0, 0, 0, 0 << 8, image->width/2, image->height);
			R_RenderTriangle( &fv[0], &fv[1], &fv[2] );
		}
#endif


			break;
		default:
			break;
		}
	}

//	GL_CheckForErrors();

	// quake-specific feature
//	R_DrawAlphaTextureChains();

//	GL_CheckForErrors();

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

//	GL_CheckForErrors();

	if( !RI.onlyClientDraw )
	{
		gEngfuncs.CL_DrawEFX( tr.frametime, false );
	}

//	GL_CheckForErrors();

	if( RI.drawWorld )
		gEngfuncs.pfnDrawNormalTriangles();

//	GL_CheckForErrors();
	d_pdrawspans = R_PolysetDrawSpans8_33;
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
			//R_DrawAliasModel( RI.currententity );
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

//	GL_CheckForErrors();

	if( RI.drawWorld )
	{
	//	pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
		gEngfuncs.pfnDrawTransparentTriangles ();
	}

//	GL_CheckForErrors();

	if( !RI.onlyClientDraw )
	{
		R_AllowFog( false );
		gEngfuncs.CL_DrawEFX( tr.frametime, true );
		R_AllowFog( true );
	}

	//GL_CheckForErrors();

//	pglDisable( GL_BLEND );	// Trinity Render issues
	GL_SetRenderMode(kRenderNormal);
	R_SetUpWorldTransform();
	if( !RI.onlyClientDraw )
		R_DrawViewModel();
	gEngfuncs.CL_ExtraUpdate();

	//GL_CheckForErrors();
}

#if 1
qboolean insubmodel;


/*
=============
R_BmodelCheckBBox
=============
*/
int R_BmodelCheckBBox (float *minmaxs)
{
	int			i, *pindex, clipflags;
	vec3_t		acceptpt, rejectpt;
	float		d;

	clipflags = 0;

	for (i=0 ; i<4 ; i++)
	{
	// generate accept and reject points
	// FIXME: do with fast look-ups or integer tests based on the sign bit
	// of the floating point values

		pindex = qfrustum.pfrustum_indexes[i];

		rejectpt[0] = minmaxs[pindex[0]];
		rejectpt[1] = minmaxs[pindex[1]];
		rejectpt[2] = minmaxs[pindex[2]];

		d = DotProduct (rejectpt, qfrustum.view_clipplanes[i].normal);
		d -= qfrustum.view_clipplanes[i].dist;

		if (d <= 0)
			return BMODEL_FULLY_CLIPPED;

		acceptpt[0] = minmaxs[pindex[3+0]];
		acceptpt[1] = minmaxs[pindex[3+1]];
		acceptpt[2] = minmaxs[pindex[3+2]];

		d = DotProduct (acceptpt, qfrustum.view_clipplanes[i].normal);
		d -= qfrustum.view_clipplanes[i].dist;

		if (d <= 0)
			clipflags |= (1<<i);
	}

	return clipflags;
}

/*
===================
R_FindTopNode
===================
*/
mnode_t *R_FindTopnode (vec3_t mins, vec3_t maxs)
{
		mplane_t        *splitplane;
		int                     sides;
		mnode_t         *node;

		node = WORLDMODEL->nodes;

		while (1)
		{
				if (node->visframe != tr.visframecount)
						return NULL;            // not visible at all

				if (node->contents < 0)
				{
						if (node->contents != CONTENTS_SOLID)
								return node; // we've reached a non-solid leaf, so it's
														//  visible and not BSP clipped
						return NULL;    // in solid, so not visible
				}

				splitplane = node->plane;
				sides = BOX_ON_PLANE_SIDE (mins, maxs, splitplane);

				if (sides == 3)
						return node;    // this is the splitter

				// not split yet; recurse down the contacted side
				if (sides & 1)
						node = node->children[0];
				else
						node = node->children[1];
		}
}


/*
=============
RotatedBBox

Returns an axially aligned box that contains the input box at the given rotation
=============
*/
void RotatedBBox (vec3_t mins, vec3_t maxs, vec3_t angles, vec3_t tmins, vec3_t tmaxs)
{
	vec3_t	tmp, v;
	int		i, j;
	vec3_t	forward, right, up;

	if (!angles[0] && !angles[1] && !angles[2])
	{
		VectorCopy (mins, tmins);
		VectorCopy (maxs, tmaxs);
		return;
	}

	for (i=0 ; i<3 ; i++)
	{
		tmins[i] = 99999;
		tmaxs[i] = -99999;
	}

	AngleVectors (angles, forward, right, up);

	for ( i = 0; i < 8; i++ )
	{
		if ( i & 1 )
			tmp[0] = mins[0];
		else
			tmp[0] = maxs[0];

		if ( i & 2 )
			tmp[1] = mins[1];
		else
			tmp[1] = maxs[1];

		if ( i & 4 )
			tmp[2] = mins[2];
		else
			tmp[2] = maxs[2];


		VectorScale (forward, tmp[0], v);
		VectorMA (v, -tmp[1], right, v);
		VectorMA (v, tmp[2], up, v);

		for (j=0 ; j<3 ; j++)
		{
			if (v[j] < tmins[j])
				tmins[j] = v[j];
			if (v[j] > tmaxs[j])
				tmaxs[j] = v[j];
		}
	}
}


/*
=============
R_DrawBEntitiesOnList
=============
*/
void R_DrawBEntitiesOnList (void)
{
	int			i, clipflags;
	vec3_t		oldorigin;
	vec3_t		mins, maxs;
	float		minmaxs[6];
	mnode_t		*topnode;

	VectorCopy (tr.modelorg, oldorigin);
	insubmodel = true;
	//r_dlightframecount = r_framecount;

	for( i = 0; i < tr.draw_list->num_edge_entities && !RI.onlyClientDraw; i++ )
	{
		int k;
		RI.currententity = tr.draw_list->edge_entities[i];
		RI.currentmodel = RI.currententity->model;
		if (!RI.currentmodel)
			continue;
		if (RI.currentmodel->nummodelsurfaces == 0)
			continue;	// clip brush only
		//if ( currententity->flags & RF_BEAM )
			//continue;
		if (RI.currentmodel->type != mod_brush)
			continue;
	// see if the bounding box lets us trivially reject, also sets
	// trivial accept status
		RotatedBBox (RI.currentmodel->mins, RI.currentmodel->maxs,
			RI.currententity->angles, mins, maxs);
#if 0
		mins[0] = mins[0] - 100;
		mins[1] = mins[1] - 100;
		mins[2] = mins[2] - 100;
		maxs[0] = maxs[0] + 100;
		maxs[1] = maxs[1] + 100;
		maxs[2] = maxs[2] + 100;
#endif
		VectorAdd (mins, RI.currententity->origin, minmaxs);
		VectorAdd (maxs, RI.currententity->origin, (minmaxs+3));

		clipflags = R_BmodelCheckBBox (minmaxs);
		if (clipflags == BMODEL_FULLY_CLIPPED)
			continue;	// off the edge of the screen
		//clipflags = 0;

		topnode = R_FindTopnode (minmaxs, minmaxs+3);
		if (!topnode)
			continue;	// no part in a visible leaf

		VectorCopy (RI.currententity->origin, r_entorigin);
		VectorSubtract (RI.vieworg, r_entorigin, tr.modelorg);
		//VectorSubtract (r_origin, RI.currententity->origin, modelorg);
		r_pcurrentvertbase = RI.currentmodel->vertexes;

	// FIXME: stop transforming twice
		R_RotateBmodel ();

	// calculate dynamic lighting for bmodel
		// this will reset RI.currententity, do we need this?
		//R_PushDlights ();
		/*if (clmodel->firstmodelsurface != 0)
		{
				for (k=0 ; k<r_refdef2.numDlights ; k++)
				{
						R_MarkLights (&r_refdef2.dlights[k], 1<<k,
								clmodel->nodes + clmodel->firstnode);
				}
		}*/

		// calculate dynamic lighting for bmodel
		for( k = 0; k < MAX_DLIGHTS; k++ )
		{
			dlight_t *l = gEngfuncs.GetDynamicLight( k );
			vec3_t origin_l, oldorigin;

			if( l->die < gpGlobals->time || !l->radius )
				continue;

			VectorCopy( l->origin, oldorigin ); // save lightorigin
			Matrix4x4_CreateFromEntity( RI.objectMatrix, RI.currententity->angles, RI.currententity->origin, 1 );
			Matrix4x4_VectorITransform( RI.objectMatrix, l->origin, origin_l );
			VectorCopy( origin_l, l->origin ); // move light in bmodel space
			R_MarkLights( l, 1<<k, RI.currentmodel->nodes + RI.currentmodel->hulls[0].firstclipnode );
			VectorCopy( oldorigin, l->origin ); // restore lightorigin*/
			//R_MarkLights( l, 1<<k, RI.currentmodel->nodes + RI.currentmodel->hulls[0].firstclipnode );
		}

	//	RI.currentmodel = tr.draw_list->solid_entities[i]->model;
	//	RI.currententity = tr.draw_list->solid_entities[i];
		RI.currententity->topnode = topnode;
//ASSERT( RI.currentmodel == tr.draw_list->solid_entities[i]->model );
		if (topnode->contents >= 0)
		{
		// not a leaf; has to be clipped to the world BSP
			r_clipflags = clipflags;
			R_DrawSolidClippedSubmodelPolygons (RI.currentmodel, topnode);
		}
		else
		{
		// falls entirely in one leaf, so we just put all the
		// edges in the edge list and let 1/z sorting handle
		// drawing order
			//ASSERT( RI.currentmodel == tr.draw_list->solid_entities[i]->model );


			R_DrawSubmodelPolygons (RI.currentmodel, clipflags, topnode);
		}
		RI.currententity->topnode = NULL;

	// put back world rotation and frustum clipping
	// FIXME: R_RotateBmodel should just work off base_vxx
		VectorCopy (RI.base_vpn, RI.vforward);
		VectorCopy (RI.base_vup, RI.vup);
		VectorCopy (RI.base_vright, RI.vright);
		VectorCopy (oldorigin, tr.modelorg);
		R_TransformFrustum ();
	}

	insubmodel = false;
}

extern qboolean alphaspans;
/*
=============
R_DrawBEntitiesOnList
=============
*/
void R_DrawBrushModel(cl_entity_t *pent)
{
	int			i, clipflags;
	vec3_t		oldorigin;
	vec3_t		mins, maxs;
	float		minmaxs[6];
	mnode_t		*topnode;
	int k;
	edge_t	ledges[NUMSTACKEDGES +
				((CACHE_SIZE - 1) / sizeof(edge_t)) + 1];
	surf_t	lsurfs[NUMSTACKSURFACES +
				((CACHE_SIZE - 1) / sizeof(surf_t)) + 1];

	if ( !RI.drawWorld )
		return;

	if (auxedges)
	{
		r_edges = auxedges;
	}
	else
	{
		r_edges =  (edge_t *)
				(((uintptr_t)&ledges[0] + CACHE_SIZE - 1) & ~(CACHE_SIZE - 1));
	}

	if (r_surfsonstack)
	{
		surfaces = (surf_t *)(((uintptr_t)&lsurfs[0] + CACHE_SIZE - 1) & ~(CACHE_SIZE - 1));
		surf_max = &surfaces[r_cnumsurfs];
	// surface 0 doesn't really exist; it's just a dummy because index 0
	// is used to indicate no edge attached to surface
		memset(&surfaces[0], 0, sizeof(surf_t));
		surfaces--;
		R_SurfacePatch ();
	}


	R_BeginEdgeFrame();

	VectorCopy (tr.modelorg, oldorigin);
	insubmodel = true;
	//r_dlightframecount = r_framecount;

		if (!RI.currentmodel)
			return;
		if (RI.currentmodel->nummodelsurfaces == 0)
			return;	// clip brush only
		//if ( currententity->flags & RF_BEAM )
			//continue;
		if (RI.currentmodel->type != mod_brush)
			return;
	// see if the bounding box lets us trivially reject, also sets
	// trivial accept status
		RotatedBBox (RI.currentmodel->mins, RI.currentmodel->maxs,
			RI.currententity->angles, mins, maxs);
#if 0
		mins[0] = mins[0] - 100;
		mins[1] = mins[1] - 100;
		mins[2] = mins[2] - 100;
		maxs[0] = maxs[0] + 100;
		maxs[1] = maxs[1] + 100;
		maxs[2] = maxs[2] + 100;
#endif
		VectorAdd (mins, RI.currententity->origin, minmaxs);
		VectorAdd (maxs, RI.currententity->origin, (minmaxs+3));

		clipflags = R_BmodelCheckBBox (minmaxs);
		if (clipflags == BMODEL_FULLY_CLIPPED)
			return;	// off the edge of the screen
		//clipflags = 0;

		topnode = R_FindTopnode (minmaxs, minmaxs+3);
		if (!topnode)
			return;	// no part in a visible leaf

			alphaspans = true;
		VectorCopy (RI.currententity->origin, r_entorigin);
		VectorSubtract (RI.vieworg, r_entorigin, tr.modelorg);
		//VectorSubtract (r_origin, RI.currententity->origin, modelorg);
		r_pcurrentvertbase = RI.currentmodel->vertexes;

	// FIXME: stop transforming twice
		R_RotateBmodel ();

	// calculate dynamic lighting for bmodel
		// this will reset RI.currententity, do we need this?
		//R_PushDlights ();
		/*if (clmodel->firstmodelsurface != 0)
		{
				for (k=0 ; k<r_refdef2.numDlights ; k++)
				{
						R_MarkLights (&r_refdef2.dlights[k], 1<<k,
								clmodel->nodes + clmodel->firstnode);
				}
		}*/

		// calculate dynamic lighting for bmodel
		for( k = 0; k < MAX_DLIGHTS; k++ )
		{
			dlight_t *l = gEngfuncs.GetDynamicLight( k );
			vec3_t origin_l, oldorigin;

			if( l->die < gpGlobals->time || !l->radius )
				continue;

			VectorCopy( l->origin, oldorigin ); // save lightorigin
			Matrix4x4_CreateFromEntity( RI.objectMatrix, RI.currententity->angles, RI.currententity->origin, 1 );
			Matrix4x4_VectorITransform( RI.objectMatrix, l->origin, origin_l );
			tr.modelviewIdentity = false;
			VectorCopy( origin_l, l->origin ); // move light in bmodel space
			R_MarkLights( l, 1<<k, RI.currentmodel->nodes + RI.currentmodel->hulls[0].firstclipnode );
			VectorCopy( oldorigin, l->origin ); // restore lightorigin*/
			//R_MarkLights( l, 1<<k, RI.currentmodel->nodes + RI.currentmodel->hulls[0].firstclipnode );
		}

	//	RI.currentmodel = tr.draw_list->solid_entities[i]->model;
	//	RI.currententity = tr.draw_list->solid_entities[i];
		RI.currententity->topnode = topnode;
//ASSERT( RI.currentmodel == tr.draw_list->solid_entities[i]->model );
		if (topnode->contents >= 0)
		{
		// not a leaf; has to be clipped to the world BSP
			r_clipflags = clipflags;
			R_DrawSolidClippedSubmodelPolygons (RI.currentmodel, topnode);
		}
		else
		{
		// falls entirely in one leaf, so we just put all the
		// edges in the edge list and let 1/z sorting handle
		// drawing order
			//ASSERT( RI.currentmodel == tr.draw_list->solid_entities[i]->model );


			R_DrawSubmodelPolygons (RI.currentmodel, clipflags, topnode);
		}
		RI.currententity->topnode = NULL;

	// put back world rotation and frustum clipping
	// FIXME: R_RotateBmodel should just work off base_vxx
		VectorCopy (RI.base_vpn, RI.vforward);
		VectorCopy (RI.base_vup, RI.vup);
		VectorCopy (RI.base_vright, RI.vright);
		VectorCopy (oldorigin, tr.modelorg);
		R_TransformFrustum ();


	insubmodel = false;
	R_ScanEdges();
	alphaspans = false;
}

#endif

/*
================
R_EdgeDrawing
================
*/
void R_EdgeDrawing (void)
{
	edge_t	ledges[NUMSTACKEDGES +
				((CACHE_SIZE - 1) / sizeof(edge_t)) + 1];
	surf_t	lsurfs[NUMSTACKSURFACES +
				((CACHE_SIZE - 1) / sizeof(surf_t)) + 1];

	if ( !RI.drawWorld )
		return;

	if (auxedges)
	{
		r_edges = auxedges;
	}
	else
	{
		r_edges =  (edge_t *)
				(((uintptr_t)&ledges[0] + CACHE_SIZE - 1) & ~(CACHE_SIZE - 1));
	}

	if (r_surfsonstack)
	{
		surfaces =  (surf_t *)(((uintptr_t)&lsurfs + CACHE_SIZE - 1) & ~(CACHE_SIZE - 1));
		surf_max = &surfaces[r_cnumsurfs];
		
		// surface 0 doesn't really exist; it's just a dummy because index 0
		// is used to indicate no edge attached to surface

		memset(surfaces, 0, sizeof(surf_t));
		surfaces--;
		R_SurfacePatch ();
	}

	R_BeginEdgeFrame ();

	// this will prepare edges
	R_RenderWorld ();

	// move brushes to separate list to merge with edges?
	R_DrawBEntitiesOnList ();

	// display all edges
	R_ScanEdges ();
}

#if 0
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

	/*if( FBitSet( r_novis->flags, FCVAR_CHANGED ) || tr.fResetVis )
	{
		// force recalc viewleaf
		ClearBits( r_novis->flags, FCVAR_CHANGED );
		tr.fResetVis = false;
		RI.viewleaf = NULL;
	}*/

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
	//if( sw_lockpvs->value ) return;

	RI.oldviewleaf = RI.viewleaf;
	tr.visframecount++;

	if( RI.drawOrtho || !RI.viewleaf || !WORLDMODEL->visdata )
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
#else
/*
===============
R_MarkLeaves
===============
*/
void R_MarkLeaves (void)
{
	byte	*vis;
	mnode_t	*node;
	int		i;

	if (r_oldviewcluster == r_viewcluster && !r_novis->value && r_viewcluster != -1)
		return;

	tr.visframecount++;
	r_oldviewcluster = r_viewcluster;

	gEngfuncs.R_FatPVS( RI.pvsorigin, REFPVS_RADIUS, RI.visbytes, FBitSet( RI.params, RP_OLDVIEWLEAF ), false );
	vis = RI.visbytes;

	for (i = 0; i < WORLDMODEL->numleafs; i++)
	{
		if (vis[i>>3] & (1<<(i&7)))
		{
			node = (mnode_t *) &WORLDMODEL->leafs[i+1];
			do
			{
				if (node->visframe == tr.visframecount)
					break;
				node->visframe = tr.visframecount;
				node = node->parent;
			} while (node);
		}
	}
}



#endif


/*
================
R_RenderScene

R_SetupRefParams must be called right before
================
*/
void GAME_EXPORT R_RenderScene( void )
{
	if( !WORLDMODEL && RI.drawWorld )
		gEngfuncs.Host_Error( "R_RenderView: NULL worldmodel\n" );

	// frametime is valid only for normal pass
	if( RP_NORMALPASS( ))
		tr.frametime = gpGlobals->time -   gpGlobals->oldtime;
	else tr.frametime = 0.0;

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
	//R_Clear( ~0 );

	R_MarkLeaves();
	// R_PushDlights (r_worldmodel); ??
	//R_DrawWorld();
	R_EdgeDrawing ();

	gEngfuncs.CL_ExtraUpdate ();	// don't let sound get messed up if going slow

	R_DrawEntitiesOnList();

//	R_DrawWaterSurfaces();

//	R_EndGL();
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
	// FIXME: this looks ugly. apply the backward gamma changes to the output image
	return false;
#if 0
	switch( cls.scrshot_action )
	{
	case scrshot_normal:
		if( CL_IsDevOverviewMode( ))
			return true;
		return false;
	case scrshot_snapshot:
		if( CL_IsDevOverviewMode( ))
			return true;
		return false;
	case scrshot_plaque:
	case scrshot_savegame:
	case scrshot_envshot:
	case scrshot_skyshot:
	case scrshot_mapshot:
		return true;
	default:
		return false;
	}
#endif
}

/*
===============
R_BeginFrame
===============
*/
void GAME_EXPORT R_BeginFrame( qboolean clearScene )
{
#if 0 // unused
	if( R_DoResetGamma( ))
	{
		gEngfuncs.BuildGammaTable( 1.8f, 0.0f );
		D_FlushCaches( );

		// next frame will be restored gamma
		SetBits( vid_brightness->flags, FCVAR_CHANGED );
		SetBits( vid_gamma->flags, FCVAR_CHANGED );
	}
	else
#endif
	if( FBitSet( vid_gamma->flags, FCVAR_CHANGED ) || FBitSet( vid_brightness->flags, FCVAR_CHANGED ))
	{
		gEngfuncs.BuildGammaTable( vid_gamma->value, vid_brightness->value );

		D_FlushCaches( );
		// next frame will be restored gamma
		ClearBits( vid_brightness->flags, FCVAR_CHANGED );
		ClearBits( vid_gamma->flags, FCVAR_CHANGED );
	}

	R_Set2DMode( true );

	// draw buffer stuff
	//pglDrawBuffer( GL_BACK );

	// update texture parameters
	//if( FBitSet( gl_texture_nearest->flags|gl_lightmap_nearest->flags|gl_texture_anisotropy->flags|gl_texture_lodbias->flags, FCVAR_CHANGED ))
		//R_SetTextureParameters();

	gEngfuncs.CL_ExtraUpdate ();
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
	else RI.drawOrtho = false;

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
			//R_GatherPlayerLight();
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

	RI.viewleaf = NULL;		// force markleafs next frame
}

/*
===============
R_NewMap
===============
*/
void GAME_EXPORT R_NewMap (void)
{
	int i;
	model_t *world = WORLDMODEL;

	r_viewcluster = -1;

	tr.draw_list->num_solid_entities = 0;
	tr.draw_list->num_trans_entities = 0;
	tr.draw_list->num_beam_entities = 0;
	tr.draw_list->num_edge_entities = 0;

	R_ClearDecals(); // clear all level decals
	R_StudioResetPlayerModels();

	r_cnumsurfs = sw_maxsurfs->value;

	if (r_cnumsurfs <= MINSURFACES)
			r_cnumsurfs = MINSURFACES;

	if (r_cnumsurfs > NUMSTACKSURFACES)
	{
			surfaces = Mem_Calloc (r_temppool, r_cnumsurfs * sizeof(surf_t));
			surface_p = surfaces;
			surf_max = &surfaces[r_cnumsurfs];
			r_surfsonstack = false;
	// surface 0 doesn't really exist; it's just a dummy because index 0
	// is used to indicate no edge attached to surface
			surfaces--;
			R_SurfacePatch ();
	}
	else
	{
			r_surfsonstack = true;
	}

	r_numallocatededges = sw_maxedges->value;

	if (r_numallocatededges < MINEDGES)
			r_numallocatededges = MINEDGES;

	if (r_numallocatededges <= NUMSTACKEDGES)
	{
			auxedges = NULL;
	}
	else
	{
			auxedges = Mem_Malloc (r_temppool, r_numallocatededges * sizeof(edge_t) );
	}

	// clear out efrags in case the level hasn't been reloaded
	for( i = 0; i < world->numleafs; i++ )
		world->leafs[i+1].efrags = NULL;

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

		for( sample_pot = 1; sample_pot < tr.sample_size; sample_pot <<= 1, tr.sample_bits++ );
	}

	gEngfuncs.Con_Printf("Map sample size is %d\n", tr.sample_size );

}

/*
================
R_InitTurb
================
*/
void R_InitTurb (void)
{
	int		i;

	for (i=0 ; i<1280 ; i++)
	{
		sintable[i] = AMP + sin(i*3.14159*2/CYCLE)*AMP;
		intsintable[i] = AMP2 + sin(i*3.14159*2/CYCLE)*AMP2;	// AMP2, not 20
		blanktable[i] = 0;			//PGM
	}
}



qboolean GAME_EXPORT R_Init( void )
{
	qboolean glblit = false;

	RETRIEVE_ENGINE_SHARED_CVAR_LIST();

//	sw_aliasstats = ri.Cvar_Get ("sw_polymodelstats", "0", 0);
//	sw_allow_modex = ri.Cvar_Get( "sw_allow_modex", "1", CVAR_ARCHIVE );
	sw_clearcolor = gEngfuncs.Cvar_Get ("sw_clearcolor", "48999", 0, "screen clear color");
	sw_drawflat = gEngfuncs.Cvar_Get ("sw_drawflat", "0", 0, "");
	sw_draworder = gEngfuncs.Cvar_Get ("sw_draworder", "0", 0, "");
	sw_maxedges = gEngfuncs.Cvar_Get ("sw_maxedges", "32", 0, "");
	sw_maxsurfs = gEngfuncs.Cvar_Get ("sw_maxsurfs", "0", 0, "");
	sw_mipscale = gEngfuncs.Cvar_Get ("sw_mipscale", "1", FCVAR_GLCONFIG, "nothing");
	sw_mipcap = gEngfuncs.Cvar_Get( "sw_mipcap", "0", FCVAR_GLCONFIG, "nothing" );
	sw_reportedgeout = gEngfuncs.Cvar_Get ("sw_reportedgeout", "0", 0, "");
	sw_reportsurfout = gEngfuncs.Cvar_Get ("sw_reportsurfout", "0", 0, "");
	sw_stipplealpha = gEngfuncs.Cvar_Get( "sw_stipplealpha", "1", FCVAR_GLCONFIG, "nothing" );
	sw_surfcacheoverride = gEngfuncs.Cvar_Get ("sw_surfcacheoverride", "0", 0, "");
	sw_waterwarp = gEngfuncs.Cvar_Get ("sw_waterwarp", "1", FCVAR_GLCONFIG, "nothing");
	sw_notransbrushes = gEngfuncs.Cvar_Get( "sw_notransbrushes", "0", FCVAR_GLCONFIG, "do not apply transparency to water/glasses (faster)");
	sw_noalphabrushes = gEngfuncs.Cvar_Get( "sw_noalphabrushes", "0", FCVAR_GLCONFIG, "do not draw brush holes (faster)");
	r_traceglow = gEngfuncs.Cvar_Get( "r_traceglow", "1", FCVAR_GLCONFIG, "cull flares behind models" );
#ifndef DISABLE_TEXFILTER
	sw_texfilt = gEngfuncs.Cvar_Get ("sw_texfilt", "0", FCVAR_GLCONFIG, "texture dither");
#endif
//	r_speeds = ri.Cvar_Get ("r_speeds", "0", 0);
	//r_drawworld = ri.Cvar_Get ("r_drawworld", "1", 0);
	//r_dspeeds = ri.Cvar_Get ("r_dspeeds", "0", 0);
//	r_lightlevel = ri.Cvar_Get ("r_lightlevel", "0", 0);
	//r_lerpmodels = ri.Cvar_Get( "r_lerpmodels", "1", 0 );
	r_novis = gEngfuncs.Cvar_Get( "r_novis", "0", 0, "" );

	r_temppool = Mem_AllocPool( "ref_soft zone" );

	glblit = !!gEngfuncs.Sys_CheckParm( "-glblit" );

	// create the window and set up the context
	if( !glblit && !gEngfuncs.R_Init_Video( REF_SOFTWARE )) // request software blitter
	{
		gEngfuncs.R_Free_Video();
		gEngfuncs.Con_Printf("failed to initialize software blitter, fallback to glblit\n");
		glblit = true;
	}

	if( glblit && !gEngfuncs.R_Init_Video( REF_GL )) // request GL context
	{
		gEngfuncs.R_Free_Video();
		return false;
	}

	R_InitBlit( glblit );

	R_InitImages();
	// init draw stack
	tr.draw_list = &tr.draw_stack[0];
	tr.draw_stack_pos = 0;
	qfrustum.view_clipplanes[0].leftedge = true;
	qfrustum.view_clipplanes[1].rightedge = true;
	qfrustum.view_clipplanes[1].leftedge = qfrustum.view_clipplanes[2].leftedge =qfrustum.view_clipplanes[3].leftedge = false;
	qfrustum.view_clipplanes[0].rightedge = qfrustum.view_clipplanes[2].rightedge = qfrustum.view_clipplanes[3].rightedge = false;
	R_StudioInit();
	R_SpriteInit();
	R_InitTurb();

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
	int	blend = 0;
	float	offset, dist;
	vec3_t	tmp;

	offset = ((int)e->index ) * 363.0f; // Use ent index to de-sync these fx

	switch( e->curstate.renderfx )
	{
	case kRenderFxPulseSlowWide:
		blend = e->curstate.renderamt + 0x40 * sin( gpGlobals->time * 2 + offset );
		break;
	case kRenderFxPulseFastWide:
		blend = e->curstate.renderamt + 0x40 * sin( gpGlobals->time * 8 + offset );
		break;
	case kRenderFxPulseSlow:
		blend = e->curstate.renderamt + 0x10 * sin( gpGlobals->time * 2 + offset );
		break;
	case kRenderFxPulseFast:
		blend = e->curstate.renderamt + 0x10 * sin( gpGlobals->time * 8 + offset );
		break;
	case kRenderFxFadeSlow:
		if( RP_NORMALPASS( ))
		{
			if( e->curstate.renderamt > 0 )
				e->curstate.renderamt -= 1;
			else e->curstate.renderamt = 0;
		}
		blend = e->curstate.renderamt;
		break;
	case kRenderFxFadeFast:
		if( RP_NORMALPASS( ))
		{
			if( e->curstate.renderamt > 3 )
				e->curstate.renderamt -= 4;
			else e->curstate.renderamt = 0;
		}
		blend = e->curstate.renderamt;
		break;
	case kRenderFxSolidSlow:
		if( RP_NORMALPASS( ))
		{
			if( e->curstate.renderamt < 255 )
				e->curstate.renderamt += 1;
			else e->curstate.renderamt = 255;
		}
		blend = e->curstate.renderamt;
		break;
	case kRenderFxSolidFast:
		if( RP_NORMALPASS( ))
		{
			if( e->curstate.renderamt < 252 )
				e->curstate.renderamt += 4;
			else e->curstate.renderamt = 255;
		}
		blend = e->curstate.renderamt;
		break;
	case kRenderFxStrobeSlow:
		blend = 20 * sin( gpGlobals->time * 4 + offset );
		if( blend < 0 ) blend = 0;
		else blend = e->curstate.renderamt;
		break;
	case kRenderFxStrobeFast:
		blend = 20 * sin( gpGlobals->time * 16 + offset );
		if( blend < 0 ) blend = 0;
		else blend = e->curstate.renderamt;
		break;
	case kRenderFxStrobeFaster:
		blend = 20 * sin( gpGlobals->time * 36 + offset );
		if( blend < 0 ) blend = 0;
		else blend = e->curstate.renderamt;
		break;
	case kRenderFxFlickerSlow:
		blend = 20 * (sin( gpGlobals->time * 2 ) + sin( gpGlobals->time * 17 + offset ));
		if( blend < 0 ) blend = 0;
		else blend = e->curstate.renderamt;
		break;
	case kRenderFxFlickerFast:
		blend = 20 * (sin( gpGlobals->time * 16 ) + sin( gpGlobals->time * 23 + offset ));
		if( blend < 0 ) blend = 0;
		else blend = e->curstate.renderamt;
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
			if( dist <= 100 ) blend = e->curstate.renderamt;
			else blend = (int) ((1.0f - ( dist - 100 ) * ( 1.0f / 400.0f )) * e->curstate.renderamt );
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

