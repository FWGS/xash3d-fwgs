/*
r_main.c -- dx2 renderer main loop
Copyright (C) 2010 Uncle Mike
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
#include "ref_params.h"
#include "xash3d_mathlib.h"

ref_instance_t	RI;

static float R_GetFarClip( void )
{
#if 0
	if( WORLDMODEL && RI.drawWorld )
		return tr.movevars->zmax * 1.73f;
#endif
	return 2048.0f;
}

void R_SetupFrustum( void )
{
	const ref_overview_t	*ov = gEngfuncs.GetOverviewParms();
#if 0
	if( RP_NORMALPASS() && ( ENGINE_GET_PARM( PARM_WATER_LEVEL ) >= 3 ) && ENGINE_GET_PARM( PARM_QUAKE_COMPATIBLE ))
	{
		RI.fov_x = atan( tan( DEG2RAD( RI.fov_x ) / 2 ) * ( 0.97f + sin( gp_cl->time * 1.5f ) * 0.03f )) * 2 / (M_PI_F / 180.0f);
		RI.fov_y = atan( tan( DEG2RAD( RI.fov_y ) / 2 ) * ( 1.03f - sin( gp_cl->time * 1.5f ) * 0.03f )) * 2 / (M_PI_F / 180.0f);
	}
#endif
	// build the transformation matrix for the given view angles
	AngleVectors( RI.viewangles, RI.vforward, RI.vright, RI.vup );

	if( !r_lockfrustum.value )
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


static void R_SetupProjectionMatrix( matrix4x4 m )
{
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

	if( tr.rotation & 1 )
	{
		Matrix4x4_CreateProjection( m, yMax, yMin, xMax, xMin, zNear, zFar );
	}
	else
	{
		Matrix4x4_CreateProjection( m, xMax, xMin, yMax, yMin, zNear, zFar );
	}
}

/*
=============
R_SetupModelviewMatrix
=============
*/
static void R_SetupModelviewMatrix( matrix4x4 m )
{
	Matrix4x4_CreateModelview( m );
	if( tr.rotation & 1 )
	{
		Matrix4x4_ConcatRotate( m, anglemod( -RI.viewangles[2] + 90 ), 1, 0, 0 );
		Matrix4x4_ConcatRotate( m, -RI.viewangles[0], 0, 1, 0 );
		Matrix4x4_ConcatRotate( m, -RI.viewangles[1], 0, 0, 1 );
	}
	else
	{
		Matrix4x4_ConcatRotate( m, -RI.viewangles[2], 1, 0, 0 );
		Matrix4x4_ConcatRotate( m, -RI.viewangles[0], 0, 1, 0 );
		Matrix4x4_ConcatRotate( m, -RI.viewangles[1], 0, 0, 1 );
	}
	Matrix4x4_ConcatTranslate( m, -RI.vieworg[0], -RI.vieworg[1], -RI.vieworg[2] );
}

void R_LoadIdentity( void )
{
	if( tr.modelviewIdentity ) return;

	Matrix4x4_LoadIdentity( RI.objectMatrix );
	Matrix4x4_Copy( RI.modelviewMatrix, RI.worldviewMatrix );

	//TODO GL_LoadMatrix( MODELVIEW, RI.modelviewMatrix );
	tr.modelviewIdentity = true;
}

void R_GammaChanged( qboolean do_reset_gamma )
{
	if( do_reset_gamma )
	{
		// paranoia cubemap rendering
		if( gEngfuncs.drawFuncs->GL_BuildLightmaps )
			gEngfuncs.drawFuncs->GL_BuildLightmaps( );
	}
	else
	{
		//GL_RebuildLightmaps();
	}
}

void R_BeginFrame( qboolean clearScene )
{

	gEngfuncs.CL_ExtraUpdate();
}

void R_RenderScene( void )
{
	model_t *worldmodel = gp_cl->models[1];
	if (!worldmodel && RI.drawWorld)
		gEngfuncs.Host_Error("%s: NULL worldmodel\n", __func__);

	// frametime is valid only for normal pass
	if (FBitSet(RI.params, RP_ENVVIEW) == 0)
		tr.frametime = gp_cl->time - gp_cl->oldtime;
	else tr.frametime = 0.0;

	// begin a new frame
	tr.framecount++;

#if 0
	R_PushDlights();
#endif
	R_SetupFrustum();
#if 0
	R_SetupFrame();
	R_SetupGL(true);
	R_Clear(~0);
#endif
	R_MarkLeaves();
#if 0
	R_DrawFog();
	if (RI.drawWorld)
		R_AnimateRipples();

	R_CheckGLFog();
#endif
	R_DrawWorld();

//	R_CheckFog();

	gEngfuncs.CL_ExtraUpdate();	// don't let sound get messed up if going slow
#if 0
	R_DrawEntitiesOnList();

	R_DrawWaterSurfaces();
#endif
}

void R_EndFrame( void )
{
	//swap
	if (!dxc.window)
		return;

	if (dxc.pddsBack)
	{
		RECT dstRect = { 0 };
		POINT point = { 0,0 };
		ClientToScreen(dxc.window, &point);
		GetClientRect(dxc.window, &dstRect);
		OffsetRect(&dstRect, point.x, point.y);

		RECT srcRect = { 0,0,dxc.windowWidth,dxc.windowHeight };
		DXCheck(IDirectDrawSurface_Blt(dxc.pddsPrimary, &dstRect, dxc.pddsBack, &srcRect, DDBLT_WAIT, NULL));
	}

	//resize
	RECT rect = { 0 };
	GetClientRect(dxc.window, &rect);
	int w = rect.right - rect.left;
	int h = rect.bottom - rect.top;

	if (w != dxc.windowWidth || h != dxc.windowHeight)
	{
		D3D_Resize(w, h);
	}
}


void R_AllowFog( qboolean allowed )
{

}


static void R_SetupRefParams( const ref_viewpass_t *rvp )
{
//	RI.params = RP_NONE;
	RI.drawWorld = FBitSet(rvp->flags, RF_DRAW_WORLD);
	RI.onlyClientDraw = FBitSet(rvp->flags, RF_ONLY_CLIENTDRAW);
	RI.farClip = 0;

	if (!FBitSet(rvp->flags, RF_DRAW_CUBEMAP))
		RI.drawOrtho = FBitSet(rvp->flags, RF_DRAW_OVERVIEW);
	else RI.drawOrtho = false;

	// setup viewport
	RI.viewport[0] = rvp->viewport[0];
	RI.viewport[1] = rvp->viewport[1];
	RI.viewport[2] = rvp->viewport[2];
	RI.viewport[3] = rvp->viewport[3];

	// calc FOV
	RI.fov_x = rvp->fov_x;
	RI.fov_y = rvp->fov_y;

	VectorCopy(rvp->vieworigin, RI.vieworg);
	VectorCopy(rvp->viewangles, RI.viewangles);
	VectorCopy(rvp->vieworigin, RI.pvsorigin);
}


void R_RenderFrame( const struct ref_viewpass_s *rvp )
{
	if (r_norefresh->value)
		return;

	// setup the initial render params
	R_SetupRefParams(rvp);

	// completely override rendering
	if (gEngfuncs.drawFuncs->GL_RenderFrame != NULL)
	{
		tr.fCustomRendering = true;

		if (gEngfuncs.drawFuncs->GL_RenderFrame(rvp))
		{
			//R_GatherPlayerLight();
			tr.realframecount++;
			tr.fResetVis = true;
			return;
		}
	}

	tr.fCustomRendering = false;
	//if (!RI.onlyClientDraw)
	//	R_RunViewmodelEvents();

	tr.realframecount++; // right called after viewmodel events
	R_RenderScene();
}


qboolean R_AddEntity( struct cl_entity_s *clent, int type )
{
	return true;
}

void R_ClearScene( void )
{
	tr.draw_list->num_solid_entities = 0;
	tr.draw_list->num_trans_entities = 0;
	tr.draw_list->num_beam_entities = 0;

	// clear the scene befor start new frame
	if (gEngfuncs.drawFuncs->R_ClearScene != NULL)
		gEngfuncs.drawFuncs->R_ClearScene();
}


int WorldToScreen( const vec3_t world, vec3_t screen )
{
	VectorClear( screen );
	return 0;
}

void ScreenToWorld( const float *screen, float *world )
{
	;
}
