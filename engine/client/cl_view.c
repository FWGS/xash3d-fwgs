/*
cl_view.c - player rendering positioning
Copyright (C) 2009 Uncle Mike

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
#include "const.h"
#include "entity_types.h"
#include "vgui_draw.h"
#include "sound.h"
#include "input.h" // touch
#include "platform/platform.h" // GL_UpdateSwapInterval
#if XASH_GAMECUBE
#include "mod_local.h"
#endif

#if XASH_GAMECUBE
#include "studio.h"
#include "gc_vmanim.h"

static qboolean gc_loading_feedback_logged;

/*
DoD viewmodels on HL1 weapon code: the weapon, its prediction and its events
all ask for HL1 sequence indices, so map them by meaning onto the sequence
the DoD model names that way (gc_vmanim.c). Cached per model; anim indices
past the table are mapped on every call.
*/
#define GC_VMA_CACHE 16

static model_t *gc_vma_model;
static char gc_vma_name[64];
static gc_vm_family_t gc_vma_family;
static short gc_vma_seq[GC_VMA_CACHE];

static gc_vm_family_t V_GCViewAnimBind( model_t *mod )
{
	studiohdr_t *hdr;
	int i;

	// model slots are reused across maps, so the name is part of the key
	if( mod == gc_vma_model && ( !mod || !Q_strncmp( mod->name, gc_vma_name, sizeof( gc_vma_name ))))
		return gc_vma_family;

	gc_vma_model = mod;
	Q_strncpy( gc_vma_name, mod ? mod->name : "", sizeof( gc_vma_name ));
	gc_vma_family = GC_VMF_NONE;
	for( i = 0; i < GC_VMA_CACHE; i++ )
		gc_vma_seq[i] = -1;

	if( !mod || mod->type != mod_studio )
		return GC_VMF_NONE;
	gc_vma_family = GC_VMAnimFamily( mod->name );
	if( gc_vma_family == GC_VMF_NONE )
		return GC_VMF_NONE;

	hdr = (studiohdr_t *)Mod_StudioExtradata( mod );
	if( hdr && hdr->numseq > 0 )
	{
		const mstudioseqdesc_t *seq = (const mstudioseqdesc_t *)((const byte *)hdr + hdr->seqindex );
		static const gc_vm_action_t shown[] = { GC_VMA_IDLE, GC_VMA_DRAW, GC_VMA_FIRE, GC_VMA_RELOAD };
		string line;
		size_t k;

		line[0] = '\0';
		for( k = 0; k < sizeof( shown ) / sizeof( shown[0] ); k++ )
		{
			gc_vm_intent_t intent = { shown[k], 0, 0 };
			const int s = GC_VMAnimPick( seq->label, sizeof( *seq ), hdr->numseq, intent );

			Q_strncat( line, va( " %s=%s", GC_VMAnimActionName( shown[k] ),
				s >= 0 ? seq[s].label : "-" ), sizeof( line ));
		}
		Con_Reportf( "Xash3D GameCube: viewmodel anim map %s seqs=%d%s\n",
			mod->name, hdr->numseq, line );
	}
	return gc_vma_family;
}

static int V_GCViewAnimSequence( model_t *mod, int hl_anim )
{
	const gc_vm_family_t family = V_GCViewAnimBind( mod );
	const mstudioseqdesc_t *seq;
	studiohdr_t *hdr;
	gc_vm_intent_t intent;
	int s;

	if( family == GC_VMF_NONE || hl_anim < 0 )
		return hl_anim;
	if( hl_anim < GC_VMA_CACHE && gc_vma_seq[hl_anim] >= 0 )
		return gc_vma_seq[hl_anim];

	hdr = (studiohdr_t *)Mod_StudioExtradata( mod );
	if( !hdr || hdr->numseq <= 0 )
		return hl_anim;

	intent = GC_VMAnimIntent( family, hl_anim );
	if( intent.action == GC_VMA_KEEP )
		intent.action = GC_VMA_RELOAD; // only reached when a step arrives first
	seq = (const mstudioseqdesc_t *)((const byte *)hdr + hdr->seqindex );
	s = GC_VMAnimPick( seq->label, sizeof( *seq ), hdr->numseq, intent );
	if( s < 0 || s >= hdr->numseq )
		s = 0;
	if( hl_anim < GC_VMA_CACHE )
		gc_vma_seq[hl_anim] = (short)s;
	return s;
}

/*
The client's V_CalcRefdef poses the viewmodel. A lean client that returns
early leaves it where it was, so mark it before the call and, if nothing
moved it, pose it here the way hlsdk's V_CalcNormalRefdef does: predicted
eye, cl_bob bob along forward and up with a little tilt, then 1 unit down.
Pitch is stored negated, as the client does; the studio transform undoes it.
*/
#define GC_VM_POSE_UNSET -131072.0f

static float V_GCCvarOr( const char *name, float fallback )
{
	convar_t *var = Cvar_FindVar( name );
	return var ? var->value : fallback;
}

static float V_GCCalcBob( const ref_params_t *pp )
{
	static double bobtime;
	static float bob, lasttime;
	const float bobcycle = V_GCCvarOr( "cl_bobcycle", 0.8f );
	const float bobup = V_GCCvarOr( "cl_bobup", 0.5f );
	float cycle, speed;

	if( pp->onground == -1 || pp->time == lasttime || bobcycle <= 0.0f
		|| bobup <= 0.0f || bobup >= 1.0f )
		return bob;
	lasttime = pp->time;

	bobtime += pp->frametime;
	cycle = bobtime - (int)( bobtime / bobcycle ) * bobcycle;
	cycle /= bobcycle;
	if( cycle < bobup )
		cycle = M_PI_F * cycle / bobup;
	else cycle = M_PI_F + M_PI_F * ( cycle - bobup ) / ( 1.0f - bobup );

	speed = sqrtf( pp->simvel[0] * pp->simvel[0] + pp->simvel[1] * pp->simvel[1] );
	bob = speed * V_GCCvarOr( "cl_bob", 0.01f );
	bob = bob * 0.3f + bob * 0.7f * sinf( cycle );
	bob = bound( -7.0f, bob, 4.0f );
	return bob;
}

static void V_GCRetailViewModelPose( const ref_params_t *pp )
{
	cl_entity_t *view = &clgame.viewent;
	static qboolean logged;
	const float bob = V_GCCalcBob( pp );
	vec3_t forward;
	int i;

	VectorCopy( pp->cl_viewangles, view->angles );
	view->angles[YAW] = pp->viewangles[YAW] + pp->crosshairangle[YAW];
	view->angles[PITCH] = -pp->viewangles[PITCH] + pp->crosshairangle[PITCH] * 0.25f;

	AngleVectors( pp->viewangles, forward, NULL, NULL );
	VectorAdd( pp->simorg, pp->viewheight, view->origin );
	for( i = 0; i < 3; i++ )
		view->origin[i] += bob * 0.4f * forward[i];
	view->origin[2] += bob;

	view->angles[YAW] -= bob * 0.5f;
	view->angles[ROLL] -= bob;
	view->angles[PITCH] -= bob * 0.3f;

	view->origin[2] -= 1.0f;
	if( pp->viewsize == 110.0f || pp->viewsize == 90.0f )
		view->origin[2] += 1.0f;
	else if( pp->viewsize == 100.0f )
		view->origin[2] += 2.0f;
	else if( pp->viewsize == 80.0f )
		view->origin[2] += 0.5f;

	VectorCopy( view->origin, view->curstate.origin );
	VectorCopy( view->angles, view->curstate.angles );
	VectorCopy( view->angles, view->latched.prevangles );

	if( !logged )
	{
		logged = true;
		Con_Reportf( "Xash3D GameCube: viewmodel posed by engine (client CalcRefdef left it)\n" );
	}
}

/*
True when the HL1 animation is one step of a longer one (a shotgun shell or
the closing pump) that the DoD model plays as a single reload: the caller
leaves the playing sequence and its start time alone.
*/
qboolean CL_GCViewAnimKeep( int hl_anim )
{
	model_t *mod = CL_ModelHandle( cl.local.viewmodel );
	const gc_vm_family_t family = V_GCViewAnimBind( mod );

	if( family == GC_VMF_NONE )
		return false;
	return GC_VMAnimIntent( family, hl_anim ).action == GC_VMA_KEEP;
}
#endif

/*
===============
V_CalcViewRect

calc frame rectangle (Quake1 style)
===============
*/
static void V_CalcViewRect( void )
{
	qboolean	full = false;
	int	sb_lines;
	float	size;

	if( Host_IsQuakeCompatible( ))
	{
		// intermission is always full screen
		if( cl.intermission ) size = 120.0f;
		else size = scr_viewsize.value;

		if( size >= 120.0f )
			sb_lines = 0;		// no status bar at all
		else if( size >= 110.0f )
			sb_lines = 24;		// no inventory
		else sb_lines = 48;

		if( scr_viewsize.value >= 100.0f )
		{
			full = true;
			size = 100.0f;
		}
		else size = scr_viewsize.value;

		if( cl.intermission )
		{
			size = 100.0f;
			sb_lines = 0;
			full = true;
		}
		size /= 100.0f;
	}
	else
	{
		full = true;
		sb_lines = 0;
		size = 1.0f;
	}

	clgame.viewport[2] = refState.width * size;
	clgame.viewport[3] = refState.height * size;

	if( clgame.viewport[3] > refState.height - sb_lines )
		clgame.viewport[3] = refState.height - sb_lines;
	if( clgame.viewport[3] > refState.height )
		clgame.viewport[3] = refState.height;

	clgame.viewport[0] = ( refState.width - clgame.viewport[2] ) / 2;
	if( full ) clgame.viewport[1] = 0;
	else clgame.viewport[1] = ( refState.height - sb_lines - clgame.viewport[3] ) / 2;

}

/*
===============
V_SetupViewModel
===============
*/
static void V_SetupViewModel( void )
{
	cl_entity_t	*view = &clgame.viewent;
	player_info_t	*info = &cl.players[cl.playernum];

	if( !cl.local.weaponstarttime )
		cl.local.weaponstarttime = cl.time;

	// setup the viewent variables
	view->curstate.colormap = (info->topcolor & 0xFFFF)|((info->bottomcolor << 8) & 0xFFFF);
	view->curstate.number = cl.playernum + 1;
	view->index = cl.playernum + 1;
	view->model = CL_ModelHandle( cl.local.viewmodel );
	view->curstate.modelindex = cl.local.viewmodel;
	view->curstate.sequence = cl.local.weaponsequence;
	view->curstate.rendermode = kRenderNormal;

#if XASH_GAMECUBE
	/* Prefer landmark Deploy viewmodel (glock after hop); else crowbar. */
	if( Sys_CheckParm( "-gcnewgame" ))
	{
		const char *prefer = Mod_GCLandmarkViewModelPath();
		model_t *vm = NULL;

		if( prefer && prefer[0] )
			vm = Mod_FindName( prefer, false );
		if( vm && vm->type == mod_studio && vm->cache.data )
			view->model = vm;
		else if( !view->model || view->model->type != mod_studio || !view->model->cache.data )
		{
			vm = Mod_FindName( "models/v_crowbar.mdl", false );
			if( vm && vm->type == mod_studio && vm->cache.data )
				view->model = vm;
		}
	}
#endif

#if XASH_GAMECUBE
	view->curstate.sequence = V_GCViewAnimSequence( view->model, cl.local.weaponsequence );
#endif

	// alias models has another animation methods
	if( view->model && view->model->type == mod_studio )
	{
		view->curstate.animtime = cl.local.weaponstarttime;
		view->curstate.frame = 0.0f;
	}
}

/*
===============
V_SetRefParams
===============
*/
static void V_SetRefParams( ref_params_t *fd )
{
	memset( fd, 0, sizeof( ref_params_t ));

	VectorCopy( refState.vieworg, fd->vieworg );
	VectorCopy( refState.viewangles, fd->viewangles );

	// Compute view vectors from angles each frame
	AngleVectors( fd->viewangles, fd->forward, fd->right, fd->up );

	fd->frametime = host.frametime;
	fd->time = cl.time;

	fd->intermission = cl.intermission;
	fd->paused = (cl.paused != 0);
	fd->spectator = (cls.spectator != 0);
	fd->onground = (cl.local.onground != -1);
	fd->waterlevel = cl.local.waterlevel;

	VectorCopy( cl.simvel, fd->simvel );
	VectorCopy( cl.simorg, fd->simorg );

	VectorCopy( cl.viewheight, fd->viewheight );
	fd->idealpitch = cl.local.idealpitch;

	VectorCopy( cl.viewangles, fd->cl_viewangles );
	fd->health = cl.local.health;
	VectorCopy( cl.crosshairangle, fd->crosshairangle );
	if( Host_IsQuakeCompatible( ))
		fd->viewsize = scr_viewsize.value;
	else fd->viewsize = 120.0f;

	VectorCopy( cl.punchangle, fd->punchangle );
	fd->maxclients = cl.maxclients;
	fd->viewentity = cl.viewentity;
	fd->playernum = cl.playernum;
	fd->max_entities = clgame.maxEntities;
	fd->demoplayback = cls.demoplayback;
	fd->hardware = 1; // OpenGL

	if( cl.first_frame || cl.skip_interp )
	{
		cl.first_frame = false;		// now can be unlocked
		fd->smoothing = true;		// NOTE: currently this used to prevent ugly un-duck effect while level is changed
	}
	else fd->smoothing = cl.local.pushmsec;		// enable smoothing in multiplayer by server request (AMX uses)

	// get pointers to movement vars and user cmd
	fd->movevars = &clgame.movevars;
	fd->cmd = &cl.cmd;

	// setup viewport
	fd->viewport[0] = clgame.viewport[0];
	fd->viewport[1] = clgame.viewport[1];
	fd->viewport[2] = clgame.viewport[2];
	fd->viewport[3] = clgame.viewport[3];

	fd->onlyClientDraw = 0;	// reset clientdraw
	fd->nextView = 0;		// reset nextview
}

/*
===============
V_MergeOverviewRefdef

merge refdef with overview settings
===============
*/
static void V_RefApplyOverview( ref_viewpass_t *rvp )
{
	ref_overview_t	*ov = &clgame.overView;

	if( !CL_IsDevOverviewMode( ))
		return;

	// NOTE: Xash3D may use 16:9 or 16:10 aspects
	float aspect = (float)refState.width / (float)refState.height;

	float size_x = fabs( 8192.0f / ov->flZoom );
	float size_y = fabs( 8192.0f / (ov->flZoom * aspect ));

	// compute rectangle
	ov->xLeft = -(size_x / 2);
	ov->xRight = (size_x / 2);
	ov->yTop = -(size_y / 2);
	ov->yBottom = (size_y / 2);

	if( CL_IsDevOverviewMode() == 1 )
	{
		Con_NPrintf( 0, " Overview: Zoom %.2f, Map Origin (%.2f, %.2f, %.2f), Z Min %.2f, Z Max %.2f, Rotated %i\n",
		ov->flZoom, ov->origin[0], ov->origin[1], ov->origin[2], ov->zNear, ov->zFar, ov->rotated );
	}

	VectorCopy( ov->origin, rvp->vieworigin );
	rvp->vieworigin[2] = ov->zFar + ov->zNear;
	vec2_t mins = Vec2( rvp->vieworigin );
	vec2_t maxs = Vec2( rvp->vieworigin );

	mins[!ov->rotated] += ov->xLeft;
	maxs[!ov->rotated] += ov->xRight;
	mins[ov->rotated] += ov->yTop;
	maxs[ov->rotated] += ov->yBottom;

	rvp->viewangles[0] = 90.0f;
	rvp->viewangles[1] = 90.0f;
	rvp->viewangles[2] = (ov->rotated) ? (ov->flZoom < 0.0f) ? 180.0f : 0.0f : (ov->flZoom < 0.0f) ? -90.0f : 90.0f;

	SetBits( rvp->flags, RF_DRAW_OVERVIEW );

	ref.dllFuncs.GL_OrthoBounds( mins, maxs );
}

/*
====================
V_CalcFov
====================
*/
static float V_CalcFov( float *fov_x, float width, float height )
{
	if( *fov_x < 1.0f || *fov_x > 179.0f )
		*fov_x = 90.0f; // default value

	float x = width / tan( DEG2RAD( *fov_x ) * 0.5f );
	float half_fov_y = atan( height / x );

	return RAD2DEG( half_fov_y ) * 2;
}

/*
====================
V_AdjustFov
====================
*/
static void V_AdjustFov( float *fov_x, float *fov_y, float width, float height, qboolean lock_x )
{
	float x, y;

	if( width * 3 == 4 * height || width * 4 == height * 5 )
	{
		// 4:3 or 5:4 ratio
		return;
	}

	if( lock_x )
	{
		*fov_y = 2 * atan((width * 3) / (height * 4) * tan( *fov_y * M_PI_F / 360.0f * 0.5f )) * 360 / M_PI_F;
		return;
	}

	y = V_CalcFov( fov_x, 640, 480 );
	x = *fov_x;

	*fov_x = V_CalcFov( &y, height, width );
	if( *fov_x < x ) *fov_x = x;
	else *fov_y = y;
}

/*
=============
V_GetRefParams
=============
*/
static void V_GetRefParams( ref_params_t *fd, ref_viewpass_t *rvp )
{
	// part1: deniable updates
	VectorCopy( fd->simvel, cl.simvel );
	VectorCopy( fd->simorg, cl.simorg );
	VectorCopy( fd->punchangle, cl.punchangle );
	VectorCopy( fd->viewheight, cl.viewheight );

	// part2: really used updates
	VectorCopy( fd->crosshairangle, cl.crosshairangle );
	VectorCopy( fd->cl_viewangles, cl.viewangles );

	// setup ref_viewpass
	rvp->viewport[0] = fd->viewport[0];
	rvp->viewport[1] = fd->viewport[1];
	rvp->viewport[2] = fd->viewport[2];
	rvp->viewport[3] = fd->viewport[3];

	VectorCopy( fd->vieworg, rvp->vieworigin );
	VectorCopy( fd->viewangles, rvp->viewangles );

	rvp->viewentity = fd->viewentity;

	// calc FOV
	rvp->fov_x = bound( 10.0f, cl.local.scr_fov, 150.0f ); // this is a final fov value

	// first we need to compute FOV and other things that needs for frustum properly work
	rvp->fov_y = V_CalcFov( &rvp->fov_x, clgame.viewport[2], clgame.viewport[3] );

	// adjust FOV for widescreen
	if( refState.wideScreen && r_adjust_fov.value )
		V_AdjustFov( &rvp->fov_x, &rvp->fov_y, clgame.viewport[2], clgame.viewport[3], false );

	rvp->flags = 0;

	if( fd->onlyClientDraw )
		SetBits( rvp->flags, RF_ONLY_CLIENTDRAW );
	SetBits( rvp->flags, RF_DRAW_WORLD );
}

/*
==================
V_PreRender

==================
*/
qboolean V_PreRender( void )
{
	// too early
	if( !ref.initialized )
		return false;

	if( host.status == HOST_SLEEP )
		return false;

	// if the screen is disabled (loading plaque is up)
	if( cls.disable_screen )
	{
#if XASH_GAMECUBE
		/* G36/New Game: allow presents once budget sampling is armed so the
		 * post-ca_active window is not blocked by the loading plaque. */
		if( GC_ShouldUseLightPresent() )
		{
			cls.disable_screen = 0.0f;
		}
		else
#endif
		{
#if XASH_GAMECUBE
			double load_elapsed = host.realtime - cls.disable_screen;
			if( load_elapsed >= 2.0 && !gc_loading_feedback_logged )
			{
#if 0 /* DOL reclaim */
				Con_Reportf( "Xash3D GameCube: G49 loading feedback elapsed=%.2f threshold=2.0\n", load_elapsed );
#endif
				gc_loading_feedback_logged = true;
			}
			if( load_elapsed < 2.0 )
				gc_loading_feedback_logged = false;
#endif
			if(( host.realtime - cls.disable_screen ) > cl_timeout.value )
			{
				Con_Reportf( "%s: loading plaque timed out\n", __func__ );
				cls.disable_screen = 0.0f;
			}
			else
				return false;
		}
	}

	V_CheckGamma();

	ref.dllFuncs.R_BeginFrame( !cl.paused && ( cls.state == ca_active ));

	GL_UpdateSwapInterval( );

	return true;
}

//============================================================================

/*
====================
V_ApplyRefUnderwater

apply underwater fov effect
====================
*/
static void V_ApplyRefUnderwater( ref_viewpass_t *rvp )
{
	if( !FBitSet( rvp->flags, RF_DRAW_CUBEMAP|RF_DRAW_OVERVIEW ))
		return;

	if( !FBitSet( host.features, ENGINE_QUAKE_COMPATIBLE ))
		return;

	if( cl.local.waterlevel < 3 )
		return;

	rvp->fov_x = atan( tan( DEG2RAD( rvp->fov_x ) / 2 ) * ( 0.97f + sin( cl.time * 1.5f ) * 0.03f )) * 2 / (M_PI_F / 180.0f);
	rvp->fov_y = atan( tan( DEG2RAD( rvp->fov_y ) / 2 ) * ( 1.03f - sin( cl.time * 1.5f ) * 0.03f )) * 2 / (M_PI_F / 180.0f);

}

/*
==================
V_RenderView

==================
*/
void V_RenderView( void )
{
	// HACKHACK: make ref params static
	// not really critical but allows client.dll to take address of refdef and don't trigger ASan
	static ref_params_t	rp;
	ref_viewpass_t	rvp;
	int		viewnum = 0;

	if( !cl.video_prepped || ( !ui_renderworld.value && UI_IsVisible() && !cl.background ))
		return; // still loading

#if XASH_GAMECUBE
	/* G90: New Game post-G36 uses the bounded path (no pfnCalcRefdef). */
	if( Sys_CheckParm( "-gcnewgame" ) && GC_IsNewGameG36Done() && GC_IsNewGameWorldReady() )
	{
		V_RenderViewBoundedGC();
		return;
	}
#endif

	V_CalcViewRect ();	// compute viewport rectangle
	V_SetRefParams( &rp );
	V_SetupViewModel ();
	ref.dllFuncs.R_Set2DMode( false );
	SCR_DirtyScreen();
	ref.dllFuncs.GL_BackendStartFrame ();

	do
	{
#if XASH_GAMECUBE
		clgame.viewent.origin[2] = GC_VM_POSE_UNSET;
#endif
		clgame.dllFuncs.pfnCalcRefdef( &rp );
#if XASH_GAMECUBE
		if( clgame.viewent.origin[2] == GC_VM_POSE_UNSET )
			V_GCRetailViewModelPose( &rp );
#endif

		// client dll may replace viewmodel in pfnCalcRefdef, so set animtime again
		// V_SetupViewModel did it only for the model known before this call
		if( clgame.viewent.model && clgame.viewent.model->type == mod_studio )
			clgame.viewent.curstate.animtime = cl.local.weaponstarttime;

		V_GetRefParams( &rp, &rvp );
		V_RefApplyOverview( &rvp );
		V_ApplyRefUnderwater( &rvp );

		if( viewnum == 0 && FBitSet( rvp.flags, RF_ONLY_CLIENTDRAW ))
		{
			ref.dllFuncs.R_ClearScreen();
		}

		GL_RenderFrame( &rvp );
		S_UpdateFrame( &rvp );
		viewnum++;

	} while( rp.nextView );

	// draw debug triangles on a server
	SV_DrawDebugTriangles ();
	ref.dllFuncs.GL_BackendEndFrame ();
}

#if XASH_GAMECUBE
/*
==================
V_RenderViewBoundedGC

G90: New Game world present via V_RenderView entry without pfnCalcRefdef.
Assumes V_PreRender already called R_BeginFrame; V_PostRender will EndFrame.
==================
*/
qboolean V_RenderViewBoundedGC( void )
{
	static int gc_vrv_log;

	if( !GC_IsNewGameWorldReady() )
		return false;

	/* Self-contained Begin/End world present (same as Prepare G90 pump). */
	if( !GC_RenderNewGameWorldFrames( 1 ))
		return false;

	if( gc_vrv_log < 4 )
	{
		Con_Reportf( "Xash3D GameCube: V_RenderView bounded present\n" );
		gc_vrv_log++;
	}
	return true;
}
#endif

#define POINT_SIZE		16.0f
#define NODE_INTERVAL_X(x)	(x * 16.0f)
#define NODE_INTERVAL_Y(x)	(x * 16.0f)

static void R_DrawLeafNode( float x, float y, float scale )
{
	float downScale = scale * 0.25f;// * POINT_SIZE;

	ref.dllFuncs.R_DrawStretchPic( x - downScale * 0.5f, y - downScale * 0.5f, downScale, downScale, 0, 0, 1, 1, R_GetBuiltinTexture( REF_PARTICLE_TEXTURE ) );
}

static void R_DrawNodeConnection( float x, float y, float x2, float y2 )
{
	ref.dllFuncs.Begin( TRI_LINES );
		ref.dllFuncs.Vertex3f( x, y, 0 );
		ref.dllFuncs.Vertex3f( x2, y2, 0 );
	ref.dllFuncs.End( );
}

static void R_ShowTree_r( mnode_t *node, float x, float y, float scale, int shownodes, mleaf_t *viewleaf )
{
	float	downScale = scale * 0.8f;

	downScale = Q_max( downScale, 1.0f );

	if( !node ) return;

	world.recursion_level++;

	if( node->contents < 0 )
	{
		mleaf_t	*leaf = (mleaf_t *)node;

		if( world.recursion_level > world.max_recursion )
			world.max_recursion = world.recursion_level;

		if( shownodes == 1 )
		{
			if( cl.worldmodel->leafs == leaf )
				ref.dllFuncs.Color4f( 1.0f, 1.0f, 1.0f, 1.0f );
			else if( viewleaf && viewleaf == leaf )
				ref.dllFuncs.Color4f( 1.0f, 0.0f, 0.0f, 1.0f );
			else ref.dllFuncs.Color4f( 0.0f, 1.0f, 0.0f, 1.0f );
			R_DrawLeafNode( x, y, scale );
		}
		world.recursion_level--;
		return;
	}

	if( shownodes == 1 )
	{
		ref.dllFuncs.Color4f( 0.0f, 0.0f, 1.0f, 1.0f );
		R_DrawLeafNode( x, y, scale );
	}
	else if( shownodes == 2 )
	{
		R_DrawNodeConnection( x, y, x - scale, y + scale );
		R_DrawNodeConnection( x, y, x + scale, y + scale );
	}

	R_ShowTree_r( node_child( node, 1, cl.worldmodel ), x - scale, y + scale, downScale, shownodes, viewleaf );
	R_ShowTree_r( node_child( node, 0, cl.worldmodel ), x + scale, y + scale, downScale, shownodes, viewleaf );

	world.recursion_level--;
}

static void R_ShowTree( void )
{
	float	x = (float)((refState.width - (int)POINT_SIZE) >> 1);
	float	y = NODE_INTERVAL_Y(1.0f);

	if( !cl.worldmodel || !r_showtree.value )
		return;

	world.recursion_level = 0;
	mleaf_t *viewleaf = Mod_PointInLeaf( refState.vieworg, cl.worldmodel->nodes, cl.worldmodel );

	ref.dllFuncs.TriRenderMode( kRenderTransTexture );

	//pglLineWidth( 2.0f );
	ref.dllFuncs.Color4f( 1, 0.7f, 0, 1.0f );
	//pglDisable( GL_TEXTURE_2D );
	R_ShowTree_r( cl.worldmodel->nodes, x, y, world.max_recursion * 3.5f, 2, viewleaf );
	//pglEnable( GL_TEXTURE_2D );
	//pglLineWidth( 1.0f );

	R_ShowTree_r( cl.worldmodel->nodes, x, y, world.max_recursion * 3.5f, 1, viewleaf );

	Con_NPrintf( 0, "max recursion %d\n", world.max_recursion );
}

/*
==================
V_PostRender

==================
*/
void V_PostRender( void )
{
	qboolean		draw_2d = false;

	ref.dllFuncs.R_AllowFog( false );
	ref.dllFuncs.R_Set2DMode( true );

#if XASH_GAMECUBE
	/* Startup cinematics already drew their complete frame.  Do not run the
	 * general HUD/console/netgraph/debug overlay pipeline here: it adds work
	 * after the video quad and can dominate Dolphin's frame interval. */
	if( cls.state == ca_cinematic )
	{
		ref.dllFuncs.R_AllowFog( true );
		Platform_SetTimer( 0.0f );
		ref.dllFuncs.R_EndFrame();
		return;
	}

	if( cls.state == ca_active && ( Sys_CheckParm( "-gcmap" ) || Sys_CheckParm( "-gcnewgame" )
		|| GC_ShouldUseLightPresent() ))
	{
		/* Smoke/G36 light presents: no HUD. New Game / DoD CapFaces listen
		 * after signon: draw lean HLSDK HUD (health/ammo/cross) so gameplay
		 * is not world-only. Skip silent G36 sample windows. */
		if( !GC_ShouldUseLightPresent()
			&& !GC_IsFrameBudgetProbeActive()
			&& cls.signon == SIGNONS && cls.scrshot_action != scrshot_mapshot
			&& ( Sys_CheckParm( "-gcnewgame" ) || Sys_CheckParm( "-gcmap" )
				|| Sys_CheckParm( "-gcworldrender" )))
			CL_DrawHUD( CL_ACTIVE );
		ref.dllFuncs.R_AllowFog( true );
		Platform_SetTimer( 0.0f );
		ref.dllFuncs.R_EndFrame();
		return;
	}
#endif

	if( cls.state == ca_active && cls.signon == SIGNONS && cls.scrshot_action != scrshot_mapshot )
	{
		SCR_TileClear();
		CL_DrawHUD( CL_ACTIVE );
		VGui_Paint();
	}

	switch( cls.scrshot_action )
	{
	case scrshot_inactive:
	case scrshot_normal:
	case scrshot_snapshot:
		draw_2d = true;
		break;
	}

	if( draw_2d )
	{
		SCR_RSpeeds();
		SCR_NetSpeeds();
		SCR_DrawPos();
		SCR_DrawEnts();
		SCR_DrawNetGraph();
		SCR_DrawUserCmd();
		Joy_DrawDebug();
		IN_GyroDrawDebug();
		SV_DrawOrthoTriangles();
		CL_DrawDemoRecording();
		CL_DrawHUD( CL_CHANGELEVEL );
		ref.dllFuncs.R_ShowTextures();
		R_ShowTree();
		UI_UpdateMenu( host.realtime );
		Con_DrawVersion();
		Con_DrawConsole();
		Con_DrawDebug(); // must be last
		Touch_Draw();
		OSK_Draw();
		Platform_DrawCursor();

		S_ExtraUpdate();
	}

	SCR_MakeScreenShot();
	ref.dllFuncs.R_AllowFog( true );
	Platform_SetTimer( 0.0f );
	ref.dllFuncs.R_EndFrame();

	V_CheckGammaEnd();
}
