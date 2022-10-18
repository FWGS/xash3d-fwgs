/*
vid_sdl.c - SDL vid component
Copyright (C) 2018 a1batross
Copyright (C) 2021 Sergey Galushko

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
#include "gu_local.h"

ref_api_t      gEngfuncs;
ref_globals_t *gpGlobals;

static void R_ClearScreen( void )
{
	sceGuClearColor( 0x00000000 );
	sceGuClear( GU_COLOR_BUFFER_BIT );
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
static void CL_FillRGBA( float _x, float _y, float _w, float _h, int r, int g, int b, int a )
{
	sceGuDisable( GU_TEXTURE_2D );
	sceGuEnable( GU_BLEND );
	sceGuTexFunc( GU_TFX_MODULATE, GU_TCC_RGBA );
	sceGuBlendFunc( GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, GUBLEND1 );
	sceGuColor( GUCOLOR4UB( r, g, b, a ) );

	gu_vert_hv_t* const out = ( gu_vert_hv_t* )sceGuGetMemory( sizeof( gu_vert_hv_t ) * 2 );
	out[0].x = _x;
	out[0].y = _y;
	out[0].z = 0;
	out[1].x = _x + _w;
	out[1].y = _y + _h;
	out[1].z = 0;
	sceGuDrawArray( GU_SPRITES, GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, out );

	sceGuColor( 0xffffffff );
	sceGuEnable( GU_TEXTURE_2D );
	sceGuDisable( GU_BLEND );
}

/*
=============
pfnFillRGBABlend

=============
*/
static void GAME_EXPORT CL_FillRGBABlend( float _x, float _y, float _w, float _h, int r, int g, int b, int a )
{
	sceGuDisable( GU_TEXTURE_2D );
	sceGuEnable( GU_BLEND );
	sceGuTexFunc( GU_TFX_MODULATE, GU_TCC_RGBA );
	sceGuBlendFunc( GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_DST_ALPHA, 0, 0 );
	sceGuColor( GUCOLOR4UB( r, g, b, a ) );

	gu_vert_hv_t* const out = ( gu_vert_hv_t* )sceGuGetMemory( sizeof( gu_vert_hv_t ) * 2 );
	out[0].x = _x;
	out[0].y = _y;
	out[0].z = 0;
	out[1].x = _x + _w;
	out[1].y = _y + _h;
	out[1].z = 0;
	sceGuDrawArray( GU_SPRITES, GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, out );

	sceGuColor( 0xffffffff );
	sceGuEnable( GU_TEXTURE_2D );
	sceGuDisable( GU_BLEND );
}

void Mod_BrushUnloadTextures( model_t *mod )
{
	int i;

	for( i = 0; i < mod->numtextures; i++ )
	{
		texture_t *tx = mod->textures[i];
		if( !tx || tx->gl_texturenum == tr.defaultTexture )
			continue; // free slot

		GL_FreeTexture( tx->gl_texturenum );    // main texture
		GL_FreeTexture( tx->fb_texturenum );    // luma texture
	}
}

void Mod_UnloadTextures( model_t *mod )
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
		Mod_SpriteUnloadTextures( mod->cache.data );
		break;
	default:
		ASSERT( 0 );
		break;
	}
}

qboolean Mod_ProcessRenderData( model_t *mod, qboolean create, const byte *buf )
{
	qboolean loaded = true;

	if( create )
	{
		switch( mod->type )
		{
			case mod_studio:
				// Mod_LoadStudioModel( mod, buf, loaded );
				break;
			case mod_sprite:
				Mod_LoadSpriteModel( mod, buf, &loaded, mod->numtexinfo );
				break;
			case mod_alias:
				Mod_LoadAliasModel( mod, buf, &loaded );
				break;
			case mod_brush:
				// Mod_LoadBrushModel( mod, buf, loaded );
				break;
			default: gEngfuncs.Host_Error( "Mod_LoadModel: unsupported type %d\n", mod->type );
		}
	}

	if( loaded && gEngfuncs.drawFuncs->Mod_ProcessUserData )
		gEngfuncs.drawFuncs->Mod_ProcessUserData( mod, create, buf );

	if( !create )
		Mod_UnloadTextures( mod );

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
		/*glt = R_GetTexture( arg );*/
		return /*glt->encode*/0;
	case PARM_TEX_MIPCOUNT:
		glt = R_GetTexture( arg );
		return glt->numMips;
	case PARM_TEX_DEPTH:
		/*glt = R_GetTexture( arg );*/
		return /*glt->depth*/1;
	case PARM_TEX_SKYBOX:
		Assert( arg >= 0 && arg < 6 );
		return tr.skyboxTextures[arg];
	case PARM_TEX_SKYTEXNUM:
		return tr.skytexturenum;
	case PARM_TEX_LIGHTMAP:
		arg = bound( 0, arg, MAX_LIGHTMAPS - 1 );
		return tr.lightmapTextures[arg];
	case PARM_WIDESCREEN:
		return gpGlobals->wideScreen;
	case PARM_FULLSCREEN:
		return gpGlobals->fullScreen;
	case PARM_SCREEN_WIDTH:
		return gpGlobals->width;
	case PARM_SCREEN_HEIGHT:
		return gpGlobals->height;
	case PARM_TEX_TARGET:
		/*glt = R_GetTexture( arg );*/
		return /*glt->target*/0;
	case PARM_TEX_TEXNUM:
		/*glt = R_GetTexture( arg );*/
		return /*glt->texnum*/0;
	case PARM_TEX_FLAGS:
		glt = R_GetTexture( arg );
		return glt->flags;
	case PARM_ACTIVE_TMU:
		return 0;
	case PARM_LIGHTSTYLEVALUE:
		arg = bound( 0, arg, MAX_LIGHTSTYLES - 1 );
		return tr.lightstylevalue[arg];
	case PARM_MAX_IMAGE_UNITS:
		return 1;
	case PARM_REBUILD_GAMMA:
		return glConfig.softwareGammaUpdate;
	case PARM_SURF_SAMPLESIZE:
		if( arg >= 0 && arg < WORLDMODEL->numsurfaces )
			return gEngfuncs.Mod_SampleSizeForFace( &WORLDMODEL->surfaces[arg] );
		return LM_SAMPLE_SIZE;
	case PARM_GL_CONTEXT_TYPE:
		return CONTEXT_TYPE_GL;
	case PARM_GLES_WRAPPER:
		return GLES_WRAPPER_NONE;
	case PARM_STENCIL_ACTIVE:
		return glState.stencilEnabled;
	case PARM_SKY_SPHERE:
		return ENGINE_GET_PARM_( parm, arg ) && !tr.fCustomSkybox;
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

const byte *GL_TextureData( unsigned int texnum )
{
	rgbdata_t *pic = R_GetTexture( texnum )->original;

	if( pic != NULL )
		return pic->buffer;
	return NULL;
}

void R_ProcessEntData( qboolean allocate )
{
	if( !allocate )
	{
		tr.draw_list->num_solid_entities = 0;
		tr.draw_list->num_trans_entities = 0;
		tr.draw_list->num_beam_entities = 0;
	}

	if( gEngfuncs.drawFuncs->R_ProcessEntData )
		gEngfuncs.drawFuncs->R_ProcessEntData( allocate );
}

qboolean R_SetDisplayTransform( ref_screen_rotation_t rotate, int offset_x, int offset_y, float scale_x, float scale_y )
{
	qboolean ret = true;
	if( rotate > 0 )
	{
		gEngfuncs.Con_Printf("rotation transform not supported\n");
		ret = false;
	}

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

static void* GAME_EXPORT R_GetProcAddress( const char *name )
{
	return gEngfuncs.GL_GetProcAddress( name );
}

static const char *R_GetConfigName( void )
{
	return "ref_gu";
}

ref_interface_t gReffuncs =
{
	R_Init,
	R_Shutdown,
	R_GetConfigName,
	R_SetDisplayTransform,

	GL_SetupAttributes,
	GL_InitExtensions,
	GL_ClearExtensions,

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
	GL_SetColor4ub,

	R_AddEntity,
	CL_AddCustomBeam,
	R_ProcessEntData,

	R_ShowTextures,

	R_GetTextureOriginalBuffer,
	GL_LoadTextureFromBuffer,
	GL_ProcessTexture,
	R_SetupSky,

	R_Set2DMode,
	R_DrawStretchRaw,
	R_DrawStretchPic,
	R_DrawTileClear,
	CL_FillRGBA,
	CL_FillRGBABlend,

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

	R_InitSkyClouds,
	GL_SubdivideSurface,
	CL_RunLightStyles,

	R_GetSpriteParms,
	R_GetSpriteTexture,

	Mod_LoadMapSprite,
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

	getTriAPI,

	VGUI_DrawInit,
	VGUI_DrawShutdown,
	VGUI_SetupDrawingText,
	VGUI_SetupDrawingRect,
	VGUI_SetupDrawingImage,
	VGUI_BindTexture,
	VGUI_EnableTexture,
	VGUI_CreateTexture,
	VGUI_UploadTexture,
	VGUI_UploadTextureBlock,
	VGUI_DrawQuad,
	VGUI_GetTextureSizes,
	VGUI_GenerateTexture,
};

int EXPORT GetRefAPI( int version, ref_interface_t *funcs, ref_api_t *engfuncs, ref_globals_t *globals )
{
	if( version != REF_API_VERSION )
		return 0;

	// fill in our callbacks
	memcpy( funcs, &gReffuncs, sizeof( ref_interface_t ));
	memcpy( &gEngfuncs, engfuncs, sizeof( ref_api_t ));
	gpGlobals = globals;

	return REF_API_VERSION;
}

void EXPORT GetRefHumanReadableName( char *out, size_t size )
{
	Q_strncpy( out, "sceGu", size );
}
