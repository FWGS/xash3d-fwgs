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
#if XASH_GAMECUBE
#include "mem_gamecube.h"
int GC_GetNewGameViewCluster( void );
qboolean GC_HasNewGameCachedVis( void );
qboolean GC_ApplyNewGameCachedVis( int visframe );
void GC_ApplyNewGameSurfVis( int surf_frame );
#endif
// #include "beamdef.h"
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
#if XASH_GAMECUBE
	CVAR_DEFINE_AUTO( sw_surfcacheoverride, "262144", FCVAR_GLCONFIG, "software surface cache bytes (GameCube cap applies)" );
#else
CVAR_DEFINE_AUTO( sw_surfcacheoverride, "0", 0, "" );
#endif
static CVAR_DEFINE_AUTO( sw_waterwarp, "1", FCVAR_GLCONFIG, "nothing" );
static CVAR_DEFINE_AUTO( sw_notransbrushes, "0", FCVAR_GLCONFIG, "do not apply transparency to water/glasses (faster)" );
CVAR_DEFINE_AUTO( sw_noalphabrushes, "0", FCVAR_GLCONFIG, "do not draw brush holes (faster)" );
CVAR_DEFINE_AUTO( r_traceglow, "0", FCVAR_GLCONFIG, "cull flares behind models" );
CVAR_DEFINE_AUTO( sw_texfilt, "0", FCVAR_GLCONFIG, "texture dither" );
static CVAR_DEFINE_AUTO( r_novis, "0", 0, "" );

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

#if XASH_GAMECUBE
/* Reduced edge/surface/span budgets for low-res world probe: static BSS, not heap.
 * MEM1 is too fragmented after New Game connect for reliable malloc here.
 * Sized for world + opaque brush movers (tram/doors) in the shared edge pass. */
#define GC_PROBE_NUMEDGES 768
#define GC_PROBE_NUMSURFS 384
#define GC_PROBE_NUMSPANS 768

static byte gc_probe_edges_store[( GC_PROBE_NUMEDGES + 2 ) * sizeof( edge_t ) + CACHE_SIZE]
	__attribute__((aligned( 32 )));
static byte gc_probe_surfaces_store[( GC_PROBE_NUMSURFS + 1 ) * sizeof( surf_t ) + CACHE_SIZE]
	__attribute__((aligned( 32 )));
static byte gc_probe_spans_store[GC_PROBE_NUMSPANS * sizeof( espan_t ) + CACHE_SIZE]
	__attribute__((aligned( 32 )));

static edge_t *gc_probe_edges;
static surf_t *gc_probe_surfaces;
static byte   *gc_probe_spans;
static int     gc_probe_numedges;
static int     gc_probe_numsurfs;
static int     gc_probe_numspans;

qboolean R_GcmapEnsureWorldRenderScratch( void )
{
	if( gc_probe_edges && gc_probe_surfaces && gc_probe_spans &&
		gc_probe_numedges > 0 && gc_probe_numsurfs > 0 && gc_probe_numspans > 0 )
		return true;

	gc_probe_edges = (edge_t *)gc_probe_edges_store;
	gc_probe_surfaces = (surf_t *)gc_probe_surfaces_store;
	gc_probe_spans = gc_probe_spans_store;
	gc_probe_numedges = GC_PROBE_NUMEDGES;
	gc_probe_numsurfs = GC_PROBE_NUMSURFS;
	gc_probe_numspans = GC_PROBE_NUMSPANS;
	gEngfuncs.Con_Reportf( "Xash3D GameCube: gcmap world-render scratch ready edges=%d surfs=%d spans=%d (static)\n",
		gc_probe_numedges, gc_probe_numsurfs, gc_probe_numspans );
	return true;
}

espan_t *R_GcmapProbeSpanBase( int *maxspans )
{
	if( !gc_probe_spans )
		return NULL;
	if( maxspans )
		*maxspans = gc_probe_numspans;
	return (espan_t *)(((uintptr_t)gc_probe_spans + CACHE_SIZE - 1 ) & ~( CACHE_SIZE - 1 ));
}
#endif

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
	vec3_t vecLen, org;
	float  dist1, dist2;

	cl_entity_t *ent1 = (cl_entity_t *)*a;
	cl_entity_t *ent2 = (cl_entity_t *)*b;
	int rendermode1 = R_GetEntityRenderMode( ent1 );
	int rendermode2 = R_GetEntityRenderMode( ent2 );

	// sort by distance
	if(( ent1->model && ent1->model->type != mod_brush ) || rendermode1 != kRenderTransAlpha )
	{
		VectorAverage( ent1->model->mins, ent1->model->maxs, org );
		VectorAdd( ent1->origin, org, org );
		VectorSubtract( RI.rvp.vieworigin, org, vecLen );
		dist1 = DotProduct( vecLen, vecLen );
	}
	else
		dist1 = 1000000000;

	if(( ent1->model && ent2->model->type != mod_brush ) || rendermode2 != kRenderTransAlpha )
	{
		VectorAverage( ent2->model->mins, ent2->model->maxs, org );
		VectorAdd( ent2->origin, org, org );
		VectorSubtract( RI.rvp.vieworigin, org, vecLen );
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

	if( !point || !screen )
		return true;

	Matrix4x4_Copy( worldToScreen, RI.worldviewProjectionMatrix );
	screen[0] = worldToScreen[0][0] * point[0] + worldToScreen[0][1] * point[1] + worldToScreen[0][2] * point[2] + worldToScreen[0][3];
	screen[1] = worldToScreen[1][0] * point[0] + worldToScreen[1][1] * point[1] + worldToScreen[1][2] * point[2] + worldToScreen[1][3];
	float w = worldToScreen[3][0] * point[0] + worldToScreen[3][1] * point[1] + worldToScreen[3][2] * point[2] + worldToScreen[3][3];
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

	if( !point || !screen )
		return;

	Matrix4x4_Invert_Full( screenToWorld, RI.worldviewProjectionMatrix );

	point[0] = screen[0] * screenToWorld[0][0] + screen[1] * screenToWorld[0][1] + screen[2] * screenToWorld[0][2] + screenToWorld[0][3];
	point[1] = screen[0] * screenToWorld[1][0] + screen[1] * screenToWorld[1][1] + screen[2] * screenToWorld[1][2] + screenToWorld[1][3];
	point[2] = screen[0] * screenToWorld[2][0] + screen[1] * screenToWorld[2][1] + screen[2] * screenToWorld[2][2] + screenToWorld[2][3];
	float w = screen[0] * screenToWorld[3][0] + screen[1] * screenToWorld[3][1] + screen[2] * screenToWorld[3][2] + screenToWorld[3][3];
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

	if( FBitSet( clent->curstate.effects, EF_NODRAW ))
		return false; // done

	if( !R_ModelOpaque( clent->curstate.rendermode ) && CL_FxBlend( clent ) <= 0 )
		return true; // invisible

	if( type == ET_FRAGMENTED )
		r_stats.c_client_ents++;

	if( type == ET_BEAM )
	{
		if( tr.draw_list->num_beam_entities >= MAX_VISIBLE_PACKET )
		{
			gEngfuncs.Con_Printf( S_ERROR "Too many beams %d!\n", tr.draw_list->num_beam_entities );
			return false;
		}

		tr.draw_list->beam_entities[tr.draw_list->num_beam_entities] = clent;
		tr.draw_list->num_beam_entities++;

		return true;
	}
	else if( R_OpaqueEntity( clent ))
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

// =============================================================================
/*
===============
R_GetFarClip
===============
*/
static float R_GetFarClip( void )
{
	if( WORLDMODEL && FBitSet( RI.rvp.flags, RF_DRAW_WORLD ))
		return gp_movevars->zmax * 1.73f;
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
	AngleVectors( RI.rvp.viewangles, RI.vforward, RI.vright, RI.vup );

	{
		VectorCopy( RI.rvp.vieworigin, RI.cullorigin );
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

	RI.farClip = R_GetFarClip();

	zNear = 4.0f;
	zFar = Q_max( 256.0f, RI.farClip );

	yMax = zNear * tan( RI.rvp.fov_y * M_PI_F / 360.0f );
	yMin = -yMax;

	xMax = zNear * tan( RI.rvp.fov_x * M_PI_F / 360.0f );
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
	Matrix4x4_ConcatRotate( m, -RI.rvp.viewangles[2], 1, 0, 0 );
	Matrix4x4_ConcatRotate( m, -RI.rvp.viewangles[0], 0, 1, 0 );
	Matrix4x4_ConcatRotate( m, -RI.rvp.viewangles[1], 0, 0, 1 );
	Matrix4x4_ConcatTranslate( m, -RI.rvp.vieworigin[0], -RI.rvp.vieworigin[1], -RI.rvp.vieworigin[2] );
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
#if XASH_GAMECUBE
	/* G83: BSP scratch is corrupt by first world present; use prepare-time cluster. */
	if( gEngfuncs.Sys_CheckParm( "-gcnewgame" ) && GC_GetNewGameViewCluster() >= 0 )
	{
		RI.viewleaf = NULL;
		return;
	}
#endif
	RI.viewleaf = gEngfuncs.Mod_PointInLeaf( RI.rvp.vieworigin, WORLDMODEL->nodes, WORLDMODEL );
}

/*
===============
R_SetupFrame
===============
*/
static void R_SetupFrame( void )
{
	// setup viewplane dist
	RI.viewplanedist = DotProduct( RI.rvp.vieworigin, RI.vforward );

#if XASH_GAMECUBE
	// G24a: skip translucent sort in low-memory smoke; New Game low-res keeps it.
	{
		int quality = GC_GetVisualQuality();
		if( tr.framecount <= 1 )
			gEngfuncs.Con_Reportf( "Xash3D GameCube: R_SetupFrame quality=%d trans=%u\n",
				quality, tr.draw_list ? tr.draw_list->num_trans_entities : 0 );
		/* Bound qsort: a stale/corrupt count hangs the first New Game world frame. */
		if(( quality > 0 || GC_UseLowResWorldProbe() )
			&& tr.draw_list
			&& tr.draw_list->num_trans_entities > 0
			&& tr.draw_list->num_trans_entities <= MAX_VISIBLE_PACKET )
		{
			qsort( tr.draw_list->trans_entities, tr.draw_list->num_trans_entities, sizeof( cl_entity_t * ), (void *)R_TransEntityCompare );
		}
		else if( tr.draw_list && tr.draw_list->num_trans_entities > MAX_VISIBLE_PACKET )
		{
			gEngfuncs.Con_Reportf( "Xash3D GameCube: R_SetupFrame dropping bad trans count %u\n",
				tr.draw_list->num_trans_entities );
			tr.draw_list->num_trans_entities = 0;
		}
	}
#else
	qsort( tr.draw_list->trans_entities, tr.draw_list->num_trans_entities, sizeof( cl_entity_t * ), (void *)R_TransEntityCompare );
#endif

	if( tr.framecount <= 1 && gEngfuncs.Sys_CheckParm( "-gcnewgame" ))
		gEngfuncs.Con_Reportf( "Xash3D GameCube: R_SetupFrame after sort\n" );

	// current viewleaf
	if( FBitSet( RI.rvp.flags, RF_DRAW_WORLD ))
	{
#if XASH_GAMECUBE
		if( tr.framecount <= 1 && gEngfuncs.Sys_CheckParm( "-gcnewgame" ))
			gEngfuncs.Con_Reportf( "Xash3D GameCube: PointInLeaf begin\n" );
#endif
		R_FindViewLeaf();
#if XASH_GAMECUBE
		if( gEngfuncs.Sys_CheckParm( "-gcnewgame" ) && tr.framecount <= 1 )
		{
			gEngfuncs.Con_Reportf( "Xash3D GameCube: PointInLeaf cluster=%d contents=%d (prepare=%d)\n",
				RI.viewleaf ? RI.viewleaf->cluster : GC_GetNewGameViewCluster(),
				RI.viewleaf ? RI.viewleaf->contents : 0,
				GC_GetNewGameViewCluster() );
		}
#endif
	}

	if( tr.framecount <= 1 && gEngfuncs.Sys_CheckParm( "-gcnewgame" ))
		gEngfuncs.Con_Reportf( "Xash3D GameCube: R_SetupFrame after viewleaf\n" );

	// setup twice until globals fully refactored
	R_SetupFrameQ();

	if( tr.framecount <= 1 && gEngfuncs.Sys_CheckParm( "-gcnewgame" ))
		gEngfuncs.Con_Reportf( "Xash3D GameCube: R_SetupFrame ready\n" );
}

/*
=============
R_DrawStudioEntitiesLowRes

New Game low-res: solid studio/sprites, translucent brushes (glass),
studio/sprites, and EFX particles. Affine/alpha spans write display RGB565.
=============
*/
#if XASH_GAMECUBE
static void R_DrawStudioEntitiesLowRes( void )
{
	unsigned drawn = 0;
	unsigned sprites = 0;
	unsigned brushes = 0;
	const unsigned max_studio = 4;
	const unsigned max_sprites = 2;
	/* Cap 3 outside silent G36 windows (budget still skips after one draw). */
	const unsigned max_brushes = 3;
	qboolean drew_view = false;
	static qboolean gc_fx_marker_logged;
	static qboolean gc_trans_brush_marker_logged;
	const qboolean skip_fx = ( GC_GetVisualQuality() <= 0 );

	tr.blend = 1.0f;
	d_pdrawspans = R_PolysetFillSpans8;
	GL_SetRenderMode( kRenderNormal );
	d_gc_span_rgb565 = true;

	for( int i = 0; i < tr.draw_list->num_solid_entities && !FBitSet( RI.rvp.flags, RF_ONLY_CLIENTDRAW ); i++ )
	{
		RI.currententity = tr.draw_list->solid_entities[i];
		RI.currentmodel = RI.currententity->model;

		if( !RI.currentmodel )
			continue;
		if( RI.currentmodel->type == mod_studio )
		{
			if( drawn >= max_studio )
				continue;
			R_SetUpWorldTransform();
			R_DrawStudioModel( RI.currententity );
			drawn++;
		}
		else if( RI.currentmodel->type == mod_sprite )
		{
			if( sprites >= max_sprites )
				continue;
			R_SetUpWorldTransform();
			R_DrawSpriteModel( RI.currententity );
			sprites++;
		}
	}

	/* c0a0 tram spawn often has no studio ents in PVS — force one promoted
	 * world mesh so the low-res studio path is exercised beyond viewmodels.
	 * During G36 sample windows draw it once, then skip (saves ~present ms). */
	if( drawn == 0 && gEngfuncs.Sys_CheckParm( "-gcnewgame" )
		&& !FBitSet( RI.rvp.flags, RF_ONLY_CLIENTDRAW ))
	{
		static qboolean force_drawn_once;
		extern qboolean GC_IsFrameBudgetProbeActive( void );

		if( !( GC_IsFrameBudgetProbeActive() && force_drawn_once ))
		{
		model_t *gm = gEngfuncs.Mod_ForName( "models/roach.mdl", false, false );

		if( gm && gm->type == mod_studio && gm->cache.data )
		{
			studiohdr_t *hdr = (studiohdr_t *)gm->cache.data;

			/* Stub / empty promotes have no bodyparts — skip false studio=1. */
			if( hdr && hdr->numbodyparts > 0 )
			{
				static cl_entity_t probe;
				static qboolean probe_logged;
				vec3_t origin;

				memset( &probe, 0, sizeof( probe ));
				probe.model = gm;
				VectorCopy( RI.rvp.vieworigin, origin );
				VectorMA( origin, 72.0f, RI.vforward, origin );
				origin[2] -= 16.0f;
				VectorCopy( origin, probe.origin );
				VectorCopy( origin, probe.curstate.origin );
				VectorCopy( RI.rvp.viewangles, probe.angles );
				probe.angles[YAW] += 180.0f;
				VectorCopy( probe.angles, probe.curstate.angles );
				probe.curstate.animtime = (float)gp_cl->time;
				probe.curstate.framerate = 1.0f;
				probe.curstate.sequence = 0;
				probe.curstate.rendermode = kRenderNormal;
				probe.curstate.solid = SOLID_NOT;

				RI.currententity = &probe;
				RI.currentmodel = gm;
				R_SetUpWorldTransform();
				R_DrawStudioModel( &probe );
				drawn++;
				force_drawn_once = true;
				if( !probe_logged )
				{
					gEngfuncs.Con_Reportf( "Xash3D GameCube: low-res forced world studio models/roach.mdl\n" );
					probe_logged = true;
				}
			}
		}
		}
	}

	/* View weapon when present — tram intro often has none yet.
	 * Skip during silent G36 windows after the first draw. */
	if( !FBitSet( RI.rvp.flags, RF_ONLY_CLIENTDRAW )
	    && tr.viewent && tr.viewent->model && tr.viewent->model->type == mod_studio
	    && r_drawviewmodel && r_drawviewmodel->value )
	{
		static qboolean view_drawn_once;
		extern qboolean GC_IsFrameBudgetProbeActive( void );

		if( !( GC_IsFrameBudgetProbeActive() && view_drawn_once ))
		{
			R_SetUpWorldTransform();
			R_DrawViewModel();
			drew_view = true;
			view_drawn_once = true;
		}
	}

	/* Particles / tempents into the RGB565 world buffer. */
	if( !skip_fx && !FBitSet( RI.rvp.flags, RF_ONLY_CLIENTDRAW ))
	{
		gEngfuncs.CL_DrawEFX( tr.frametime, false );
		if( !gc_fx_marker_logged )
		{
			gEngfuncs.Con_Reportf( "Xash3D GameCube: RGB565 particles/EFX active\n" );
			gc_fx_marker_logged = true;
		}
	}

	/* Bounded translucent brushes (glass/grates) + studio/sprites.
	 * Skip after one draw during silent G36 windows (160×120 fill is tight). */
	{
		static qboolean trans_brush_once;
		extern qboolean GC_IsFrameBudgetProbeActive( void );
		const qboolean skip_trans_brushes =
			( GC_IsFrameBudgetProbeActive() && trans_brush_once );

	d_pdrawspans = R_PolysetDrawSpans8_33;
	for( int i = 0; i < tr.draw_list->num_trans_entities && !FBitSet( RI.rvp.flags, RF_ONLY_CLIENTDRAW ); i++ )
	{
		RI.currententity = tr.draw_list->trans_entities[i];
		RI.currentmodel = RI.currententity->model;

		if( !RI.currentmodel )
			continue;
		if( RI.currententity->curstate.rendermode != kRenderNormal )
			tr.blend = CL_FxBlend( RI.currententity ) / 255.0f;
		else
			tr.blend = 1.0f;
		if( tr.blend <= 0.0f )
			continue;

		if( RI.currentmodel->type == mod_brush )
		{
			if( skip_trans_brushes || brushes >= max_brushes )
				continue;
			R_SetUpWorldTransform();
			R_DrawBrushModel( RI.currententity );
			brushes++;
			trans_brush_once = true;
		}
		else if( RI.currentmodel->type == mod_studio )
		{
			if( drawn >= max_studio )
				continue;
			R_SetUpWorldTransform();
			R_DrawStudioModel( RI.currententity );
			drawn++;
		}
		else if( RI.currentmodel->type == mod_sprite )
		{
			if( sprites >= max_sprites )
				continue;
			R_SetUpWorldTransform();
			R_DrawSpriteModel( RI.currententity );
			sprites++;
		}
	}
	}

	if( brushes && !gc_trans_brush_marker_logged )
	{
		gEngfuncs.Con_Reportf( "Xash3D GameCube: RGB565 translucent brushes active\n" );
		gc_trans_brush_marker_logged = true;
	}

	if( !skip_fx && !FBitSet( RI.rvp.flags, RF_ONLY_CLIENTDRAW ))
		gEngfuncs.CL_DrawEFX( tr.frametime, true );

	d_gc_span_rgb565 = false;
	/* OSReport is expensive on Dolphin — do not log every frame just because
	 * studio/viewmodel stayed non-zero (force-draw + crowbar are steady). */
	{
		static int last_ents_log_frame = -999;

		if( tr.framecount <= 4 || ( tr.framecount - last_ents_log_frame ) >= 64 )
		{
			gEngfuncs.Con_Reportf( "Xash3D GameCube: low-res ents studio=%u sprites=%u brushes=%u solids=%u trans=%u viewmodel=%d%s%s frame=%d\n",
				drawn, sprites, brushes, tr.draw_list->num_solid_entities, tr.draw_list->num_trans_entities,
				drew_view ? 1 : 0,
				( tr.viewent && tr.viewent->model ) ? " vm=" : "",
				( tr.viewent && tr.viewent->model ) ? tr.viewent->model->name : "",
				tr.framecount );
			last_ents_log_frame = tr.framecount;
		}
	}
}
#endif

/*
=============
R_DrawEntitiesOnList
=============
*/
static void R_DrawEntitiesOnList( void )
{
	// extern int d_aflatcolor;
	// d_aflatcolor = 0;
	tr.blend = 1.0f;
//	GL_CheckForErrors();
	// RI.currententity = CL_GetEntityByIndex(0);
	d_pdrawspans = R_PolysetFillSpans8;
	GL_SetRenderMode( kRenderNormal );
	// first draw solid entities
	for( int i = 0; i < tr.draw_list->num_solid_entities && !FBitSet( RI.rvp.flags, RF_ONLY_CLIENTDRAW ); i++ )
	{
		RI.currententity = tr.draw_list->solid_entities[i];
		RI.currentmodel = RI.currententity->model;
		// d_aflatcolor += 500;

		if( !RI.currentmodel && RI.currententity->player && !FBitSet( RI.rvp.flags, RF_DRAW_WORLD ))
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
		default:
			break;
		}
	}

	R_SetUpWorldTransform();
	// draw sprites seperately, because of alpha blending
	for( int i = 0; i < tr.draw_list->num_solid_entities && !FBitSet( RI.rvp.flags, RF_ONLY_CLIENTDRAW ); i++ )
	{
		RI.currententity = tr.draw_list->solid_entities[i];
		RI.currentmodel = RI.currententity->model;

		if( !RI.currentmodel && RI.currententity->player && !FBitSet( RI.rvp.flags, RF_DRAW_WORLD ))
			continue;

		Assert( RI.currententity != NULL );
		Assert( RI.currentmodel != NULL );

		switch( RI.currentmodel->type )
		{
		case mod_sprite:
			R_DrawSpriteModel( RI.currententity );
			break;
		}
	}

	if( !FBitSet( RI.rvp.flags, RF_ONLY_CLIENTDRAW ))
	{
		gEngfuncs.CL_DrawEFX( tr.frametime, false );
	}

	if( FBitSet( RI.rvp.flags, RF_DRAW_WORLD ))
		gEngfuncs.pfnDrawNormalTriangles();

	d_pdrawspans = R_PolysetDrawSpans8_33;

#if XASH_GAMECUBE
		if( !GC_IsLowMemoryMode() )
		{
			// then draw translucent entities
			for( int i = 0; i < tr.draw_list->num_trans_entities && !FBitSet( RI.rvp.flags, RF_ONLY_CLIENTDRAW ); i++ )
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

				if( !RI.currentmodel && RI.currententity->player && !FBitSet( RI.rvp.flags, RF_DRAW_WORLD ))
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
		}
#else
		// then draw translucent entities
		for( int i = 0; i < tr.draw_list->num_trans_entities && !FBitSet( RI.rvp.flags, RF_ONLY_CLIENTDRAW ); i++ )
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

			if( !RI.currentmodel && RI.currententity->player && !FBitSet( RI.rvp.flags, RF_DRAW_WORLD ))
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
#endif

	if( FBitSet( RI.rvp.flags, RF_DRAW_WORLD ))
		gEngfuncs.pfnDrawTransparentTriangles();

	if( !FBitSet( RI.rvp.flags, RF_ONLY_CLIENTDRAW ))
	{
		R_AllowFog( false );
		gEngfuncs.CL_DrawEFX( tr.frametime, true );
		R_AllowFog( true );
	}

	GL_SetRenderMode( kRenderNormal );
	R_SetUpWorldTransform();
	if( !FBitSet( RI.rvp.flags, RF_ONLY_CLIENTDRAW ))
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
	vec3_t acceptpt, rejectpt;
	float  d;

	int clipflags = 0;

	for( int i = 0; i < 4; i++ )
	{
		// generate accept and reject points
		// FIXME: do with fast look-ups or integer tests based on the sign bit
		// of the floating point values

		int *pindex = qfrustum.pfrustum_indexes[i];

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
	mnode_t *node = WORLDMODEL->nodes;

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

		mplane_t *splitplane = node->plane;
		int      sides = BOX_ON_PLANE_SIDE( mins, maxs, splitplane );

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
	vec3_t forward, right, up;

	if( !angles[0] && !angles[1] && !angles[2] )
	{
		VectorCopy( mins, tmins );
		VectorCopy( maxs, tmaxs );
		return;
	}

	for( int i = 0; i < 3; i++ )
	{
		tmins[i] = 99999;
		tmaxs[i] = -99999;
	}

	AngleVectors( angles, forward, right, up );

	for( int i = 0; i < 8; i++ )
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

		for( int j = 0; j < 3; j++ )
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
#if XASH_GAMECUBE
	/* Low-memory smoke skips bmodels, but the low-res New Game / world-render
	 * probe already owns heap/static edge tables — merge brush movers there. */
	if( GC_IsLowMemoryMode() && !GC_UseLowResWorldProbe() )
	{
		gEngfuncs.Con_Reportf( "Xash3D GameCube: R_DrawBEntitiesOnList skipping (quality=0)\n" );
		return;
	}
#endif

	vec3_t mins, maxs;
	float  minmaxs[6];

	vec3_t oldorigin = Vec3( tr.modelorg );
	insubmodel = true;

	{
		int edge_count = tr.draw_list->num_edge_entities;
		int drawn_bmodels = 0;
#if XASH_GAMECUBE
		/* New Game q0: cap opaque movers so tram/doors return without blowing
		 * the G36 present window (full edge list can be large on c0a0).
		 * Silent budget windows still draw this path only once. */
		if( gEngfuncs.Sys_CheckParm( "-gcnewgame" ) && GC_GetVisualQuality() <= 0
			&& edge_count > 6 )
			edge_count = 6;
#endif

	for( int i = 0; i < edge_count && !FBitSet( RI.rvp.flags, RF_ONLY_CLIENTDRAW ); i++ )
	{
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

		int clipflags = R_BmodelCheckBBox( minmaxs );
		if( clipflags == BMODEL_FULLY_CLIPPED )
			continue; // off the edge of the screen
		// clipflags = 0;

		mnode_t *topnode = R_FindTopnode( minmaxs, minmaxs + 3 );
		if( !topnode )
			continue; // no part in a visible leaf

		VectorCopy( RI.currententity->origin, r_entorigin );
		VectorSubtract( RI.rvp.vieworigin, r_entorigin, tr.modelorg );
		// VectorSubtract (r_origin, RI.currententity->origin, modelorg);
		r_pcurrentvertbase = RI.currentmodel->vertexes;

		// FIXME: stop transforming twice
		R_RotateBmodel();

		// calculate dynamic lighting for bmodel
		Matrix4x4_CreateFromEntity( RI.objectMatrix, RI.currententity->angles, RI.currententity->origin, 1 );
		R_PushDlightsForBmodel( RI.currentmodel, tr.dlightframecount, RI.objectMatrix );

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
		drawn_bmodels++;

		// put back world rotation and frustum clipping
		// FIXME: R_RotateBmodel should just work off base_vxx
		VectorCopy( RI.base_vpn, RI.vforward );
		VectorCopy( RI.base_vup, RI.vup );
		VectorCopy( RI.base_vright, RI.vright );
		VectorCopy( oldorigin, tr.modelorg );
		R_TransformFrustum();
	}

#if XASH_GAMECUBE
	if( tr.framecount <= 1 && gEngfuncs.Sys_CheckParm( "-gcnewgame" )
		&& GC_GetVisualQuality() <= 0 )
		gEngfuncs.Con_Reportf( "Xash3D GameCube: low-res opaque bmodels drawn=%d of %u\n",
			drawn_bmodels, tr.draw_list->num_edge_entities );
#endif
	}

	insubmodel = false;
}

extern qboolean alphaspans;

#if XASH_GAMECUBE
/*
=============
R_DrawBrushModelProbe

Translucent brush ents on the New Game / world-render probe path.
Reuses static probe edge BSS — never NUMSTACKEDGES on the GameCube stack.
=============
*/
static void R_DrawBrushModelProbe( cl_entity_t *pent )
{
	vec3_t mins, maxs;
	float  minmaxs[6];
	vec3_t oldorigin;
	mnode_t *topnode;
	int clipflags;

	if( !FBitSet( RI.rvp.flags, RF_DRAW_WORLD ))
		return;
	if( !R_GcmapEnsureWorldRenderScratch() )
		return;

	r_numallocatededges = gc_probe_numedges;
	r_cnumsurfs = gc_probe_numsurfs;
	r_edges = (edge_t *)(((uintptr_t)gc_probe_edges + CACHE_SIZE - 1 ) & ~( CACHE_SIZE - 1 ));
	surfaces = (surf_t *)(((uintptr_t)gc_probe_surfaces + CACHE_SIZE - 1 ) & ~( CACHE_SIZE - 1 ));
	surf_max = &surfaces[r_cnumsurfs + 1];
	memset( &surfaces[1], 0, sizeof( surf_t ));
	R_SurfacePatch();

	R_BeginEdgeFrame();

	VectorCopy( tr.modelorg, oldorigin );
	insubmodel = true;
	RI.currententity = pent;
	RI.currentmodel = pent->model;

	if( !RI.currentmodel || RI.currentmodel->nummodelsurfaces == 0
	    || RI.currentmodel->type != mod_brush )
	{
		insubmodel = false;
		return;
	}

	RotatedBBox( RI.currentmodel->mins, RI.currentmodel->maxs,
		     RI.currententity->angles, mins, maxs );
	VectorAdd( mins, RI.currententity->origin, minmaxs );
	VectorAdd( maxs, RI.currententity->origin, ( minmaxs + 3 ));

	clipflags = R_BmodelCheckBBox( minmaxs );
	if( clipflags == BMODEL_FULLY_CLIPPED )
	{
		insubmodel = false;
		return;
	}

	topnode = R_FindTopnode( minmaxs, minmaxs + 3 );
	if( !topnode )
	{
		insubmodel = false;
		return;
	}

	alphaspans = true;
	VectorCopy( RI.currententity->origin, r_entorigin );
	VectorSubtract( RI.rvp.vieworigin, r_entorigin, tr.modelorg );
	r_pcurrentvertbase = RI.currentmodel->vertexes;
	R_RotateBmodel();

	Matrix4x4_CreateFromEntity( RI.objectMatrix, RI.currententity->angles, RI.currententity->origin, 1 );
	R_PushDlightsForBmodel( RI.currentmodel, tr.dlightframecount, RI.objectMatrix );
	tr.modelviewIdentity = false;

	RI.currententity->topnode = topnode;
	if( topnode->contents >= 0 )
	{
		r_clipflags = clipflags;
		R_DrawSolidClippedSubmodelPolygons( RI.currentmodel, topnode );
	}
	else
		R_DrawSubmodelPolygons( RI.currentmodel, clipflags, topnode );
	RI.currententity->topnode = NULL;

	VectorCopy( RI.base_vpn, RI.vforward );
	VectorCopy( RI.base_vup, RI.vup );
	VectorCopy( RI.base_vright, RI.vright );
	VectorCopy( oldorigin, tr.modelorg );
	R_TransformFrustum();

	insubmodel = false;
	R_ScanEdges();
	alphaspans = false;
}
#endif

/*
=============
R_DrawBrushModel
=============
*/
void R_DrawBrushModel( cl_entity_t *pent )
{
#if XASH_GAMECUBE
	/* Probe path: no stack edge tables (NUMSTACKEDGES would blow MEM1). */
	if( GC_UseLowResWorldProbe() )
	{
		R_DrawBrushModelProbe( pent );
		return;
	}
#endif

	vec3_t mins, maxs;
	float  minmaxs[6];
	edge_t ledges[NUMSTACKEDGES
		      + (( CACHE_SIZE - 1 ) / sizeof( edge_t )) + 1];
	surf_t lsurfs[NUMSTACKSURFACES
		      + (( CACHE_SIZE - 1 ) / sizeof( surf_t )) + 1];

	if( !FBitSet( RI.rvp.flags, RF_DRAW_WORLD ))
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

	vec3_t oldorigin = Vec3( tr.modelorg );
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

	int clipflags = R_BmodelCheckBBox( minmaxs );
	if( clipflags == BMODEL_FULLY_CLIPPED )
		return;         // off the edge of the screen
	// clipflags = 0;

	mnode_t *topnode = R_FindTopnode( minmaxs, minmaxs + 3 );
	if( !topnode )
		return;         // no part in a visible leaf

	alphaspans = true;
	VectorCopy( RI.currententity->origin, r_entorigin );
	VectorSubtract( RI.rvp.vieworigin, r_entorigin, tr.modelorg );
	// VectorSubtract (r_origin, RI.currententity->origin, modelorg);
	r_pcurrentvertbase = RI.currentmodel->vertexes;

	// FIXME: stop transforming twice
	R_RotateBmodel();

	// calculate dynamic lighting for bmodel
	Matrix4x4_CreateFromEntity( RI.objectMatrix, RI.currententity->angles, RI.currententity->origin, 1 );
	R_PushDlightsForBmodel( RI.currentmodel, tr.dlightframecount, RI.objectMatrix );
	tr.modelviewIdentity = false;

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
#if XASH_GAMECUBE
static void R_EdgeDrawingGcmapProbe( void )
{
	if( !FBitSet( RI.rvp.flags, RF_DRAW_WORLD ))
		return;

	if( !R_GcmapEnsureWorldRenderScratch() )
		return;

	r_numallocatededges = gc_probe_numedges;
	r_cnumsurfs = gc_probe_numsurfs;
	r_edges = (edge_t *)(((uintptr_t)gc_probe_edges + CACHE_SIZE - 1 ) & ~( CACHE_SIZE - 1 ));
	/* Keep surfaces[0] as an in-bounds dummy; background is surfaces[1]. */
	surfaces = (surf_t *)(((uintptr_t)gc_probe_surfaces + CACHE_SIZE - 1 ) & ~( CACHE_SIZE - 1 ));
	surf_max = &surfaces[r_cnumsurfs + 1];
	memset( &surfaces[1], 0, sizeof( surf_t ));
	R_SurfacePatch();

	if( tr.framecount <= 1 )
		gEngfuncs.Con_Reportf( "Xash3D GameCube: R_EdgeDrawing heap world pass edges=%d surfs=%d\n",
			r_numallocatededges, r_cnumsurfs );

	if( vid.buffer && vid.width > 0 && vid.height > 0 )
		memset( vid.buffer, 0, (size_t)vid.width * (size_t)vid.height * sizeof( pixel_t ));

	R_BeginEdgeFrame();
#if XASH_GAMECUBE
	if( tr.framecount <= 1 && gEngfuncs.Sys_CheckParm( "-gcnewgame" ))
		gEngfuncs.Con_Reportf( "Xash3D GameCube: R_EdgeDrawing after BeginEdgeFrame\n" );
#endif
	R_RenderWorld();
#if XASH_GAMECUBE
	{
		extern void R_GcReportFaceEmit( void );
		R_GcReportFaceEmit();
	}
	if( tr.framecount <= 1 && gEngfuncs.Sys_CheckParm( "-gcnewgame" ))
		gEngfuncs.Con_Reportf( "Xash3D GameCube: R_EdgeDrawing after RenderWorld\n" );
#endif
	/* Opaque brush entities (tram, doors, …) share the probe edge/span BSS.
	 * Translucent brushes draw later via R_DrawBrushModelProbe.
	 * During silent G36 windows draw once then skip (160×120 fill budget). */
	{
		static qboolean bmodels_once;
		extern qboolean GC_IsFrameBudgetProbeActive( void );

		if( !( GC_IsFrameBudgetProbeActive() && bmodels_once ))
		{
			R_DrawBEntitiesOnList();
			bmodels_once = true;
		}
	}
	if( tr.framecount <= 1 )
		gEngfuncs.Con_Reportf( "Xash3D GameCube: low-res bmodels in edge pass count=%u\n",
			tr.draw_list->num_edge_entities );
	R_ScanEdges();
#if XASH_GAMECUBE
	if( tr.framecount <= 1 && gEngfuncs.Sys_CheckParm( "-gcnewgame" ))
		gEngfuncs.Con_Reportf( "Xash3D GameCube: R_EdgeDrawing after ScanEdges\n" );
#endif

	if( tr.framecount <= 1 && vid.buffer )
	{
		size_t n = (size_t)vid.width * (size_t)vid.height;
		size_t nonzero = 0;
		size_t p;

		for( p = 0; p < n; p++ )
		{
			if( vid.buffer[p] )
				nonzero++;
		}
		gEngfuncs.Con_Reportf( "Xash3D GameCube: gcmap world pixels nonzero=%zu/%zu\n", nonzero, n );
	}
}
#endif

static void R_EdgeDrawingStack( void )
{
	edge_t ledges[NUMSTACKEDGES
		      + (( CACHE_SIZE - 1 ) / sizeof( edge_t )) + 1];
	surf_t lsurfs[NUMSTACKSURFACES
		      + (( CACHE_SIZE - 1 ) / sizeof( surf_t )) + 1];

	if( !FBitSet( RI.rvp.flags, RF_DRAW_WORLD ))
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

static void R_EdgeDrawing( void )
{
#if XASH_GAMECUBE
	/* Keep large stack tables out of this frame: they live only in R_EdgeDrawingStack. */
	if( gEngfuncs.Sys_CheckParm( "-gcmap" ) && !GC_UseLowResWorldProbe() )
	{
		gEngfuncs.Con_Reportf( "Xash3D GameCube: R_EdgeDrawing skipping world pass (gcmap smoke)\n" );
		return;
	}
	if( GC_UseLowResWorldProbe() )
	{
		R_EdgeDrawingGcmapProbe();
		return;
	}
#endif
	R_EdgeDrawingStack();
}

/*
===============
R_MarkLeaves
===============
*/
static void R_MarkLeaves( void )
{
#if XASH_GAMECUBE
	if( GC_IsLowMemoryMode() && !GC_UseLowResWorldProbe() )
	{
		tr.visframecount++;
		r_oldviewcluster = r_viewcluster;
		return;
	}

	/* Low-res without visdata: mark every leaf. New Game with prepare-time
	 * FatPVS cache applies that mark below (not full-vis). */
	if( GC_UseLowResWorldProbe() && !( WORLDMODEL && WORLDMODEL->visdata )
		&& !gEngfuncs.Sys_CheckParm( "-gcnewgame" ))
	{
		int i;

		tr.visframecount++;
		r_oldviewcluster = r_viewcluster;
		if( WORLDMODEL && WORLDMODEL->leafs )
		{
			for( i = 0; i < WORLDMODEL->numleafs; i++ )
				((mnode_t *)&WORLDMODEL->leafs[i + 1])->visframe = tr.visframecount;
		}
		if( WORLDMODEL && WORLDMODEL->nodes )
		{
			for( i = 0; i < WORLDMODEL->numnodes; i++ )
				WORLDMODEL->nodes[i].visframe = tr.visframecount;
		}
		if( tr.framecount <= 1 )
			gEngfuncs.Con_Reportf( "Xash3D GameCube: full-vis leaf mark active (low-res no visdata)\n" );
		return;
	}

	/* G83: apply prepare-time FatPVS + parent marks — no live tree walks. */
	if( gEngfuncs.Sys_CheckParm( "-gcnewgame" ) && GC_UseLowResWorldProbe()
		&& GC_HasNewGameCachedVis() )
	{
		tr.visframecount++;
		r_oldviewcluster = r_viewcluster;
		if( GC_ApplyNewGameCachedVis( tr.visframecount ))
		{
			GC_ApplyNewGameSurfVis( tr.framecount );
			if( tr.framecount <= 1 )
				gEngfuncs.Con_Reportf( "Xash3D GameCube: cached FatPVS leaf mark active cluster=%d\n",
					r_viewcluster );
			return;
		}
		/* Apply failed — do not fall through to a second full-vis stamp. */
		if( tr.framecount <= 1 )
			gEngfuncs.Con_Reportf( "Xash3D GameCube: cached FatPVS apply failed; using full-vis\n" );
	}

	/* New Game fallback if prepare cache missing: full-vis (keeps pixels). */
	if( GC_UseLowResWorldProbe() && gEngfuncs.Sys_CheckParm( "-gcnewgame" ))
	{
		int i;

		tr.visframecount++;
		r_oldviewcluster = r_viewcluster;
		if( WORLDMODEL && WORLDMODEL->leafs )
		{
			for( i = 0; i < WORLDMODEL->numleafs; i++ )
				((mnode_t *)&WORLDMODEL->leafs[i + 1])->visframe = tr.visframecount;
		}
		if( WORLDMODEL && WORLDMODEL->nodes )
		{
			for( i = 0; i < WORLDMODEL->numnodes; i++ )
				WORLDMODEL->nodes[i].visframe = tr.visframecount;
		}
		if( tr.framecount <= 1 )
			gEngfuncs.Con_Reportf( "Xash3D GameCube: full-vis leaf mark fallback (low-res newgame) cluster=%d\n",
				r_viewcluster );
		return;
	}
#endif

	if( r_oldviewcluster == r_viewcluster && !r_novis.value && r_viewcluster != -1 )
		return;

	tr.visframecount++;
	r_oldviewcluster = r_viewcluster;

	gEngfuncs.R_FatPVS( RI.rvp.vieworigin, r_pvs_radius->value, RI.visbytes, false, false );
	byte *vis = RI.visbytes;

	for( int i = 0; i < WORLDMODEL->numleafs; i++ )
	{
		if( vis[i >> 3] & ( 1 << ( i & 7 )))
		{
			mnode_t *node = (mnode_t *) &WORLDMODEL->leafs[i + 1];
#if XASH_GAMECUBE
			int parent_depth = 0;
			const int parent_limit = WORLDMODEL->numnodes > 0 ? WORLDMODEL->numnodes + 8 : 4096;
#endif
			do
			{
				if( node->visframe == tr.visframecount )
					break;
				node->visframe = tr.visframecount;
				node = node->parent;
#if XASH_GAMECUBE
				if( ++parent_depth > parent_limit )
				{
					if( tr.framecount <= 1 )
						gEngfuncs.Con_Reportf( "Xash3D GameCube: MarkLeaves parent walk limit leaf=%d\n", i );
					break;
				}
#endif
			}
			while( node );
		}
	}
#if XASH_GAMECUBE
	if( GC_UseLowResWorldProbe() && tr.framecount <= 1 )
		gEngfuncs.Con_Reportf( "Xash3D GameCube: FatPVS leaf mark active (low-res)%s\n",
			gEngfuncs.Sys_CheckParm( "-gcnewgame" ) ? " newgame" : "" );
#endif
}

/*
================
R_RenderScene

R_SetupRefParams must be called right before
================
*/
void GAME_EXPORT R_RenderScene( void )
{
#if XASH_GAMECUBE
	double gc_render_start = gEngfuncs.pfnTime();
#endif

	if( !WORLDMODEL && FBitSet( RI.rvp.flags, RF_DRAW_WORLD ))
		gEngfuncs.Host_Error( "%s: NULL worldmodel\n", __func__ );

	// frametime is valid only for normal pass
	tr.frametime = gp_cl->time - gp_cl->oldtime;

	// begin a new frame
	tr.framecount++;

#if XASH_GAMECUBE
	// G24a: report quality mode at render scene entry for verification
	if( tr.framecount <= 2 )
	{
		int quality = GC_GetVisualQuality();
		gEngfuncs.Con_Reportf( "Xash3D GameCube: R_RenderScene frame %d quality=%d\n", tr.framecount, quality );
	}
#endif

	if( tr.map_unload )
	{
		D_FlushCaches();
		tr.map_unload = false;
	}


	R_SetupFrustum();
#if XASH_GAMECUBE
	if( tr.framecount <= 1 && gEngfuncs.Sys_CheckParm( "-gcnewgame" ))
		gEngfuncs.Con_Reportf( "Xash3D GameCube: R_RenderScene after frustum\n" );
#endif
	R_SetupFrame();
#if XASH_GAMECUBE
	if( tr.framecount <= 1 && gEngfuncs.Sys_CheckParm( "-gcnewgame" ))
		gEngfuncs.Con_Reportf( "Xash3D GameCube: R_RenderScene after setupframe\n" );
#endif

#if XASH_GAMECUBE
	/* Smoke (quality 0, non-probe) skips PushDlights. New Game low-res skips
	 * PushDlights too — walking the full c0a0 tree stalls Host_Frame before
	 * edge pixels land. */
	if(( GC_IsLowMemoryMode() && !GC_UseLowResWorldProbe() )
		|| gEngfuncs.Sys_CheckParm( "-gcnewgame" ))
		tr.dlightframecount = tr.framecount;
	else
	{
#endif
	tr.dlightframecount = R_PushDlights( WORLDMODEL, tr.framecount );
#if XASH_GAMECUBE
		if( GC_UseLowResWorldProbe() && tr.framecount <= 1 )
		{
			unsigned active = 0;

			for( int i = 0; i < MAX_DLIGHTS; i++ )
			{
				const dlight_t *l = &gp_dlights[i];

				if( l->die >= gp_cl->time && l->radius > 0.0f )
					active++;
			}
			gEngfuncs.Con_Reportf( "Xash3D GameCube: world dlight push armed active=%u\n", active );
		}
	}
	if( tr.framecount <= 1 && gEngfuncs.Sys_CheckParm( "-gcnewgame" ))
		gEngfuncs.Con_Reportf( "Xash3D GameCube: R_RenderScene after dlights\n" );
#endif
	R_SetupModelviewMatrix( RI.worldviewMatrix );
	R_SetupProjectionMatrix( RI.projectionMatrix );

	Matrix4x4_Concat( RI.worldviewProjectionMatrix, RI.projectionMatrix, RI.worldviewMatrix );
	tr.modelviewIdentity = true;

//	R_SetupGL( true );
	// R_Clear( ~0 );

	R_MarkLeaves();
#if XASH_GAMECUBE
	if( tr.framecount <= 1 && gEngfuncs.Sys_CheckParm( "-gcnewgame" ))
		gEngfuncs.Con_Reportf( "Xash3D GameCube: R_RenderScene after markleaves\n" );
#endif
	// R_PushDlights (r_worldmodel); ??
	// R_DrawWorld();
	R_EdgeDrawing();
#if XASH_GAMECUBE
	if( tr.framecount <= 1 && gEngfuncs.Sys_CheckParm( "-gcnewgame" ))
		gEngfuncs.Con_Reportf( "Xash3D GameCube: R_RenderScene after edges\n" );
#endif
#if XASH_GAMECUBE
	/* -gcmap smoke: world only. New Game low-res: world + bounded ents
	 * (studio/sprites/translucent brushes via probe BSS). */
	if( gEngfuncs.Sys_CheckParm( "-gcmap" ) || gEngfuncs.Sys_CheckParm( "-gcnewgame" ))
	{
		if( gEngfuncs.Sys_CheckParm( "-gcnewgame" ) && GC_UseLowResWorldProbe() )
			R_DrawStudioEntitiesLowRes();

		/* OSReport during the timed window inflates Host_Frame; keep sparse. */
		if( tr.framecount <= 2 || (( tr.framecount & 63 ) == 0 ))
		{
			double elapsed_ms = ( gEngfuncs.pfnTime() - gc_render_start ) * 1000.0;

			gEngfuncs.Con_Reportf( "Xash3D GameCube: gcmap render time=%.2fms frame=%d worldrender=%d\n",
				elapsed_ms, tr.framecount, GC_UseLowResWorldProbe() ? 1 : 0 );
			if( elapsed_ms >= 33.0 )
				gEngfuncs.Con_Reportf( "Xash3D GameCube: G49 slow gcmap render %.2fms frame=%d quality=%d\n",
					elapsed_ms, tr.framecount, GC_GetVisualQuality() );
		}
		if( tr.framecount <= 1 )
			gEngfuncs.Con_Reportf( "Xash3D GameCube: R_RenderScene gcmap route complete worldrender=%d\n",
				GC_UseLowResWorldProbe() ? 1 : 0 );
		return;
	}
#endif

	gEngfuncs.CL_ExtraUpdate(); // don't let sound get messed up if going slow

	R_DrawEntitiesOnList();

#if XASH_GAMECUBE
	if( tr.framecount <= 32 )
	{
		double elapsed_ms = ( gEngfuncs.pfnTime() - gc_render_start ) * 1000.0;

		gEngfuncs.Con_Reportf( "Xash3D GameCube: frame time=%.2fms\n", elapsed_ms );
		if( elapsed_ms >= 33.0 )
			gEngfuncs.Con_Reportf( "Xash3D GameCube: G49 slow render %.2fms frame=%d quality=%d\n",
				elapsed_ms, tr.framecount, GC_GetVisualQuality() );
	}
#endif

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
	RI.rvp = *rvp;
}

/*
===============
R_RenderFrame
===============
*/
void GAME_EXPORT R_RenderFrame( const ref_viewpass_t *rvp )
{
#if XASH_GAMECUBE
	/* Direct world probe path: skip viewmodel events / client draw hooks that
	 * are not populated for -gcmap smoke or post-G36 New Game presents. */
	if( gEngfuncs.Sys_CheckParm( "-gcmap" ) || gEngfuncs.Sys_CheckParm( "-gcnewgame" ))
	{
		if( r_norefresh->value )
			return;
		if( gpGlobals->height > vid.height || gpGlobals->width > vid.width )
		{
			/* G129: sync lean New Game screens instead of silently skipping draw. */
			if( gEngfuncs.Sys_CheckParm( "-gcnewgame" ) && vid.width > 0 && vid.height > 0 )
			{
				gpGlobals->width = vid.width;
				gpGlobals->height = vid.height;
			}
			else
				return;
		}

		R_ClearScene();
		R_SetupRefParams( rvp );
		tr.fCustomRendering = false;
		tr.realframecount++;
		R_RenderScene();
		return;
	}
#endif

	if( r_norefresh->value )
		return;

	// prevent cache overrun
	if( gpGlobals->height > vid.height || gpGlobals->width > vid.width )
		return;

	// setup the initial render params
	R_SetupRefParams( rvp );

	// completely override rendering
	if( gEngfuncs.drawFuncs != NULL && gEngfuncs.drawFuncs->GL_RenderFrame != NULL )
	{
		tr.fCustomRendering = true;

		if( gEngfuncs.drawFuncs->GL_RenderFrame( rvp ))
		{
			// R_GatherPlayerLight( tr.viewent );
			tr.realframecount++;
			tr.fResetVis = true;
			return;
		}
	}

	tr.fCustomRendering = false;
	if( !FBitSet( RI.rvp.flags, RF_ONLY_CLIENTDRAW ))
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
	model_t *world = WORLDMODEL;
#if XASH_GAMECUBE
	gEngfuncs.Con_Reportf( "Xash3D GameCube: R_NewMap quality=%d\n", GC_GetVisualQuality() );
#endif

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

#if XASH_GAMECUBE
	{
		const qboolean gc_world_probe = GC_IsLowMemoryMode() && GC_UseLowResWorldProbe();

		// G24a: low-memory smoke path caps surfaces before MINSURFACES floor
		if( GC_IsLowMemoryMode() )
		{
			if( gc_world_probe )
			{
				if( r_cnumsurfs <= 0 || r_cnumsurfs > GC_PROBE_NUMSURFS )
					r_cnumsurfs = GC_PROBE_NUMSURFS;
			}
			else
			{
				if( r_cnumsurfs > NUMSTACKSURFACES )
					r_cnumsurfs = NUMSTACKSURFACES;
				if( r_cnumsurfs < MINSURFACES )
					r_cnumsurfs = MINSURFACES;
			}
			if( r_cnumsurfs > NUMSTACKSURFACES )
				r_cnumsurfs = NUMSTACKSURFACES;
		}
		gEngfuncs.Con_Reportf( "Xash3D GameCube: renderer surface budget capped to %d (quality=%d)\n", r_cnumsurfs, GC_GetVisualQuality() );
	}
#endif
	if( r_cnumsurfs <= MINSURFACES
#if XASH_GAMECUBE
	 && !( GC_IsLowMemoryMode() && GC_UseLowResWorldProbe() )
#endif
	)
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

#if XASH_GAMECUBE
	{
		const qboolean gc_world_probe = GC_IsLowMemoryMode() && GC_UseLowResWorldProbe();

		// G24a: low-memory smoke path caps edges to stack budget
		if( GC_IsLowMemoryMode() )
		{
			if( gc_world_probe )
			{
				if( r_numallocatededges <= 0 || r_numallocatededges > GC_PROBE_NUMEDGES )
					r_numallocatededges = GC_PROBE_NUMEDGES;
			}
			else if( r_numallocatededges > NUMSTACKEDGES )
				r_numallocatededges = NUMSTACKEDGES;
		}
		gEngfuncs.Con_Reportf( "Xash3D GameCube: renderer edge budget capped to %d (quality=%d)\n", r_numallocatededges, GC_GetVisualQuality() );
	}
#endif

	if( r_numallocatededges <= NUMSTACKEDGES )
	{
		auxedges = NULL;
	}
	else
	{
		auxedges = Mem_Malloc( r_temppool, r_numallocatededges * sizeof( edge_t ));
	}

	// clear out efrags in case the level hasn't been reloaded
	for( int i = 0; i < world->numleafs; i++ )
		world->leafs[i + 1].efrags = NULL;

	tr.sample_size = gEngfuncs.Mod_SampleSizeForFace( &world->surfaces[0] );

	for( int i = 1; i < world->numsurfaces; i++ )
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
		tr.sample_bits = 0;

		for( uint sample_pot = 1; sample_pot < tr.sample_size; sample_pot <<= 1, tr.sample_bits++ )
			;
	}

	gEngfuncs.Con_Printf( "Map sample size is %d\n", tr.sample_size );
#if XASH_GAMECUBE
	gEngfuncs.Con_Reportf( "Xash3D GameCube: R_NewMap post-load quality=%d (sample_size=%d)\n", GC_GetVisualQuality(), tr.sample_size );
#endif
}

/*
================
R_InitTurb
================
*/
static void R_InitTurb( void )
{
	for( int i = 0; i < 1280; i++ )
	{
		sintable[i] = AMP + sin( i * 3.14159 * 2 / CYCLE ) * AMP;
		blanktable[i] = 0;                                             // PGM
	}
}



qboolean GAME_EXPORT R_Init( void )
{
	qboolean glblit = false;

	tr.framecount = 0;
	tr.sample_size = 0;
	tr.sample_bits = -1;

#if XASH_GAMECUBE
	{
		int quality = GC_GetVisualQuality();
		gEngfuncs.Con_Reportf( "Xash3D GameCube: renderer init quality=%d (sample_size=%d, low_memory=%d)\n", quality, tr.sample_size, GC_IsLowMemoryMode() );
	}
#endif

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

	r_temppool = Mem_AllocPool( "ref_gx zone" );

#if XASH_GAMECUBE
	gc_probe_edges = NULL;
	gc_probe_surfaces = NULL;
#endif

#if XASH_GAMECUBE
	glblit = false;

	gEngfuncs.Con_Reportf( "Xash3D GameCube: renderer video init begin\n" );
	if( !gEngfuncs.R_Init_Video( REF_GX ))
	{
		gEngfuncs.R_Free_Video();
		return false;
	}
	gEngfuncs.Con_Reportf( "Xash3D GameCube: renderer video init ready\n" );
#else
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
#endif

	// see R_ProcessEntData for tr.entities initialization
	tr.palette = (color24 *)ENGINE_GET_PARM( PARM_GET_PALETTE_PTR );
	tr.viewent = (cl_entity_t *)ENGINE_GET_PARM( PARM_GET_VIEWENT_PTR );
	tr.texgammatable = (byte *)ENGINE_GET_PARM( PARM_GET_TEXGAMMATABLE_PTR );
	tr.lightgammatable = (uint *)ENGINE_GET_PARM( PARM_GET_LIGHTGAMMATABLE_PTR );
	tr.screengammatable = (uint *)ENGINE_GET_PARM( PARM_GET_SCREENGAMMATABLE_PTR );
	tr.lineargammatable = (uint *)ENGINE_GET_PARM( PARM_GET_LINEARGAMMATABLE_PTR );
		tr.elights = (dlight_t *)ENGINE_GET_PARM( PARM_GET_ELIGHTS_PTR );

	if( !R_InitBlit( glblit ))
	{
		gEngfuncs.R_Free_Video();
		return false;
	}
#if XASH_GAMECUBE
	gEngfuncs.Con_Reportf( "Xash3D GameCube: renderer blit init ready\n" );
#endif

#if XASH_GAMECUBE
	{
		int init_quality = GC_GetVisualQuality();
		gEngfuncs.Con_Reportf( "Xash3D GameCube: renderer entering image init (quality=%d)\n", init_quality );
		if( init_quality == 0 )
		{
			gEngfuncs.Con_Reportf( "Xash3D GameCube: skipping full image init (quality=0)\n" );
		}
		else
		{
			R_InitImages();
			gEngfuncs.Con_Reportf( "Xash3D GameCube: renderer images ready\n" );
#if defined(GC_MEMSAMPLE_AVAILABLE)
			GC_MemSample( "textures" );
#endif
		}
	}
#else
	R_InitImages();
#endif
	// init draw stack
	tr.draw_list = &tr.draw_stack[0];
	tr.draw_stack_pos = 0;
	qfrustum.view_clipplanes[0].leftedge = true;
	qfrustum.view_clipplanes[1].rightedge = true;
	qfrustum.view_clipplanes[1].leftedge = qfrustum.view_clipplanes[2].leftedge = qfrustum.view_clipplanes[3].leftedge = false;
	qfrustum.view_clipplanes[0].rightedge = qfrustum.view_clipplanes[2].rightedge = qfrustum.view_clipplanes[3].rightedge = false;
	R_StudioInit();
#if XASH_GAMECUBE
	{
		int init_quality = GC_GetVisualQuality();
		gEngfuncs.Con_Reportf( "Xash3D GameCube: renderer studio ready (quality=%d)\n", init_quality );
		if( init_quality != 0 )
		{
#if defined(GC_MEMSAMPLE_AVAILABLE)
			GC_MemSample( "models" );
#endif
		}
	}
#endif
	R_InitTurb();
	GL_InitRandomTable();
#if XASH_GAMECUBE
	gEngfuncs.Con_Reportf( "Xash3D GameCube: renderer init ready (quality=%d)\n", GC_GetVisualQuality() );
#endif

	return true;
}

void GAME_EXPORT R_Shutdown( void )
{
#if XASH_GAMECUBE
	free( gc_probe_edges );
	free( gc_probe_surfaces );
	free( gc_probe_spans );
	gc_probe_edges = NULL;
	gc_probe_surfaces = NULL;
	gc_probe_spans = NULL;
	gc_probe_numedges = 0;
	gc_probe_numsurfs = 0;
	gc_probe_numspans = 0;
#endif
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
	float  dist;

	float offset = ((int)e->index ) * 363.0f; // Use ent index to de-sync these fx

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
		if( e->curstate.renderamt > 0 )
			e->curstate.renderamt -= 1;
		else
			e->curstate.renderamt = 0;
		blend = e->curstate.renderamt;
		break;
	case kRenderFxFadeFast:
		if( e->curstate.renderamt > 3 )
			e->curstate.renderamt -= 4;
		else
			e->curstate.renderamt = 0;
		blend = e->curstate.renderamt;
		break;
	case kRenderFxSolidSlow:
		if( e->curstate.renderamt < 255 )
			e->curstate.renderamt += 1;
		else
			e->curstate.renderamt = 255;
		blend = e->curstate.renderamt;
		break;
	case kRenderFxSolidFast:
		if( e->curstate.renderamt < 252 )
			e->curstate.renderamt += 4;
		else
			e->curstate.renderamt = 255;
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
	{
		vec3_t tmp = Vec3( e->origin );
		VectorSubtract( tmp, RI.rvp.vieworigin, tmp );
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
	}
	default:
		blend = e->curstate.renderamt;
		break;
	}

	blend = bound( 0, blend, 255 );

	return blend;
}
