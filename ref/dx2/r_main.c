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

	R_SetupFrustum();
	R_SetupFrame();
	R_SetupGL(true);
	R_Clear(~0);

	R_MarkLeaves();
	R_DrawFog();
	if (RI.drawWorld)
		R_AnimateRipples();

	R_CheckGLFog();
	R_DrawWorld();
	R_CheckFog();

	gEngfuncs.CL_ExtraUpdate();	// don't let sound get messed up if going slow

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
