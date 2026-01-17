/*
r_context.c -- dx2 renderer context
Copyright (C) 2023-2024 a1batross
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

#define INITGUID
#include "r_local.h"
#include "xash3d_mathlib.h"

//#pragma comment(lib, "ddraw.lib")
//TODO COM_GetProcAddress

ref_api_t      gEngfuncs;
ref_globals_t *gpGlobals;
ref_client_t  *gp_cl;

static void R_SimpleStub( void )
{
	;
}

static void R_SimpleStubInt( int unused )
{
	;
}

static void R_SimpleStubUInt( unsigned int unused )
{
	;
}

static const char *R_GetConfigName( void )
{
	return "ref_dx2";
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

void GL_BackendStartFrame( void )
{

}

void GL_BackendEndFrame( void )
{

}

void GL_SetRenderMode( int mode )
{
	dxc.renderMode = mode;
}


static void CL_AddCustomBeam( cl_entity_t *pEnvBeam )
{
	;
}

static void R_ProcessEntData( qboolean allocate, cl_entity_t *entities, unsigned int max_entities )
{
	if (!allocate)
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

	if (gEngfuncs.drawFuncs->R_ProcessEntData)
		gEngfuncs.drawFuncs->R_ProcessEntData(allocate);
}

static void R_SetupSky( int *skytextures )
{
	;
}

static qboolean VID_ScreenShot( const char *filename, int shot_type )
{
	return false;
}

static qboolean VID_CubemapShot( const char *base, uint size, const float *vieworg, qboolean skyshot )
{
	return false;
}

static colorVec R_LightPoint( const float *p )
{
	colorVec c = { 0 };
	return c;
}

static void R_DecalShoot( int textureIndex, int entityIndex, int modelIndex, vec3_t pos, int flags, float scale )
{
	;
}

static int R_CreateDecalList( struct decallist_s *pList )
{
	return 0;
}

static float R_StudioEstimateFrame( cl_entity_t *e, mstudioseqdesc_t *pseqdesc, double time )
{
	return 0.0f;
}

static void R_StudioLerpMovement( cl_entity_t *e, double time, vec3_t origin, vec3_t angles )
{
	;
}

void CL_InitStudioAPI( void )
{
	;
}

static void R_SetSkyCloudsTextures( int solidskyTexture, int alphaskyTexture )
{
	;
}

static void CL_RunLightStyles( lightstyle_t *ls )
{

}

static qboolean Mod_ProcessRenderData( model_t *mod, qboolean create, const byte *buffer, size_t buffersize )
{
	gEngfuncs.Con_Printf("Mod_ProcessRenderData(%p(%s), %d, %p, %zu)\n", mod, mod->name, create, buffer, buffersize);
	return true;
}

static void Mod_StudioLoadTextures( model_t *mod, void *data )
{
	;
}

static void CL_DrawParticles( double frametime, particle_t *particles, float partsize )
{
	;
}

static void CL_DrawTracers( double frametime, particle_t *tracers )
{
	;
}

static void CL_DrawBeams( int fTrans, BEAM *beams )
{
	;
}

static qboolean R_BeamCull( const vec3_t start, const vec3_t end, qboolean pvsOnly )
{
	return false;
}

static int RefGetParm( int parm, int arg )
{
	dx_texture_t *glt;

	switch (parm)
	{
	case PARM_TEX_WIDTH:
		glt = R_GetTexture(arg);
		return glt->width;
	case PARM_TEX_HEIGHT:
		glt = R_GetTexture(arg);
		return glt->height;
	case PARM_TEX_SRC_WIDTH:
		glt = R_GetTexture(arg);
		return glt->srcWidth;
	case PARM_TEX_SRC_HEIGHT:
		glt = R_GetTexture(arg);
		return glt->srcHeight;
	case PARM_TEX_GLFORMAT:
		glt = R_GetTexture(arg);
		return 0;
	case PARM_TEX_ENCODE:
		glt = R_GetTexture(arg);
		return 0;
	case PARM_TEX_MIPCOUNT:
		glt = R_GetTexture(arg);
		return glt->numMips;
	case PARM_TEX_DEPTH:
		glt = R_GetTexture(arg);
		return 1;
#if 0
	case PARM_TEX_SKYBOX:
		Assert(arg >= 0 && arg < 6);
		return tr.skyboxTextures[arg];
	case PARM_TEX_SKYTEXNUM:
		return tr.skytexturenum;
	case PARM_TEX_LIGHTMAP:
		arg = bound(0, arg, MAX_LIGHTMAPS - 1);
		return tr.lightmapTextures[arg];
#endif
	case PARM_TEX_TARGET:
		glt = R_GetTexture(arg);
		return 0;
	case PARM_TEX_TEXNUM:
		glt = R_GetTexture(arg);
		return arg;
	case PARM_TEX_FLAGS:
		glt = R_GetTexture(arg);
		return glt->flags;
#if 0
	case PARM_TEX_MEMORY:
		return GL_TexMemory();
	case PARM_ACTIVE_TMU:
		return glState.activeTMU;
	case PARM_LIGHTSTYLEVALUE:
		arg = bound(0, arg, MAX_LIGHTSTYLES - 1);
		return tr.lightstylevalue[arg];
	case PARM_MAX_IMAGE_UNITS:
		return GL_MaxTextureUnits();
	case PARM_REBUILD_GAMMA:
		return glConfig.softwareGammaUpdate;
	case PARM_GL_CONTEXT_TYPE:
		return glConfig.context;
	case PARM_GLES_WRAPPER:
		return glConfig.wrapper;
#endif
	case PARM_STENCIL_ACTIVE:
		return 0;//No stencil in dx2
	case PARM_TEX_FILTERING:
		return 0;
#if 0
		if (arg < 0)
			return gl_texture_nearest.value == 0.0f;

		return GL_TextureFilteringEnabled(R_GetTexture(arg));
#endif
	default:
		return (*gEngfuncs.EngineGetParm)(parm, arg);
	}
	return 0;
}


static float GetFrameTime( void )
{
	return 0.0f;
}

static void R_SetCurrentEntity( struct cl_entity_s *ent )
{
	;
}

static void R_SetCurrentModel( struct model_s *mod )
{
	;
}

static void DrawSingleDecal( struct decal_s *pDecal, struct msurface_s *fa )
{
	;
}

static float *R_DecalSetupVerts( struct decal_s *pDecal, struct msurface_s *surf, int texture, int *outCount )
{
	return NULL;
}

static void R_EntityRemoveDecals( struct model_s *mod )
{
	;
}


static void GL_Bind( int tmu, unsigned int texnum )
{
	;
}

static void GL_LoadTextureMatrix( const float *glmatrix )
{
	;
}

static void GL_TexGen( unsigned int coord, unsigned int mode )
{
	;
}

static void GL_DrawParticles( const struct ref_viewpass_s *rvp, qboolean trans_pass, float frametime )
{
	;
}

static colorVec LightVec( const float *start, const float *end, float *lightspot, float *lightvec )
{
	colorVec c = { 0 };
	return c;
}

static struct mstudiotex_s *StudioGetTexture( struct cl_entity_s *e )
{
	return NULL;
}

static qboolean R_SpeedsMessage( char *out, size_t size )
{
	return false;
}

static void R_NewMap( void )
{

	if (gEngfuncs.drawFuncs->R_NewMap != NULL)
		gEngfuncs.drawFuncs->R_NewMap();
}

static void *R_GetProcAddress( const char *name )
{
	return NULL;
}

static void Color4f( float r, float g, float b, float a )
{
	Vector4Set(dxc.currentColor, r, g, b, a);
}

static void Color4ub( unsigned char r, unsigned char g, unsigned char b, unsigned char a )
{
	Vector4Set(dxc.currentColor, r/255.f, g/255.f, b/255.f, a/255.f);
}

static void TexCoord2f( float u, float v )
{
	;
}

static void Vertex3fv( const float *worldPnt )
{
	;
}

static void Vertex3f( float x, float y, float z )
{
	;
}

static void Fog( float flFogColor[3], float flStart, float flEnd, int bOn )
{
	;
}

static void GetMatrix( const int pname, float *matrix )
{
	;
}

static void FogParams( float flDensity, int iFogSkybox )
{
	;
}

static void CullFace( TRICULLSTYLE mode )
{
	;
}

static void VGUI_SetupDrawing( qboolean rect )
{
	;
}

static void VGUI_UploadTextureBlock( int drawX, int drawY, const byte *rgba, int blockWidth, int blockHeight )
{
	;
}

static const ref_interface_t gReffuncs =
{
	.R_Init                = R_Init,
	.R_Shutdown            = R_Shutdown,
	.R_GetConfigName       = R_GetConfigName,
	.R_SetDisplayTransform = R_SetDisplayTransform,

	.GL_SetupAttributes = GL_SetupAttributes,
	.GL_InitExtensions  = GL_InitExtensions,
	.GL_ClearExtensions = GL_ClearExtensions,

	.R_GammaChanged       = R_GammaChanged,
	.R_BeginFrame         = R_BeginFrame,
	.R_RenderScene        = R_RenderScene,
	.R_EndFrame           = R_EndFrame,
	.R_PushScene          = R_SimpleStub,
	.R_PopScene           = R_SimpleStub,
	.GL_BackendStartFrame = GL_BackendStartFrame,
	.GL_BackendEndFrame   = GL_BackendEndFrame,

	.R_ClearScreen    = R_SimpleStub,
	.R_AllowFog       = R_AllowFog,
	.GL_SetRenderMode = GL_SetRenderMode,

	.R_AddEntity      = R_AddEntity,
	.CL_AddCustomBeam = CL_AddCustomBeam,
	.R_ProcessEntData = R_ProcessEntData,
	.R_Flush          = R_SimpleStubUInt,

	.R_ShowTextures = R_ShowTextures,

	.R_GetTextureOriginalBuffer = R_GetTextureOriginalBuffer,
	.GL_LoadTextureFromBuffer   = GL_LoadTextureFromBuffer,
	.GL_ProcessTexture          = GL_ProcessTexture,
	.R_SetupSky                 = R_SetupSky,

	.R_Set2DMode      = R_Set2DMode,
	.R_DrawStretchRaw = R_DrawStretchRaw,
	.R_DrawStretchPic = R_DrawStretchPic,
	.FillRGBA         = FillRGBA,
	.WorldToScreen    = WorldToScreen,

	.VID_ScreenShot  = VID_ScreenShot,
	.VID_CubemapShot = VID_CubemapShot,

	.R_LightPoint = R_LightPoint,

	.R_DecalShoot      = R_DecalShoot,
	.R_DecalRemoveAll  = R_SimpleStubInt,
	.R_CreateDecalList = R_CreateDecalList,
	.R_ClearAllDecals  = R_SimpleStub,

	.R_StudioEstimateFrame = R_StudioEstimateFrame,
	.R_StudioLerpMovement  = R_StudioLerpMovement,
	.CL_InitStudioAPI      = CL_InitStudioAPI,

	.R_SetSkyCloudsTextures     = R_SetSkyCloudsTextures,
	.GL_SubdivideSurface = GL_SubdivideSurface,
	.CL_RunLightStyles   = CL_RunLightStyles,

	.R_GetSpriteParms    = R_GetSpriteParms,
	.R_GetSpriteTexture  = R_GetSpriteTexture,

	.Mod_ProcessRenderData  = Mod_ProcessRenderData,
	.Mod_StudioLoadTextures = Mod_StudioLoadTextures,

	.CL_DrawParticles = CL_DrawParticles,
	.CL_DrawTracers   = CL_DrawTracers,
	.CL_DrawBeams     = CL_DrawBeams,
	.R_BeamCull       = R_BeamCull,

	.RefGetParm               = RefGetParm,
	.GetDetailScaleForTexture = GetDetailScaleForTexture,
	.GetExtraParmsForTexture  = GetExtraParmsForTexture,
	.GetFrameTime             = GetFrameTime,

	.R_SetCurrentEntity = R_SetCurrentEntity,
	.R_SetCurrentModel  = R_SetCurrentModel,

	.GL_FindTexture        = GL_FindTexture,
	.GL_TextureName        = GL_TextureName,
	.GL_TextureData        = GL_TextureData,
	.GL_LoadTexture        = GL_LoadTexture,
	.GL_CreateTexture      = GL_CreateTexture,
	.GL_LoadTextureArray   = GL_LoadTextureArray,
	.GL_CreateTextureArray = GL_CreateTextureArray,
	.GL_FreeTexture        = GL_FreeTexture,
	.R_OverrideTextureSourceSize = R_OverrideTextureSourceSize,

	.DrawSingleDecal      = DrawSingleDecal,
	.R_DecalSetupVerts    = R_DecalSetupVerts,
	.R_EntityRemoveDecals = R_EntityRemoveDecals,

	.AVI_UploadRawFrame = AVI_UploadRawFrame,

	.GL_Bind                = GL_Bind,
	.GL_SelectTexture       = R_SimpleStubInt,
	.GL_LoadTextureMatrix   = GL_LoadTextureMatrix,
	.GL_TexMatrixIdentity   = R_SimpleStub,
	.GL_CleanUpTextureUnits = R_SimpleStubInt,
	.GL_TexGen              = GL_TexGen,
	.GL_TextureTarget       = R_SimpleStubUInt,
	.GL_TexCoordArrayMode   = R_SimpleStubUInt,
	.GL_UpdateTexSize       = GL_UpdateTexSize,
	.GL_Reserved0           = NULL,
	.GL_Reserved1           = NULL,

	.GL_DrawParticles = GL_DrawParticles,
	.LightVec         = LightVec,
	.StudioGetTexture = StudioGetTexture,

	.GL_RenderFrame    = R_RenderFrame,
	.GL_OrthoBounds    = GL_OrthoBounds,
	.R_SpeedsMessage   = R_SpeedsMessage,
	.Mod_GetCurrentVis = Mod_GetCurrentVis,
	.R_NewMap          = R_NewMap,
	.R_ClearScene      = R_ClearScene,
	.R_GetProcAddress  = R_GetProcAddress,

	.TriRenderMode = R_SimpleStubInt,
	.Begin         = R_SimpleStubInt,
	.End           = R_SimpleStub,
	.Color4f       = Color4f,
	.Color4ub      = Color4ub,
	.TexCoord2f    = TexCoord2f,
	.Vertex3fv     = Vertex3fv,
	.Vertex3f      = Vertex3f,
	.Fog           = Fog,
	.ScreenToWorld = ScreenToWorld,
	.GetMatrix     = GetMatrix,
	.FogParams     = FogParams,
	.CullFace      = CullFace,

	.VGUI_SetupDrawing   = VGUI_SetupDrawing,
	.VGUI_UploadTextureBlock = VGUI_UploadTextureBlock,
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
	gp_cl = (ref_client_t *)gEngfuncs.EngineGetParm(PARM_GET_CLIENT_PTR, 0);

	return REF_API_VERSION;
}
