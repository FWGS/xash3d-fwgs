#include "vk_core.h"
#include "vk_common.h"
#include "vk_textures.h"
#include "vk_renderstate.h"
#include "xash3d_types.h"
#include "cvardef.h"
#include "const.h"
#include "ref_api.h"
#include "crtlib.h"
#include "com_strings.h"

#include <memory.h>
#include <stdio.h>

ref_api_t gEngine = {0};
ref_globals_t* gGlobals = {0};

static const char* getParamName(int parm)
{
	#define CASE_STR(cond) case cond: return #cond;
	switch (parm)
	{
		CASE_STR(PARM_TEX_WIDTH)
		CASE_STR(PARM_TEX_HEIGHT)	
		CASE_STR(PARM_TEX_SRC_WIDTH)
		CASE_STR(PARM_TEX_SRC_HEIGHT)
		CASE_STR(PARM_TEX_SKYBOX)	
		CASE_STR(PARM_TEX_SKYTEXNUM)
		CASE_STR(PARM_TEX_LIGHTMAP)
		CASE_STR(PARM_TEX_TARGET)	
		CASE_STR(PARM_TEX_TEXNUM)
		CASE_STR(PARM_TEX_FLAGS)
		CASE_STR(PARM_TEX_DEPTH)	
		CASE_STR(PARM_TEX_GLFORMAT)
		CASE_STR(PARM_TEX_ENCODE)
		CASE_STR(PARM_TEX_MIPCOUNT)	
		CASE_STR(PARM_BSP2_SUPPORTED)
		CASE_STR(PARM_SKY_SPHERE)
		CASE_STR(PARAM_GAMEPAUSED)	
		CASE_STR(PARM_MAP_HAS_DELUXE)
		CASE_STR(PARM_MAX_ENTITIES)
		CASE_STR(PARM_WIDESCREEN)	
		CASE_STR(PARM_FULLSCREEN)
		CASE_STR(PARM_SCREEN_WIDTH)
		CASE_STR(PARM_SCREEN_HEIGHT)	
		CASE_STR(PARM_CLIENT_INGAME)
		CASE_STR(PARM_FEATURES)
		CASE_STR(PARM_ACTIVE_TMU)	
		CASE_STR(PARM_LIGHTSTYLEVALUE)
		CASE_STR(PARM_MAX_IMAGE_UNITS)
		CASE_STR(PARM_CLIENT_ACTIVE)
		CASE_STR(PARM_REBUILD_GAMMA)	
		CASE_STR(PARM_DEDICATED_SERVER)
		CASE_STR(PARM_SURF_SAMPLESIZE)
		CASE_STR(PARM_GL_CONTEXT_TYPE)
		CASE_STR(PARM_GLES_WRAPPER)	
		CASE_STR(PARM_STENCIL_ACTIVE)
		CASE_STR(PARM_WATER_ALPHA)
		CASE_STR(PARM_TEX_MEMORY)	
		CASE_STR(PARM_DELUXEDATA)
		CASE_STR(PARM_SHADOWDATA)
		default: return "UNKNOWN";
	}
	#undef CASE_STR
}

const char *R_VkGetConfigName( void )
{
    gEngine.Con_Printf("VKFIXME: %s\n", __FUNCTION__);
    return "vk";
}

qboolean R_VkSetDisplayTransform( ref_screen_rotation_t rotate, int x, int y, float scale_x, float scale_y )
{
    gEngine.Con_Printf("VK FIXME: %s(%d, %d, %f, %f)\n", __FUNCTION__, x, y, scale_x, scale_y);
    return true;
}

// only called for GL contexts
void GL_SetupAttributes( int safegl )
{

}
void GL_InitExtensions( void )
{

}
void GL_ClearExtensions( void )
{

}

void VK_BeginFrame( qboolean clearScene )
{

}
void R_RenderScene( void )
{

}
void VK_EndFrame( void )
{

}
void R_PushScene( void )
{

}
void R_PopScene( void )
{

}
void GL_BackendStartFrame( void )
{

}
void GL_BackendEndFrame( void )
{

}

void VK_ClearScreen( void )
{

}


qboolean R_AddEntity( struct cl_entity_s *clent, int type )
{
    return false;
}
void CL_AddCustomBeam( cl_entity_t *pEnvBeam )
{

}
void R_ProcessEntData( qboolean allocate )
{

}

// debug
void R_ShowTextures( void )
{

}

// texture management
const byte *R_GetTextureOriginalBuffer( unsigned int idx )
{
    return NULL;
}

void GL_ProcessTexture( int texnum, float gamma, int topColor, int bottomColor )
{

}
void R_SetupSky( const char *skyname )
{

}

// 2D
void R_DrawStretchRaw( float x, float y, float w, float h, int cols, int rows, const byte *data, qboolean dirty )
{

}
void R_DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int texnum )
{

}
void R_DrawTileClear( int texnum, int x, int y, int w, int h )
{

}

void CL_FillRGBA( float x, float y, float w, float h, int r, int g, int b, int a )
{

}

void CL_FillRGBABlend( float x, float y, float w, float h, int r, int g, int b, int a )
{

}
int R_WorldToScreen( const vec3_t world, vec3_t screen )
{
    return 0;
}

// screenshot, cubemapshot
qboolean VID_ScreenShot( const char *filename, int shot_type )
{
    return false;
}
qboolean VID_CubemapShot( const char *base, uint size, const float *vieworg, qboolean skyshot )
{
    return false;
}

// light
colorVec R_LightPoint( const float *p )
{
    return (colorVec){0};
}
// decals
// Shoots a decal onto the surface of the BSP.  position is the center of the decal in world coords
void R_DecalShoot( int textureIndex, int entityIndex, int modelIndex, vec3_t pos, int flags, float scale )
{

}
void R_DecalRemoveAll( int texture )
{

}
int R_CreateDecalList( struct decallist_s *pList )
{
    return 0;
}
void R_ClearAllDecals( void )
{

}

// studio interface
float R_StudioEstimateFrame( cl_entity_t *e, mstudioseqdesc_t *pseqdesc )
{
    return 0.0;
}
void R_StudioLerpMovement( cl_entity_t *e, double time, vec3_t origin, vec3_t angles )
{

}
void CL_InitStudioAPI( void )
{

}

// bmodel
void R_InitSkyClouds( struct mip_s *mt, struct texture_s *tx, qboolean custom_palette )
{

}
void GL_SubdivideSurface( msurface_t *fa )
{

}
void CL_RunLightStyles( void )
{

}

// sprites
void R_GetSpriteParms( int *frameWidth, int *frameHeight, int *numFrames, int currentFrame, const model_t *pSprite )
{

}
int R_GetSpriteTexture( const model_t *m_pSpriteModel, int frame )
{
    return 0;
}

// model management
// flags ignored for everything except spritemodels
void Mod_LoadMapSprite( struct model_s *mod, const void *buffer, size_t size, qboolean *loaded )
{

}
qboolean Mod_ProcessRenderData( model_t *mod, qboolean create, const byte *buffer )
{
    return false;
}
void Mod_StudioLoadTextures( model_t *mod, void *data )
{

}

// efx implementation
void CL_DrawParticles( double frametime, particle_t *particles, float partsize )
{

}
void CL_DrawTracers( double frametime, particle_t *tracers )
{

}
void CL_DrawBeams( int fTrans , BEAM *beams )
{

}
qboolean R_BeamCull( const vec3_t start, const vec3_t end, qboolean pvsOnly )
{
    return false;
}

// Xash3D Render Interface
// Get renderer info (doesn't changes engine state at all)
int	VK_RefGetParm( int param, int arg )
{
	vk_texture_t *tex = NULL;
	gEngine.Con_Printf("VK FIXME: %s(%s(%d),%d)\n",__FILE__, 
		__FUNCTION__,getParamName(param), param, arg);

	switch (param)
	{
	case PARM_TEX_WIDTH: 
	case PARM_TEX_SRC_WIDTH:
		tex = findTexture(arg);
		return tex->width;
		break;
	case PARM_TEX_HEIGHT:
	case PARM_TEX_SRC_HEIGHT:
		tex = findTexture(arg);
		return tex->height;
		break;

	default:
		break;
	}

	return 0;
}
void R_GetDetailScaleForTexture( int texture, float *xScale, float *yScale )
{

}
void R_GetExtraParmsForTexture( int texture, byte *red, byte *green, byte *blue, byte *alpha )
{

}
float R_GetFrameTime( void )
{
    return 0.0;
}

// Set renderer info (tell engine about changes)
void R_SetCurrentEntity( struct cl_entity_s *ent )
{

}
void R_SetCurrentModel( struct model_s *mod )
{

}


// Decals manipulating (draw & remove)
void DrawSingleDecal( struct decal_s *pDecal, struct msurface_s *fa )
{

}

float *R_DecalSetupVerts( struct decal_s *pDecal, struct msurface_s *surf, int texture, int *outCount )
{
    return NULL;
}

void R_EntityRemoveDecals( struct model_s *mod )
{

}

// AVI
void AVI_UploadRawFrame( int texture, int cols, int rows, int width, int height, const byte *data )
{

}

// glState related calls (must use this instead of normal gl-calls to prevent de-synchornize local states between engine and the client)
void GL_Bind( int tmu, unsigned int texnum )
{

}

void GL_SelectTexture( int tmu )
{

}

void GL_LoadTextureMatrix( const float *glmatrix )
{

}

void GL_TexMatrixIdentity( void )
{

}

void GL_CleanUpTextureUnits( int last )
{

}

void GL_TexGen( unsigned int coord, unsigned int mode )
{

}
void GL_TextureTarget( unsigned int target )
{

}
void GL_TexCoordArrayMode( unsigned int texmode )
{

}
void GL_UpdateTexSize( int texnum, int width, int height, int depth )
{

}
void GL_Reserved0( void )
{

}
void GL_Reserved1( void )
{

}

// Misc renderer functions
void GL_DrawParticles( const struct ref_viewpass_s *rvp, qboolean trans_pass, float frametime )
{

}
colorVec R_LightVec( const float *start, const float *end, float *lightspot, float *lightvec )
{
    return (colorVec){0};
}
struct mstudiotex_s *R_StudioGetTexture( struct cl_entity_s *e )
{
    return NULL;
}

// passed through R_RenderFrame (0 - use engine renderer, 1 - use custom client renderer)
void R_RenderFrame( const struct ref_viewpass_s *rvp )
{

}
// setup map bounds for ortho-projection when we in dev_overview mode
void GL_OrthoBounds( const float *mins, const float *maxs )
{

}
// grab r_speeds message
qboolean R_SpeedsMessage( char *out, size_t size )
{
    return false;
}
// get visdata for current frame from custom renderer
byte* Mod_GetCurrentVis( void )
{
    return NULL;
}
// tell the renderer what new map is started
void R_NewMap( void )
{

}
// clear the render entities before each frame
void R_ClearScene( void )
{

}
// GL_GetProcAddress for client renderer
void* R_GetProcAddress( const char *name )
{
    return NULL;
}

// TriAPI Interface
// NOTE: implementation isn't required to be compatible
void TriRenderMode( int mode )
{

}
void TriBegin( int primitiveCode )
{

}
void TriEnd( void )
{

}
void TriColor4f( float r, float g, float b, float a )
{

}

void TriTexCoord2f( float u, float v )
{

}
void TriVertex3fv( const float *worldPnt )
{

}
void TriVertex3f( float x, float y, float z )
{

}
void TriFog( float flFogColor[3], float flStart, float flEnd, int bOn )
{

}
void R_ScreenToWorld( const float *screen, float *world  )
{

}
void TriGetMatrix( const int pname, float *matrix )
{

}
void TriFogParams( float flDensity, int iFogSkybox )
{

}
void TriCullFace( TRICULLSTYLE mode )
{

}

// vgui drawing implementation
void VGUI_DrawInit( void )
{

}
void VGUI_DrawShutdown( void )
{

}
void VGUI_SetupDrawingText( int *pColor )
{

}
void VGUI_SetupDrawingRect( int *pColor )
{

}
void VGUI_SetupDrawingImage( int *pColor )
{

}
void VGUI_BindTexture( int id )
{

}
void VGUI_EnableTexture( qboolean enable )
{

}
void VGUI_CreateTexture( int id, int width, int height )
{

}
void VGUI_UploadTexture( int id, const char *buffer, int width, int height )
{

}
void VGUI_UploadTextureBlock( int id, int drawX, int drawY, const byte *rgba, int blockWidth, int blockHeight )
{

}
void VGUI_DrawQuad( const vpoint_t *ul, const vpoint_t *lr )
{

}
void VGUI_GetTextureSizes( int *width, int *height )
{

}
int	VGUI_GenerateTexture( void )
{
    return 0;
}

ref_interface_t gReffuncs =
{
	.R_Init = R_VkInit,
	.R_Shutdown = R_VkShutdown,
    .R_GetConfigName = R_VkGetConfigName,
	.R_SetDisplayTransform =  R_VkSetDisplayTransform,
	.RefGetParm = VK_RefGetParm,

	.GL_SetupAttributes = GL_SetupAttributes,
	.GL_InitExtensions = GL_InitExtensions,
	.GL_ClearExtensions = GL_ClearExtensions,

	.R_BeginFrame = VK_BeginFrame,
	.R_RenderScene =  R_RenderScene,
	.R_EndFrame = VK_EndFrame,
	.R_PushScene = R_PushScene,
	.R_PopScene  = R_PopScene,
	.GL_BackendStartFrame = GL_BackendStartFrame,
	.GL_BackendEndFrame = GL_BackendEndFrame,

	.R_ClearScreen = VK_ClearScreen,
	.R_AllowFog =  R_AllowFog,
	.GL_SetRenderMode = R_SetRenderMode,

	.R_AddEntity = R_AddEntity,
	.CL_AddCustomBeam = CL_AddCustomBeam,
	.R_ProcessEntData = R_ProcessEntData,

	.R_ShowTextures = R_ShowTextures,

	.R_GetTextureOriginalBuffer = R_GetTextureOriginalBuffer,
	.GL_LoadTextureFromBuffer = VK_LoadTextureFromBuffer,
	.GL_ProcessTexture = GL_ProcessTexture,
	.R_SetupSky =  R_SetupSky,

	.R_Set2DMode = R_Set2DMode,
	.R_DrawStretchRaw =  R_DrawStretchRaw,
	.R_DrawStretchPic = R_DrawStretchPic,
	.R_DrawTileClear =  R_DrawTileClear,
	.FillRGBA = CL_FillRGBA,
	.FillRGBABlend = CL_FillRGBABlend,
	.WorldToScreen = R_WorldToScreen,

	.VID_ScreenShot = VID_ScreenShot,
	.VID_CubemapShot =  VID_CubemapShot,

	.R_LightPoint = R_LightPoint,

	.R_DecalShoot =	R_DecalShoot,
	.R_DecalRemoveAll = R_DecalRemoveAll,
	.R_CreateDecalList = R_CreateDecalList,
	.R_ClearAllDecals = R_ClearAllDecals,

	.R_StudioEstimateFrame = R_StudioEstimateFrame,
	.R_StudioLerpMovement = R_StudioLerpMovement,
	.CL_InitStudioAPI = CL_InitStudioAPI,

	.R_InitSkyClouds = R_InitSkyClouds,
	.GL_SubdivideSurface = GL_SubdivideSurface,
	.CL_RunLightStyles = CL_RunLightStyles,

	.R_GetSpriteParms = R_GetSpriteParms,
	.R_GetSpriteTexture = R_GetSpriteTexture,

	.Mod_LoadMapSprite = Mod_LoadMapSprite,
	.Mod_ProcessRenderData = Mod_ProcessRenderData,
	.Mod_StudioLoadTextures = Mod_StudioLoadTextures,

	.CL_DrawParticles = CL_DrawParticles,
	.CL_DrawTracers = CL_DrawTracers,
	.CL_DrawBeams = CL_DrawBeams,
	.R_BeamCull = R_BeamCull,

	.GetDetailScaleForTexture = R_GetDetailScaleForTexture,
	.GetExtraParmsForTexture =  R_GetExtraParmsForTexture,
	.GetFrameTime = R_GetFrameTime,

	.R_SetCurrentEntity = R_SetCurrentEntity,
	.R_SetCurrentModel = R_SetCurrentModel,

	.GL_FindTexture = VK_FindTexture,
	.GL_TextureName = VK_TextureName,
	.GL_TextureData = VK_TextureData,
	.GL_LoadTexture = VK_LoadTexture,
	.GL_CreateTexture =  VK_CreateTexture,
	.GL_LoadTextureArray = VK_LoadTextureArray,
	.GL_CreateTextureArray = VK_CreateTextureArray,
	.GL_FreeTexture = VK_FreeTexture,

	.DrawSingleDecal =	DrawSingleDecal,
	.R_DecalSetupVerts = R_DecalSetupVerts,
	.R_EntityRemoveDecals = R_EntityRemoveDecals,

    .AVI_UploadRawFrame = AVI_UploadRawFrame,
    //R_DrawStretchRaw,

	.GL_Bind =  GL_Bind,
	.GL_SelectTexture = GL_SelectTexture,
	.GL_LoadTextureMatrix = GL_LoadTextureMatrix,
	.GL_TexMatrixIdentity = GL_TexMatrixIdentity,
	.GL_CleanUpTextureUnits = GL_CleanUpTextureUnits,
	.GL_TexGen = GL_TexGen,
	.GL_TextureTarget =  GL_TextureTarget,
	.GL_TexCoordArrayMode = GL_TexCoordArrayMode,
	.GL_UpdateTexSize =  GL_UpdateTexSize,
	NULL,
	NULL,

	// .GL_DrawParticles =  GL_DrawParticles,
	// .LightVec = R_LightVec,

	// R_RenderFrame,
    // GL_OrthoBounds,
	// R_SpeedsMessage,
	// Mod_GetCurrentVis,
	// R_NewMap,
	// R_ClearScene,
	// R_GetProcAddress,

	.TriRenderMode = TriRenderMode,
	.Begin = TriBegin,
	.End = TriEnd,
	.Color4f = TriColor4f,
	.Color4ub = TriColor4ub,
	.TexCoord2f = TriTexCoord2f,
	.Vertex3fv = TriVertex3fv,
	.Vertex3f = TriVertex3f,
	.Fog =  TriFog,
	.ScreenToWorld = R_ScreenToWorld,
	.GetMatrix = TriGetMatrix,
	.FogParams = TriFogParams,
	.CullFace = TriCullFace,

	// .VGUI_DrawInit = VGUI_DrawInit,
	// .VGUI_DrawShutdown = VGUI_DrawShutdown,
	// .VGUI_SetupDrawingTex = VGUI_SetupDrawingText,
	// .VGUI_SetupDrawingRec = VGUI_SetupDrawingRect,
	// .VGUI_SetupDrawingIma = VGUI_SetupDrawingImage,
	// .VGUI_BindTexture = VGUI_BindTexture;
	// .VGUI_EnableTexture = VGUI_EnableTexture;
	// .VGUI_CreateTexture = VGUI_CreateTexture;
	// .VGUI_UploadTexture= VGUI_UploadTexture;
	// .VGUI_UploadTextureBl= VGUI_UploadTextureBlock;
	// .VGUI_DrawQuad= VGUI_DrawQuad;
	// .VGUI_GetTextureSizes= VGUI_GetTextureSizes;
	// .VGUI_GenerateTexture= VGUI_GenerateTexture;
};

int EXPORT GetRefAPI(int version, ref_interface_t *funcs, ref_api_t* engfuncs, ref_globals_t* globals)
{
    if (version != REF_API_VERSION)
        return 0;
    
    memcpy(funcs, &gReffuncs, sizeof(ref_interface_t));
    memcpy(&gEngine, engfuncs, sizeof(ref_api_t));

    gGlobals = globals;
    
    return REF_API_VERSION;
}


void EXPORT GetRefHumanReadableName(char* out, size_t size)
{
    Q_strncpy(out, "Vulkan", size);
}
