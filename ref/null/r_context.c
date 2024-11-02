/*
r_context.c -- null renderer context
Copyright (C) 2023-2024 a1batross

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include <string.h>
#include "xash3d_types.h"
#include "const.h"
#include "cvardef.h"
#include "xash3d_mathlib.h"
#include "ref_api.h"

/*
 * this initially was made to be able to run full client
 * in hazardous^Wheadless environments
 * but might be a starting point for new renderers as well
 */

static ref_api_t      gEngfuncs;
static ref_globals_t *gpGlobals;

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

static void R_SimpleStubBool( qboolean unused )
{
	;
}

static qboolean R_Init( void )
{
	gEngfuncs.R_Init_Video( REF_SOFTWARE );
	return true;
}

static const char *R_GetConfigName( void )
{
	return NULL;
}

static qboolean R_SetDisplayTransform( ref_screen_rotation_t rotate, int x, int y, float scale_x, float scale_y )
{
	return true;
}

static void GL_SetupAttributes( int safegl )
{
	;
}

static qboolean R_AddEntity( struct cl_entity_s *clent, int type )
{
	return true;
}

static void CL_AddCustomBeam( cl_entity_t *pEnvBeam )
{
	;
}

static void R_ProcessEntData( qboolean allocate, cl_entity_t *entities, unsigned int max_entities )
{
	;
}

static const byte *R_GetTextureOriginalBuffer( unsigned int idx )
{
	return NULL;
}

static int GL_LoadTextureFromBuffer( const char *name, rgbdata_t *pic, texFlags_t flags, qboolean update )
{
	return 0;
}

static void GL_ProcessTexture( int texnum, float gamma, int topColor, int bottomColor )
{
	;
}

static void R_SetupSky( int *skytextures )
{
	;
}

static void R_DrawStretchRaw( float x, float y, float w, float h, int cols, int rows, const byte *data, qboolean dirty )
{
	;
}

static void R_DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int texnum )
{
	;
}

static void FillRGBA( int rendermode, float x, float y, float w, float h, byte r, byte g, byte b, byte a )
{
	;
}

static int  WorldToScreen( const vec3_t world, vec3_t screen )
{
	VectorClear( screen );
	return 0;
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

static void R_SetSkyCloudsTextures( int solidskyTexture, int alphaskyTexture )
{
	;
}

static void GL_SubdivideSurface( model_t *mod, msurface_t *fa )
{
	;
}

static void CL_RunLightStyles( lightstyle_t *ls )
{

}

static void R_GetSpriteParms( int *frameWidth, int *frameHeight, int *numFrames, int currentFrame, const model_t *pSprite )
{
	if( frameWidth )
		*frameWidth	= 0;

	if( frameHeight )
		*frameHeight = 0;

	if( numFrames )
		*numFrames = 0;
}

static int R_GetSpriteTexture( const model_t *m_pSpriteModel, int frame )
{
	return 0;
}

static qboolean Mod_ProcessRenderData( model_t *mod, qboolean create, const byte *buffer )
{
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
	return 0;
}

static void GetDetailScaleForTexture( int texture, float *xScale, float *yScale )
{
	*xScale = *yScale = 1.0f;
}

static void GetExtraParmsForTexture( int texture, byte *red, byte *green, byte *blue, byte *alpha )
{
	*red = *green = *blue = *alpha = 0;
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

static int GL_FindTexture( const char *name )
{
	return 0;
}

static const char *GL_TextureName( unsigned int texnum )
{
	return NULL;
}

static const byte *GL_TextureData( unsigned int texnum )
{
	return NULL;
}

static int GL_LoadTexture( const char *name, const byte *buf, size_t size, int flags )
{
	return 0;
}

static int GL_CreateTexture( const char *name, int width, int height, const void *buffer, texFlags_t flags )
{
	return 0;
}

static int GL_LoadTextureArray( const char **names, int flags )
{
	return 0;
}

static int GL_CreateTextureArray( const char *name, int width, int height, int depth, const void *buffer, texFlags_t flags )
{
	return 0;
}

static void R_OverrideTextureSourceSize( unsigned int textnum, unsigned int srcWidth, unsigned int srcHeight )
{

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

static void AVI_UploadRawFrame( int texture, int cols, int rows, int width, int height, const byte *data )
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

static void GL_UpdateTexSize( int texnum, int width, int height, int depth )
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

static void GL_RenderFrame( const struct ref_viewpass_s *rvp )
{
	;
}

static void GL_OrthoBounds( const float *mins, const float *maxs )
{
	;
}

static qboolean R_SpeedsMessage( char *out, size_t size )
{
	return false;
}

static byte *Mod_GetCurrentVis( void )
{
	return NULL;
}

static void *R_GetProcAddress( const char *name )
{
	return NULL;
}

static void Color4f( float r, float g, float b, float a )
{
	;
}

static void Color4ub( unsigned char r, unsigned char g, unsigned char b, unsigned char a )
{
	;
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

static void ScreenToWorld( const float *screen, float *world  )
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
	.R_Shutdown            = R_SimpleStub,
	.R_GetConfigName       = R_GetConfigName,
	.R_SetDisplayTransform = R_SetDisplayTransform,

	.GL_SetupAttributes = GL_SetupAttributes,
	.GL_InitExtensions  = R_SimpleStub,
	.GL_ClearExtensions = R_SimpleStub,

	.R_GammaChanged       = R_SimpleStubBool,
	.R_BeginFrame         = R_SimpleStubBool,
	.R_RenderScene        = R_SimpleStub,
	.R_EndFrame           = R_SimpleStub,
	.R_PushScene          = R_SimpleStub,
	.R_PopScene           = R_SimpleStub,
	.GL_BackendStartFrame = R_SimpleStub,
	.GL_BackendEndFrame   = R_SimpleStub,

	.R_ClearScreen    = R_SimpleStub,
	.R_AllowFog       = R_SimpleStubBool,
	.GL_SetRenderMode = R_SimpleStubInt,

	.R_AddEntity      = R_AddEntity,
	.CL_AddCustomBeam = CL_AddCustomBeam,
	.R_ProcessEntData = R_ProcessEntData,
	.R_Flush          = R_SimpleStubUInt,

	.R_ShowTextures = R_SimpleStub,

	.R_GetTextureOriginalBuffer = R_GetTextureOriginalBuffer,
	.GL_LoadTextureFromBuffer   = GL_LoadTextureFromBuffer,
	.GL_ProcessTexture          = GL_ProcessTexture,
	.R_SetupSky                 = R_SetupSky,

	.R_Set2DMode      = R_SimpleStubBool,
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
	.CL_InitStudioAPI      = R_SimpleStub,

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
	.GL_FreeTexture        = R_SimpleStubUInt,
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

	.GL_RenderFrame    = GL_RenderFrame,
	.GL_OrthoBounds    = GL_OrthoBounds,
	.R_SpeedsMessage   = R_SpeedsMessage,
	.Mod_GetCurrentVis = Mod_GetCurrentVis,
	.R_NewMap          = R_SimpleStub,
	.R_ClearScene      = R_SimpleStub,
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

	return REF_API_VERSION;
}
