/*
cl_render.c - RenderAPI loader & implementation
Copyright (C) 2019 a1batross

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
#include "library.h"
#include "platform/platform.h"

int R_FatPVS( const vec3_t org, float radius, byte *visbuffer, qboolean merge, qboolean fullvis )
{
	return Mod_FatPVS( org, radius, visbuffer, world.visbytes, merge, fullvis, false );
}

lightstyle_t *CL_GetLightStyle( int number )
{
	Assert( number >= 0 && number < MAX_LIGHTSTYLES );
	return &cl.lightstyles[number];
}

const ref_overview_t *GL_GetOverviewParms( void )
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
=================
R_EnvShot

=================
*/
static void R_EnvShot( const float *vieworg, const char *name, qboolean skyshot, int shotsize )
{
	static vec3_t viewPoint;

	if( COM_StringEmptyOrNULL( name ))
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
	cls.envshot_viewsize = Q_max( 0, shotsize );
}

/*
=============
CL_GenericHandle

=============
*/
static const char *CL_GenericHandle( int fileindex )
{
	if( fileindex < 0 || fileindex >= MAX_CUSTOM )
		return 0;
	return cl.files_precache[fileindex];
}

intptr_t CL_RenderGetParm( const int parm, const int arg, const qboolean checkRef )
{
	switch( parm )
	{
	case PARM_BSP2_SUPPORTED:
		return 1;
	case PARAM_GAMEPAUSED:
		return cl.paused;
	case PARM_CLIENT_INGAME:
		return CL_IsInGame();
	case PARM_MAX_ENTITIES:
		return clgame.maxEntities;
	case PARM_FEATURES:
		return host.features;
	case PARM_MAP_HAS_DELUXE:
		return FBitSet( world.flags, FWORLD_HAS_DELUXEMAP );
	case PARM_CLIENT_ACTIVE:
		return (cls.state == ca_active);
	case PARM_DEDICATED_SERVER:
		return (host.type == HOST_DEDICATED);
	case PARM_WATER_ALPHA:
		return FBitSet( world.flags, FWORLD_WATERALPHA );
	case PARM_DELUXEDATA:
		return (intptr_t)world.deluxedata;
	case PARM_SHADOWDATA:
		return (intptr_t)world.shadowdata;
	case PARM_FULLSCREEN:
		return refState.window_mode == WINDOW_MODE_BORDERLESS || refState.window_mode == WINDOW_MODE_FULLSCREEN;
	case PARM_WIDESCREEN:
		return refState.wideScreen;
	case PARM_SCREEN_WIDTH:
		return refState.width;
	case PARM_SCREEN_HEIGHT:
		return refState.height;
	case PARM_SKY_SPHERE:
		return FBitSet( world.flags, FWORLD_SKYSPHERE ) && !FBitSet( world.flags, FWORLD_CUSTOM_SKYBOX );
	case PARM_SURF_SAMPLESIZE:
		if( arg >= 0 && arg < cl.worldmodel->numsurfaces )
			return Mod_SampleSizeForFace( &cl.worldmodel->surfaces[arg] );
		return LM_SAMPLE_SIZE;
	default:
		// indicates call from client.dll
		if( checkRef )
		{
			return ref.dllFuncs.RefGetParm( parm, arg );
		}
		// call issued from ref_dll, check extensions here
		else switch( parm )
		{
		case PARM_DEV_OVERVIEW:
			return CL_IsDevOverviewMode();
		case PARM_THIRDPERSON:
			return CL_IsThirdPerson();
		case PARM_QUAKE_COMPATIBLE:
			return Host_IsQuakeCompatible();
		case PARM_CONNSTATE:
			return (int)cls.state;
		case PARM_PLAYING_DEMO:
			return cls.demoplayback;
		case PARM_WATER_LEVEL:
			return cl.local.waterlevel;
		case PARM_LOCAL_HEALTH:
			return cl.local.health;
		case PARM_LOCAL_GAME:
			return Host_IsLocalGame();
		case PARM_NUMENTITIES:
			return pfnNumberOfEntities();
		case PARM_GET_CLIENT_PTR:
			return (intptr_t)&cl.time; // with the offset
		case PARM_GET_HOST_PTR:
			return (intptr_t)&host.realtime; // with the offset
		case PARM_GET_WORLD_PTR:
			return (intptr_t)&world;
		case PARM_GET_MOVEVARS_PTR:
			return (intptr_t)&clgame.movevars;
		case PARM_GET_PALETTE_PTR:
			return (intptr_t)&clgame.palette;
		case PARM_GET_VIEWENT_PTR:
			return (intptr_t)&clgame.viewent;
		case PARM_GET_TEXGAMMATABLE_PTR:
		case PARM_GET_LIGHTGAMMATABLE_PTR:
		case PARM_GET_SCREENGAMMATABLE_PTR:
		case PARM_GET_LINEARGAMMATABLE_PTR:
			return V_GetGammaPtr( parm );
		case PARM_GET_LIGHTSTYLES_PTR:
			return (intptr_t)CL_GetLightStyle( 0 );
		case PARM_GET_DLIGHTS_PTR:
			return (intptr_t)CL_GetDynamicLight( 0 );
		case PARM_GET_ELIGHTS_PTR:
			return (intptr_t)CL_GetEntityLight( 0 );
		}
	}
	return 0;
}

static intptr_t pfnRenderGetParm( int parm, int arg )
{
	return CL_RenderGetParm( parm, arg, true );
}

static void pfnAVI_StreamSound( movie_state_t *avi, int entnum, float fvol, float attn, float synctime )
{
	return; // stub, use AVI_SetParm and AVI_Think to stream AVI sound
}

static render_api_t gRenderAPI =
{
	pfnRenderGetParm, // GL_RenderGetParm,
	NULL, // R_GetDetailScaleForTexture,
	NULL, // R_GetExtraParmsForTexture,
	CL_GetLightStyle,
	CL_GetDynamicLight,
	CL_GetEntityLight,
	LightToTexGamma,
	NULL, // R_GetFrameTime,
	NULL, // R_SetCurrentEntity,
	NULL, // R_SetCurrentModel,
	R_FatPVS,
	R_StoreEfrags,
	NULL, // GL_FindTexture,
	NULL, // GL_TextureName,
	NULL, // GL_TextureData,
	NULL, // GL_LoadTexture,
	NULL, // GL_CreateTexture,
	NULL, // GL_LoadTextureArray,
	NULL, // GL_CreateTextureArray,
	NULL, // GL_FreeTexture,
	NULL, // DrawSingleDecal,
	NULL, // R_DecalSetupVerts,
	NULL, // R_EntityRemoveDecals,
	AVI_LoadVideo,
	AVI_GetVideoInfo,
	AVI_GetVideoFrameNumber,
	AVI_GetVideoFrame,
	NULL, // R_UploadStretchRaw,
	AVI_FreeVideo,
	AVI_IsActive,
	pfnAVI_StreamSound,
	AVI_Think,
	AVI_SetParm,
	NULL, // GL_Bind,
	NULL, // GL_SelectTexture,
	NULL, // GL_LoadTexMatrixExt,
	NULL, // GL_LoadIdentityTexMatrix,
	NULL, // GL_CleanUpTextureUnits,
	NULL, // GL_TexGen,
	NULL, // GL_TextureTarget,
	NULL, // GL_SetTexCoordArrayMode,
	NULL, // GL_GetProcAddress,
	NULL, // GL_UpdateTexSize,
	NULL,
	NULL,
	NULL, // CL_DrawParticlesExternal,
	R_EnvShot,
	pfnSPR_LoadExt,
	NULL, // R_LightVec,
	NULL, // R_StudioGetTexture,
	GL_GetOverviewParms,
	CL_GenericHandle,
	COM_SaveFile,
	NULL,
	R_Mem_Alloc,
	R_Mem_Free,
	CL_GetFilesList,
	pfnFileBufferCRC32,
	pfnCompareFileTime,
	Host_Error,
	(void*)CL_ModelHandle,
	Sys_FloatTime,
	Cvar_Set,
	S_MusicFade,
	COM_SetRandomSeed,
};

static void R_FillRenderAPIFromRef( render_api_t *to, const ref_interface_t *from )
{
	to->GetDetailScaleForTexture = from->GetDetailScaleForTexture;
	to->GetExtraParmsForTexture  = from->GetExtraParmsForTexture;
	to->GetFrameTime             = from->GetFrameTime;
	to->R_SetCurrentEntity       = from->R_SetCurrentEntity;
	to->R_SetCurrentModel        = from->R_SetCurrentModel;
	to->GL_FindTexture           = from->GL_FindTexture;
	to->GL_TextureName           = from->GL_TextureName;
	to->GL_TextureData           = from->GL_TextureData;
	to->GL_LoadTexture           = from->GL_LoadTexture;
	to->GL_CreateTexture         = from->GL_CreateTexture;
	to->GL_LoadTextureArray      = from->GL_LoadTextureArray;
	to->GL_CreateTextureArray    = from->GL_CreateTextureArray;
	to->GL_FreeTexture           = from->GL_FreeTexture;
	to->DrawSingleDecal          = from->DrawSingleDecal;
	to->R_DecalSetupVerts        = from->R_DecalSetupVerts;
	to->R_EntityRemoveDecals     = from->R_EntityRemoveDecals;
	to->AVI_UploadRawFrame       = from->AVI_UploadRawFrame;
	to->GL_Bind                  = from->GL_Bind;
	to->GL_SelectTexture         = from->GL_SelectTexture;
	to->GL_LoadTextureMatrix     = from->GL_LoadTextureMatrix;
	to->GL_TexMatrixIdentity     = from->GL_TexMatrixIdentity;
	to->GL_CleanUpTextureUnits   = from->GL_CleanUpTextureUnits;
	to->GL_TexGen                = from->GL_TexGen;
	to->GL_TextureTarget         = from->GL_TextureTarget;
	to->GL_TexCoordArrayMode     = from->GL_TexCoordArrayMode;
	to->GL_UpdateTexSize         = from->GL_UpdateTexSize;
	to->GL_DrawParticles         = from->GL_DrawParticles;
	to->LightVec                 = from->LightVec;
	to->StudioGetTexture         = from->StudioGetTexture;
	to->GL_GetProcAddress        = from->R_GetProcAddress;
}

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

	// fill missing functions from renderer
	R_FillRenderAPIFromRef( &gRenderAPI, &ref.dllFuncs );

	if( clgame.dllFuncs.pfnGetRenderInterface )
	{
		if( clgame.dllFuncs.pfnGetRenderInterface( CL_RENDER_INTERFACE_VERSION, &gRenderAPI, &clgame.drawFuncs ))
		{
			Con_Reportf( "%s: ^2initailized extended RenderAPI ^7ver. %i\n", __func__, CL_RENDER_INTERFACE_VERSION );
			return true;
		}

		// make sure what render functions is cleared
		memset( &clgame.drawFuncs, 0, sizeof( clgame.drawFuncs ));

		return false; // just tell user about problems
	}

	// render interface is missed
	return true;
}
