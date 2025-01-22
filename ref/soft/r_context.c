/*
vid_sdl.c - SDL vid component
Copyright (C) 2018 a1batross

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

ref_api_t     gEngfuncs;
ref_globals_t *gpGlobals;
ref_client_t  *gp_cl;
ref_host_t    *gp_host;
gl_globals_t  tr;
ref_speeds_t  r_stats;
poolhandle_t  r_temppool;
viddef_t      vid;

void _Mem_Free( void *data, const char *filename, int fileline )
{
	gEngfuncs._Mem_Free( data, filename, fileline );
}

void *_Mem_Alloc( poolhandle_t poolptr, size_t size, qboolean clear, const char *filename, int fileline )
{
	return gEngfuncs._Mem_Alloc( poolptr, size, clear, filename, fileline );
}

static void GAME_EXPORT R_ClearScreen( void )
{

}

static const byte * GAME_EXPORT R_GetTextureOriginalBuffer( unsigned int idx )
{
	image_t *glt = R_GetTexture( idx );

	if( !glt || !glt->original || !glt->original->buffer )
		return NULL;

	return glt->original->buffer;
}

/*
=============
CL_FillRGBA

=============
*/
static void GAME_EXPORT CL_FillRGBA( int rendermode, float _x, float _y, float _w, float _h, byte r, byte g, byte b, byte a )
{
	vid.rendermode = rendermode;
	_TriColor4ub( r, g, b, a );
	Draw_Fill( _x, _y, _w, _h );
}

void Mod_UnloadTextures( model_t *mod );

static qboolean GAME_EXPORT Mod_ProcessRenderData( model_t *mod, qboolean create, const byte *buf )
{
	qboolean loaded = false;

	if( !create )
	{
		if( gEngfuncs.drawFuncs->Mod_ProcessUserData )
			gEngfuncs.drawFuncs->Mod_ProcessUserData( mod, false, buf );
		Mod_UnloadTextures( mod );
		return true;
	}

	switch( mod->type )
	{
	case mod_studio:
	case mod_brush:
	case mod_alias:
		loaded = true;
		break;
	case mod_sprite:
		Mod_LoadSpriteModel( mod, buf, &loaded, mod->numtexinfo );
		break;
	default:
		gEngfuncs.Host_Error( "%s: unsupported type %d\n", __func__, mod->type );
		return false;
	}

	if( gEngfuncs.drawFuncs->Mod_ProcessUserData )
		gEngfuncs.drawFuncs->Mod_ProcessUserData( mod, true, buf );

	return loaded;
}

static int GL_RefGetParm( int parm, int arg )
{
	image_t *glt;

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
		return 0; // glt->format;
	case PARM_TEX_ENCODE:
		glt = R_GetTexture( arg );
		return 0; // glt->encode;
	case PARM_TEX_MIPCOUNT:
		glt = R_GetTexture( arg );
		return glt->numMips;
	case PARM_TEX_DEPTH:
		glt = R_GetTexture( arg );
		return glt->depth;
	case PARM_TEX_SKYBOX:
		Assert( arg >= 0 && arg < 6 );
		return tr.skyboxTextures[arg];
	case PARM_TEX_SKYTEXNUM:
		return 0; // tr.skytexturenum;
	case PARM_TEX_LIGHTMAP:
		arg = bound( 0, arg, MAX_LIGHTMAPS - 1 );
		return tr.lightmapTextures[arg];
	case PARM_TEX_TARGET:
		glt = R_GetTexture( arg );
		return 0; // glt->target;
	case PARM_TEX_TEXNUM:
		glt = R_GetTexture( arg );
		return 0; // glt->texnum;
	case PARM_TEX_FLAGS:
		glt = R_GetTexture( arg );
		return glt->flags;
	case PARM_TEX_MEMORY:
		return R_TexMemory();
	case PARM_ACTIVE_TMU:
		return 0; // glState.activeTMU;
	case PARM_LIGHTSTYLEVALUE:
		arg = bound( 0, arg, MAX_LIGHTSTYLES - 1 );
		return tr.lightstylevalue[arg];
	case PARM_MAX_IMAGE_UNITS:
		return 0; // GL_MaxTextureUnits();
	case PARM_REBUILD_GAMMA:
		return 0;
	case PARM_GL_CONTEXT_TYPE:
		return 0; // glConfig.context;
	case PARM_GLES_WRAPPER:
		return 0; // glConfig.wrapper;
	case PARM_STENCIL_ACTIVE:
		return 0; // glState.stencilEnabled;
	case PARM_SKY_SPHERE:
		return 0; // ref_soft doesn't support sky sphere
	case PARM_TEX_FILTERING:
		return 0; // ref_soft doesn't do filtering in general
	default:
		return ENGINE_GET_PARM_( parm, arg );
	}
	return 0;
}

static void GAME_EXPORT R_GetDetailScaleForTexture( int texture, float *xScale, float *yScale )
{
	image_t *glt = R_GetTexture( texture );

	if( xScale )
		*xScale = glt->xscale;
	if( yScale )
		*yScale = glt->yscale;
}

static void GAME_EXPORT R_GetExtraParmsForTexture( int texture, byte *red, byte *green, byte *blue, byte *density )
{
	image_t *glt = R_GetTexture( texture );

	if( red )
		*red = glt->fogParams[0];
	if( green )
		*green = glt->fogParams[1];
	if( blue )
		*blue = glt->fogParams[2];
	if( density )
		*density = glt->fogParams[3];
}


static void GAME_EXPORT R_SetCurrentEntity( cl_entity_t *ent )
{
	RI.currententity = ent;

	// set model also
	if( RI.currententity != NULL )
	{
		RI.currentmodel = RI.currententity->model;
	}
}

static void GAME_EXPORT R_SetCurrentModel( model_t *mod )
{
	RI.currentmodel = mod;
}

static float GAME_EXPORT R_GetFrameTime( void )
{
	return tr.frametime;
}

static const char * GAME_EXPORT GL_TextureName( unsigned int texnum )
{
	return R_GetTexture( texnum )->name;
}

static const byte * GAME_EXPORT GL_TextureData( unsigned int texnum )
{
	rgbdata_t *pic = R_GetTexture( texnum )->original;

	if( pic != NULL )
		return pic->buffer;
	return NULL;
}

static void Mod_BrushUnloadTextures( model_t *mod )
{
	int i;


	gEngfuncs.Con_Printf( "Unloading world\n" );
	tr.map_unload = true;

	for( i = 0; i < mod->numtextures; i++ )
	{
		texture_t *tx = mod->textures[i];
		if( !tx || tx->gl_texturenum == tr.defaultTexture )
			continue; // free slot

		GL_FreeTexture( tx->gl_texturenum ); // main texture
		GL_FreeTexture( tx->fb_texturenum ); // luma texture
	}
}

void Mod_UnloadTextures( model_t *mod )
{
	int i, j;

	Assert( mod != NULL );

	switch( mod->type )
	{
	case mod_studio:
		// Mod_StudioUnloadTextures( mod->cache.data );
		break;
	case mod_alias:
		// Mod_AliasUnloadTextures( mod->cache.data );
		break;
	case mod_brush:
		Mod_BrushUnloadTextures( mod );
		break;
	case mod_sprite:
		Mod_SpriteUnloadTextures( mod->cache.data );
		break;
	default: gEngfuncs.Host_Error( "%s: unsupported type %d\n", __func__, mod->type );
	}
}

static void GAME_EXPORT R_ProcessEntData( qboolean allocate, cl_entity_t *entities, unsigned int max_entities )
{
	tr.entities = entities;
	tr.max_entities = max_entities;
}

static void GAME_EXPORT R_Flush( unsigned int flags )
{
	// stub
}

// stubs

static void GAME_EXPORT GL_SetTexCoordArrayMode( uint mode )
{

}

static void GAME_EXPORT GL_BackendStartFrame( void )
{

}

static void GAME_EXPORT GL_BackendEndFrame( void )
{

}


void GAME_EXPORT GL_SetRenderMode( int mode )
{
	vid.rendermode = mode;
	/// TODO: table shading/blending???
	/// maybe, setup block drawing function pointers here
}

static void GAME_EXPORT R_ShowTextures( void )
{
	// textures undone too
}

static void GAME_EXPORT R_SetupSky( int *skyboxTextures )
{
	int i;

	// TODO: R_UnloadSkybox();
	if( !skyboxTextures )
		return;

	for( i = 0; i < SKYBOX_MAX_SIDES; i++ )
		tr.skyboxTextures[i] = skyboxTextures[i];
}

qboolean GAME_EXPORT VID_CubemapShot( const char *base, uint size, const float *vieworg, qboolean skyshot )
{
	// cubemaps? in my softrender???
	return false;
}

static void GAME_EXPORT R_SetSkyCloudsTextures( int solidskyTexture, int alphaskyTexture )
{
	tr.solidskyTexture = solidskyTexture;
	tr.alphaskyTexture = alphaskyTexture;
}

static void GAME_EXPORT GL_SubdivideSurface( model_t *mod, msurface_t *fa )
{

}

static void GAME_EXPORT DrawSingleDecal( decal_t *pDecal, msurface_t *fa )
{

}

static void GAME_EXPORT GL_SelectTexture( int texture )
{

}

static void GAME_EXPORT GL_LoadTexMatrixExt( const float *glmatrix )
{

}

static void GAME_EXPORT GL_LoadIdentityTexMatrix( void )
{

}

static void GAME_EXPORT GL_CleanUpTextureUnits( int last )
{

}

static void GAME_EXPORT GL_TexGen( unsigned int coord, unsigned int mode )
{

}

static void GAME_EXPORT GL_TextureTarget( uint target )
{

}

void GAME_EXPORT Mod_SetOrthoBounds( const float *mins, const float *maxs )
{

}

qboolean GAME_EXPORT R_SpeedsMessage( char *out, size_t size )
{
	return false;
}

byte *GAME_EXPORT Mod_GetCurrentVis( void )
{
	return NULL;
}

static void GAME_EXPORT VGUI_UploadTextureBlock( int drawX, int drawY, const byte *rgba, int blockWidth, int blockHeight )
{
}

static void GAME_EXPORT VGUI_SetupDrawing( qboolean rect )
{
}

static void GAME_EXPORT R_OverrideTextureSourceSize( unsigned int texnum, uint srcWidth, uint srcHeight )
{
	image_t *tx = R_GetTexture( texnum );

	tx->srcWidth = srcWidth;
	tx->srcHeight = srcHeight;
}

static const char *R_GetConfigName( void )
{
	return "ref_soft"; // software specific cvars will go to ref_soft.cfg
}

static void * GAME_EXPORT R_GetProcAddress( const char *name )
{
	return gEngfuncs.GL_GetProcAddress( name );
}

static const ref_interface_t gReffuncs =
{
	R_Init,
	R_Shutdown,
	R_GetConfigName,
	R_SetDisplayTransform,

	GL_SetupAttributes,
	GL_InitExtensions,
	GL_ClearExtensions,

	R_GammaChanged,
	R_BeginFrame,
	R_RenderScene,
	R_EndFrame,
	R_PushScene,
	R_PopScene,
	GL_BackendStartFrame,
	GL_BackendEndFrame,

	R_ClearScreen,
	R_AllowFog,
	GL_SetRenderMode,

	R_AddEntity,
	CL_AddCustomBeam,
	R_ProcessEntData,
	R_Flush,

	R_ShowTextures,

	R_GetTextureOriginalBuffer,
	GL_LoadTextureFromBuffer,
	GL_ProcessTexture,
	R_SetupSky,

	R_Set2DMode,
	R_DrawStretchRaw,
	R_DrawStretchPic,
	CL_FillRGBA,
	R_WorldToScreen,

	VID_ScreenShot,
	VID_CubemapShot,

	R_LightPoint,

	R_DecalShoot,
	R_DecalRemoveAll,
	R_CreateDecalList,
	R_ClearAllDecals,

	R_StudioEstimateFrame,
	R_StudioLerpMovement,
	CL_InitStudioAPI,

	R_SetSkyCloudsTextures,
	GL_SubdivideSurface,
	CL_RunLightStyles,

	R_GetSpriteParms,
	R_GetSpriteTexture,

	Mod_ProcessRenderData,
	Mod_StudioLoadTextures,

	CL_DrawParticles,
	CL_DrawTracers,
	CL_DrawBeams,
	R_BeamCull,

	GL_RefGetParm,
	R_GetDetailScaleForTexture,
	R_GetExtraParmsForTexture,
	R_GetFrameTime,

	R_SetCurrentEntity,
	R_SetCurrentModel,

	GL_FindTexture,
	GL_TextureName,
	GL_TextureData,
	GL_LoadTexture,
	GL_CreateTexture,
	GL_LoadTextureArray,
	GL_CreateTextureArray,
	GL_FreeTexture,
	R_OverrideTextureSourceSize,

	DrawSingleDecal,
	R_DecalSetupVerts,
	R_EntityRemoveDecals,

	R_UploadStretchRaw,

	GL_Bind,
	GL_SelectTexture,
	GL_LoadTexMatrixExt,
	GL_LoadIdentityTexMatrix,
	GL_CleanUpTextureUnits,
	GL_TexGen,
	GL_TextureTarget,
	GL_SetTexCoordArrayMode,
	GL_UpdateTexSize,
	NULL,
	NULL,

	CL_DrawParticlesExternal,
	R_LightVec,
	R_StudioGetTexture,

	R_RenderFrame,
	Mod_SetOrthoBounds,
	R_SpeedsMessage,
	Mod_GetCurrentVis,
	R_NewMap,
	R_ClearScene,
	R_GetProcAddress,

	TriRenderMode,
	TriBegin,
	TriEnd,
	_TriColor4f,
	_TriColor4ub,
	TriTexCoord2f,
	TriVertex3fv,
	TriVertex3f,
	TriFog,
	R_ScreenToWorld,
	TriGetMatrix,
	TriFogParams,
	TriCullFace,

	VGUI_SetupDrawing,
	VGUI_UploadTextureBlock,
};

int EXPORT GetRefAPI( int version, ref_interface_t *funcs, ref_api_t *engfuncs, ref_globals_t *globals );
int EXPORT GetRefAPI( int version, ref_interface_t *funcs, ref_api_t *engfuncs, ref_globals_t *globals )
{
	if( version != REF_API_VERSION )
		return 0;

	// fill in our callbacks
	*funcs = gReffuncs;
	gEngfuncs = *engfuncs;
	gpGlobals = globals;

	gp_cl = (ref_client_t *)ENGINE_GET_PARM( PARM_GET_CLIENT_PTR );
	gp_host = (ref_host_t *)ENGINE_GET_PARM( PARM_GET_HOST_PTR );

	return REF_API_VERSION;
}
