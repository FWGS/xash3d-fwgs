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

// GL API function pointers, if any, reside in this translation unit
#define APIENTRY_LINKAGE
#include "gl_local.h"
#include "gl_export.h"

#if XASH_GL4ES
#include "gl4es/include/gl4esinit.h"
#endif



static void R_ClearScreen( void )
{
	pglClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	pglClear( GL_COLOR_BUFFER_BIT );
}

static const byte *R_GetTextureOriginalBuffer( unsigned int idx )
{
	gl_texture_t *glt = R_GetTexture( idx );

	if( !glt || !glt->original || !glt->original->buffer )
		return NULL;

	return glt->original->buffer;
}

/*
=============
CL_FillRGBA

=============
*/
static void CL_FillRGBA( int rendermode, float _x, float _y, float _w, float _h, byte r, byte g, byte b, byte a )
{
	pglDisable( GL_TEXTURE_2D );
	pglEnable( GL_BLEND );
	pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
	if( rendermode == kRenderTransAdd )
		pglBlendFunc( GL_SRC_ALPHA, GL_ONE );
	else
		pglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	pglColor4ub( r, g, b, a );

	pglBegin( GL_QUADS );
		pglVertex2f( _x, _y );
		pglVertex2f( _x + _w, _y );
		pglVertex2f( _x + _w, _y + _h );
		pglVertex2f( _x, _y + _h );
	pglEnd ();

	pglEnable( GL_TEXTURE_2D );
	pglDisable( GL_BLEND );
}

static qboolean Mod_LooksLikeWaterTexture( const char *name )
{
	if(( name[0] == '*' && Q_stricmp( name, REF_DEFAULT_TEXTURE )) || name[0] == '!' )
		return true;

	if( !FBitSet( gp_host->features, ENGINE_QUAKE_COMPATIBLE ))
	{
		if( !Q_strncmp( name, "water", 5 ) || !Q_strnicmp( name, "laser", 5 ))
			return true;
	}

	return false;
}

static void Mod_BrushUnloadTextures( model_t *mod )
{
	int i;

	for( i = 0; i < mod->numtextures; i++ )
	{
		texture_t *tx = mod->textures[i];
		if( !tx )
			continue; // free slot

		if( tx->gl_texturenum != tr.defaultTexture )
			GL_FreeTexture( tx->gl_texturenum ); // main texture

		if( !Mod_LooksLikeWaterTexture( tx->name ))
		{
			GL_FreeTexture( tx->fb_texturenum ); // luma texture
			GL_FreeTexture( tx->dt_texturenum ); // detail texture
		}
	}
}

static void Mod_UnloadTextures( model_t *mod )
{
	Assert( mod != NULL );

	switch( mod->type )
	{
	case mod_studio:
		Mod_StudioUnloadTextures( mod->cache.data );
		break;
	case mod_alias:
		Mod_AliasUnloadTextures( mod->cache.data );
		break;
	case mod_brush:
		Mod_BrushUnloadTextures( mod );
		break;
	case mod_sprite:
		break;
	default:
		Assert( 0 );
		break;
	}
}

static qboolean Mod_ProcessRenderData( model_t *mod, qboolean create, const byte *buf, size_t buffersize )
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
		loaded = true;
		break;
	case mod_sprite:
		loaded = true;
		break;
	case mod_alias:
		Mod_LoadAliasModel( mod, buf, &loaded );
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
	case PARM_TEX_SKYBOX:
		Assert( arg >= 0 && arg < 6 );
		return tr.skyboxTextures[arg];
	case PARM_TEX_SKYTEXNUM:
		return tr.skytexturenum;
	case PARM_TEX_LIGHTMAP:
		arg = bound( 0, arg, MAX_LIGHTMAPS - 1 );
		return tr.lightmapTextures[arg];
	case PARM_TEX_TARGET:
		glt = R_GetTexture( arg );
		return glt->target;
	case PARM_TEX_TEXNUM:
		glt = R_GetTexture( arg );
		return glt->texnum;
	case PARM_TEX_FLAGS:
		glt = R_GetTexture( arg );
		return glt->flags;
	case PARM_TEX_MEMORY:
		return GL_TexMemory();
	case PARM_ACTIVE_TMU:
		return glState.activeTMU;
	case PARM_LIGHTSTYLEVALUE:
		arg = bound( 0, arg, MAX_LIGHTSTYLES - 1 );
		return g_lightstylevalue[arg];
	case PARM_MAX_IMAGE_UNITS:
		return GL_MaxTextureUnits();
	case PARM_REBUILD_GAMMA:
		return glConfig.softwareGammaUpdate;
	case PARM_GL_CONTEXT_TYPE:
		return glConfig.context;
	case PARM_GLES_WRAPPER:
		return glConfig.wrapper;
	case PARM_STENCIL_ACTIVE:
		return glState.stencilEnabled;
	case PARM_TEX_FILTERING:
		if( arg < 0 )
			return gl_texture_nearest.value == 0.0f;

		return GL_TextureFilteringEnabled( R_GetTexture( arg ));
	default:
		return ENGINE_GET_PARM_( parm, arg );
	}
	return 0;
}

static void R_GetDetailScaleForTexture( int texture, float *xScale, float *yScale )
{
	gl_texture_t *glt = R_GetTexture( texture );

	if( xScale ) *xScale = glt->xscale;
	if( yScale ) *yScale = glt->yscale;
}

static void R_SetDetailScaleForTexture( int texture, float xScale, float yScale )
{
	gl_texture_t *glt = R_GetTexture( texture );

	glt->xscale = xScale;
	glt->yscale = yScale;
}

static void R_GetExtraParmsForTexture( int texture, byte *red, byte *green, byte *blue, byte *density )
{
	gl_texture_t *glt = R_GetTexture( texture );

	if( red ) *red = glt->fogParams[0];
	if( green ) *green = glt->fogParams[1];
	if( blue ) *blue = glt->fogParams[2];
	if( density ) *density = glt->fogParams[3];
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

static float R_GetFrameTime( void )
{
	return tr.frametime;
}

static const char *GL_TextureName( unsigned int texnum )
{
	return R_GetTexture( texnum )->name;
}

static const byte *GL_TextureData( unsigned int texnum )
{
	rgbdata_t *pic = R_GetTexture( texnum )->original;

	if( pic != NULL )
		return pic->buffer;
	return NULL;
}

static void R_ProcessEntData( qboolean allocate, cl_entity_t *entities, unsigned int max_entities )
{
	if( !allocate )
	{
		tr.draw_list->num_solid_entities = 0;
		tr.draw_list->num_trans_entities = 0;
		tr.draw_list->num_beam_entities = 0;

		tr.max_entities = 0;
		tr.entities = NULL;
	}
	else
	{
		tr.max_entities = max_entities;
		tr.entities = entities;
	}

	if( gEngfuncs.drawFuncs->R_ProcessEntData )
		gEngfuncs.drawFuncs->R_ProcessEntData( allocate );
}

/*
=============
R_SetSkyCloudsTextures

Quake sky cloud texture was processed by the engine,
remember them for easier access during rendering
==============
*/
static void GAME_EXPORT R_SetSkyCloudsTextures( int solidskyTexture, int alphaskyTexture )
{
	tr.solidskyTexture = solidskyTexture;
	tr.alphaskyTexture = alphaskyTexture;
}

/*
===============
R_SetupSky
===============
*/
static void GAME_EXPORT R_SetupSky( int *skyboxTextures )
{
	int i;

	R_UnloadSkybox();

	if( !skyboxTextures )
		return;

	for( i = 0; i < SKYBOX_MAX_SIDES; i++ )
		tr.skyboxTextures[i] = skyboxTextures[i];
}

static qboolean R_SetDisplayTransform( ref_screen_rotation_t rotate, int offset_x, int offset_y, float scale_x, float scale_y )
{
	qboolean ret = true;

	tr.rotation = rotate;

	if( offset_x || offset_y )
	{
		gEngfuncs.Con_Printf("offset transform not supported\n");
		ret = false;
	}

	if( scale_x != 1.0f || scale_y != 1.0f )
	{
		gEngfuncs.Con_Printf("scale transform not supported\n");
		ret = false;
	}

	return ret;
}

static void GAME_EXPORT VGUI_SetupDrawing( qboolean rect )
{
	pglEnable( GL_BLEND );
	pglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

	if( rect )
	{
		pglDisable( GL_ALPHA_TEST );
	}
	else
	{
		pglEnable( GL_ALPHA_TEST );
		pglAlphaFunc( GL_GREATER, 0.0f );
		pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
	}
}

static void GAME_EXPORT R_OverrideTextureSourceSize( unsigned int texnum, uint srcWidth, uint srcHeight )
{
	gl_texture_t *tx = R_GetTexture( texnum );

	tx->srcWidth = srcWidth;
	tx->srcHeight = srcHeight;
}

static void* GAME_EXPORT R_GetProcAddress( const char *name )
{
#if XASH_GL4ES
	return gl4es_GetProcAddress( name );
#else // TODO: other wrappers
	return gEngfuncs.GL_GetProcAddress( name );
#endif
}

static const char *R_GetConfigName( void )
{
	return "opengl";
}

static void R_NewMap( void )
{
	texture_t	*tx;
	int	i;

	tr.worldmodel = gp_cl->models[1];

	R_ClearDecals(); // clear all level decals

	R_StudioResetPlayerModels();

	// clear out efrags in case the level hasn't been reloaded
	for( i = 0; i < WORLDMODEL->numleafs; i++ )
		WORLDMODEL->leafs[i+1].efrags = NULL;

	glState.isFogEnabled = false;
	tr.skytexturenum = -1;
	pglDisable( GL_FOG );

	// clearing texture chains
	for( i = 0; i < WORLDMODEL->numtextures; i++ )
	{
		if( !WORLDMODEL->textures[i] )
			continue;

		tx = WORLDMODEL->textures[i];

		if( !Q_strncmp( tx->name, "sky", 3 ) && tx->width == ( tx->height * 2 ))
			tr.skytexturenum = i;

		tx->texturechain = NULL;
	}

	GL_BuildLightmaps ();

	R_ClearVBO();
	if( R_HasEnabledVBO( ))
		R_GenerateVBO();
	R_ResetRipples();

	if( gEngfuncs.drawFuncs->R_NewMap != NULL )
		gEngfuncs.drawFuncs->R_NewMap();
}

static void R_FillRenderAPI( render_api_t *api )
{
	api->GetExtraParmsForTexture  = R_GetExtraParmsForTexture;
	api->GetFrameTime             = R_GetFrameTime;
	api->R_SetCurrentEntity       = R_SetCurrentEntity;
	api->R_SetCurrentModel        = R_SetCurrentModel;
	api->GL_CreateTexture         = GL_CreateTexture;
	api->GL_LoadTextureArray      = GL_LoadTextureArray;
	api->GL_CreateTextureArray    = GL_CreateTextureArray;
	api->DrawSingleDecal          = DrawSingleDecal;
	api->R_DecalSetupVerts        = R_DecalSetupVerts;
	api->R_EntityRemoveDecals     = R_EntityRemoveDecals;
	api->GL_SelectTexture         = GL_SelectTexture;
	api->GL_LoadTextureMatrix     = GL_LoadTexMatrixExt;
	api->GL_TexMatrixIdentity     = GL_LoadIdentityTexMatrix;
	api->GL_CleanUpTextureUnits   = GL_CleanUpTextureUnits;
	api->GL_TexGen                = GL_TexGen;
	api->GL_TextureTarget         = GL_TextureTarget;
	api->GL_TexCoordArrayMode     = GL_SetTexCoordArrayMode;
	api->GL_UpdateTexSize         = GL_UpdateTexSize;
	api->GL_DrawParticles         = CL_DrawParticlesExternal;
	api->LightVec                 = R_LightVec;
	api->StudioGetTexture         = R_StudioGetTexture;
	api->GL_GetProcAddress        = R_GetProcAddress;
}

static void R_FillTriAPI( triangleapi_t *api )
{
	api->TexCoord2f    = TriTexCoord2f;
	api->Fog           = TriFog;
	api->ScreenToWorld = R_ScreenToWorld;
	api->GetMatrix     = TriGetMatrix;
	api->FogParams     = TriFogParams;
}

const ref_interface_t gReffuncs =
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
	R_ProcessEntData,

	R_ShowTextures,

	R_GetTextureOriginalBuffer,
	GL_LoadTextureFromBuffer,
	GL_ProcessTexture,
	R_SetupSky,

	R_Set2DMode,
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
	R_StudioFillAPI,
	R_StudioSetDrawInterface,

	R_SetSkyCloudsTextures,
	GL_SubdivideSurface,
	CL_RunLightStyles,


	Mod_ProcessRenderData,
	Mod_StudioLoadTextures,

	CL_DrawParticles,
	CL_DrawTracers,
	CL_DrawBeams,

	GL_RefGetParm,

	R_GetDetailScaleForTexture,
	R_SetDetailScaleForTexture,

	GL_CreateTexture,
	GL_FindTexture,
	GL_TextureName,
	GL_TextureData,
	GL_LoadTexture,
	GL_FreeTexture,
	R_OverrideTextureSourceSize,

	GL_UpdateTexture,

	GL_Bind,

	R_RenderFrame,
	Mod_SetOrthoBounds,
	R_SpeedsMessage,
	Mod_GetCurrentVis,
	R_NewMap,
	R_ClearScene,

	TriRenderMode,
	TriBegin,
	TriEnd,
	_TriColor4f,
	_TriColor4ub,
	TriVertex3fv,
	TriVertex3f,
	TriCullFace,

	R_FillRenderAPI,
	R_FillTriAPI,

	VGUI_SetupDrawing,
};

