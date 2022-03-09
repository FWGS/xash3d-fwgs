#include "xash3d_types.h"

#include "cvardef.h"
#include "const.h"
#include "crtlib.h"
#include "ref_api.h"

#include <memory.h>
#include <stdio.h>

ref_api_t *gEngine ={0};
ref_globals_t* gGlobals = {0};

// construct, destruct
qboolean R_VkInit( void )
{
    fprintf(stderr,"VK FIXME: %s\n", __FUNCTION__);


    if (!gEngine->R_Init_Video(REF_VULKAN))  //request vulkan surface
    {
        
        return false;
    }
    
    return true;
}
// const char *(*R_GetInitError)( void );
void R_VkShutdown( void )
{
    fprintf(stderr,"VK FIXME: %s\n", __FUNCTION__);

}

const char *R_VkGetConfigName( void )
{
    fprintf(stderr,"VK FIXME: %s\n", __FUNCTION__);
    return "vk";
}

qboolean R_VkSetDisplayTransform( ref_screen_rotation_t rotate, int x, int y, float scale_x, float scale_y )
{
    fprintf(stderr,"VK FIXME: %s(%d, %d, %d, %d)\n", __FUNCTION__, rotate, x, y, scale_x, scale_y);
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

void R_BeginFrame( qboolean clearScene )
{

}
void R_RenderScene( void )
{

}
void R_EndFrame( void )
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

void R_ClearScreen( void )
{

}
void R_AllowFog( qboolean allow )
{

}
void GL_SetRenderMode( int renderMode )
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
int GL_LoadTextureFromBuffer( const char *name, rgbdata_t *pic, texFlags_t flags, qboolean update )
{
    return 0;
}
void GL_ProcessTexture( int texnum, float gamma, int topColor, int bottomColor )
{

}
void R_SetupSky( const char *skyname )
{

}

// 2D
void R_Set2DMode( qboolean enable )
{

}
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
int	GL_RefGetParm( int parm, int arg )
{
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

// Texture tools
int	GL_FindTexture( const char *name )
{
    return 0;
}

const char*	GL_TextureName( unsigned int texnum )
{
    return NULL;
}

const byte*	GL_TextureData( unsigned int texnum )
{
    return NULL;
}

int	GL_LoadTexture( const char *name, const byte *buf, size_t size, int flags )
{
    return 0;
}

int	GL_CreateTexture( const char *name, int width, int height, const void *buffer, texFlags_t flags )
{
    return 0;
}

int	GL_LoadTextureArray( const char **names, int flags )
{
    return 0;
}

int	GL_CreateTextureArray( const char *name, int width, int height, int depth, const void *buffer, texFlags_t flags )
{
    return 0;
}

void GL_FreeTexture( unsigned int texnum )
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
void TriColor4ub( unsigned char r, unsigned char g, unsigned char b, unsigned char a )
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
	R_VkInit,
	R_VkShutdown,
    R_VkGetConfigName,
	R_VkSetDisplayTransform,

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

    AVI_UploadRawFrame,
    //R_DrawStretchRaw,

	GL_Bind,
	GL_SelectTexture,
	GL_LoadTextureMatrix,
	GL_TexMatrixIdentity,
	GL_CleanUpTextureUnits,
	GL_TexGen,
	GL_TextureTarget,
	GL_TexCoordArrayMode,
	GL_UpdateTexSize,
	NULL,
	NULL,

	GL_DrawParticles,
	R_LightVec,
	R_StudioGetTexture,

	R_RenderFrame,
    GL_OrthoBounds,
	R_SpeedsMessage,
	Mod_GetCurrentVis,
	R_NewMap,
	R_ClearScene,
	R_GetProcAddress,

	TriRenderMode,
	TriBegin,
	TriEnd,
	TriColor4f,
	TriColor4ub,
	TriTexCoord2f,
	TriVertex3fv,
	TriVertex3f,
	TriFog,
	R_ScreenToWorld,
	TriGetMatrix,
	TriFogParams,
	TriCullFace,

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
