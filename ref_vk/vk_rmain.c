#include "vk_core.h"
#include "vk_common.h"
#include "vk_textures.h"
#include "vk_renderstate.h"
#include "vk_2d.h"
#include "vk_scene.h"
#include "vk_framectl.h"
#include "vk_lightmap.h"
#include "vk_sprite.h"

#include "xash3d_types.h"
#include "com_strings.h"

#include <memory.h>

ref_api_t gEngine = {0};
ref_globals_t *gpGlobals = NULL;

static const char *R_GetConfigName( void )
{
	return "vk";
}

static qboolean R_SetDisplayTransform( ref_screen_rotation_t rotate, int x, int y, float scale_x, float scale_y )
{
	gEngine.Con_Printf("VK FIXME: %s(%d, %d, %d, %f, %f)\n", __FUNCTION__, rotate, x, y, scale_x, scale_y);

	return true;
}

// only called for GL contexts
static void GL_SetupAttributes( int safegl )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void GL_ClearExtensions( void )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}

static void GL_BackendStartFrame( void )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void GL_BackendEndFrame( void )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}

static void CL_AddCustomBeam( cl_entity_t *pEnvBeam )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}

// debug
static void R_ShowTextures( void )
{
	//gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}

// texture management
static const byte *R_GetTextureOriginalBuffer( unsigned int idx )
{
	gEngine.Con_Printf("VK FIXME: %s\n", __FUNCTION__);
	return NULL;
}

static void GL_ProcessTexture( int texnum, float gamma, int topColor, int bottomColor )
{
	gEngine.Con_Printf("VK FIXME: %s\n", __FUNCTION__);
}

static void R_SetupSky( const char *skyname )
{
	gEngine.Con_Printf("VK FIXME: %s\n", __FUNCTION__);
}

// screenshot, cubemapshot
static qboolean VID_ScreenShot( const char *filename, int shot_type )
{
	gEngine.Con_Printf("VK FIXME: %s\n", __FUNCTION__);
	return false;
}

static qboolean VID_CubemapShot( const char *base, uint size, const float *vieworg, qboolean skyshot )
{
	gEngine.Con_Printf("VK FIXME: %s\n", __FUNCTION__);
	return false;
}

// light
static colorVec R_LightPoint( const float *p )
{
	gEngine.Con_Printf("VK FIXME: %s\n", __FUNCTION__);
	return (colorVec){0};
}

// decals
// Shoots a decal onto the surface of the BSP.  position is the center of the decal in world coords
static void R_DecalShoot( int textureIndex, int entityIndex, int modelIndex, vec3_t pos, int flags, float scale )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void R_DecalRemoveAll( int texture )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static int R_CreateDecalList( struct decallist_s *pList )
{
	gEngine.Con_Printf("VK FIXME: %s\n", __FUNCTION__);
	return 0;
}
static void R_ClearAllDecals( void )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}

// studio interface
static float R_StudioEstimateFrame( cl_entity_t *e, mstudioseqdesc_t *pseqdesc )
{
	gEngine.Con_Printf("VK FIXME: %s\n", __FUNCTION__);
	return 1.f;
}

static void R_StudioLerpMovement( cl_entity_t *e, double time, vec3_t origin, vec3_t angles )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void CL_InitStudioAPI( void )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}

// bmodel
static void R_InitSkyClouds( struct mip_s *mt, struct texture_s *tx, qboolean custom_palette )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}

extern void GL_SubdivideSurface( msurface_t *fa );


static void Mod_LoadAliasModel( model_t *mod, const void *buffer, qboolean *loaded )
{
	gEngine.Con_Printf(S_ERROR "VK FIXME %s(%p, %s), %p, %d\n",
		__FUNCTION__, mod, mod->name, buffer, *loaded);
}

static void Mod_UnloadTextures( model_t *mod )
{
	gEngine.Con_Printf(S_ERROR "VK FIXME %s(%p, %s)\n",
		__FUNCTION__, mod, mod->name);
}

static qboolean Mod_ProcessRenderData( model_t *mod, qboolean create, const byte *buffer )
{
	qboolean loaded = true;

	// TODO does this ever happen?
	if (!create && mod->type == mod_brush)
		gEngine.Con_Printf( S_WARN "VK FIXME Trying to unload brush model %s\n", mod->name);

	if( create )
	{
		switch( mod->type )
		{
			case mod_studio:
				// Mod_LoadStudioModel( mod, buf, loaded );
				break;
			case mod_sprite:
				Mod_LoadSpriteModel( mod, buffer, &loaded, mod->numtexinfo );
				break;
			case mod_alias:
				Mod_LoadAliasModel( mod, buffer, &loaded );
				break;
			case mod_brush:
				// FIXME this happens before we get R_NewMap, which frees all current buffers
				// loaded = VK_LoadBrushModel( mod, buffer );
				break;
			default: gEngine.Host_Error( "Mod_LoadModel: unsupported type %d\n", mod->type );
		}
	}

	if( loaded && gEngine.drawFuncs->Mod_ProcessUserData )
		gEngine.drawFuncs->Mod_ProcessUserData( mod, create, buffer );

	if( !create )
		Mod_UnloadTextures( mod );

	return loaded;
}
static void Mod_StudioLoadTextures( model_t *mod, void *data )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}

// efx implementation
static void CL_DrawParticles( double frametime, particle_t *particles, float partsize )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void CL_DrawTracers( double frametime, particle_t *tracers )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void CL_DrawBeams( int fTrans , BEAM *beams )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static qboolean R_BeamCull( const vec3_t start, const vec3_t end, qboolean pvsOnly )
{
	gEngine.Con_Printf("VK FIXME: %s\n", __FUNCTION__);
	return false;
}

// Xash3D Render Interface
// Get renderer info (doesn't changes engine state at all)

static const char *getParmName(int parm)
{
	switch(parm){
	case PARM_TEX_WIDTH: return "PARM_TEX_WIDTH";
	case PARM_TEX_HEIGHT: return "PARM_TEX_HEIGHT";
	case PARM_TEX_SRC_WIDTH: return "PARM_TEX_SRC_WIDTH";
	case PARM_TEX_SRC_HEIGHT: return "PARM_TEX_SRC_HEIGHT";
	case PARM_TEX_SKYBOX: return "PARM_TEX_SKYBOX";
	case PARM_TEX_SKYTEXNUM: return "PARM_TEX_SKYTEXNUM";
	case PARM_TEX_LIGHTMAP: return "PARM_TEX_LIGHTMAP";
	case PARM_TEX_TARGET: return "PARM_TEX_TARGET";
	case PARM_TEX_TEXNUM: return "PARM_TEX_TEXNUM";
	case PARM_TEX_FLAGS: return "PARM_TEX_FLAGS";
	case PARM_TEX_DEPTH: return "PARM_TEX_DEPTH";
	case PARM_TEX_GLFORMAT: return "PARM_TEX_GLFORMAT";
	case PARM_TEX_ENCODE: return "PARM_TEX_ENCODE";
	case PARM_TEX_MIPCOUNT: return "PARM_TEX_MIPCOUNT";
	case PARM_BSP2_SUPPORTED: return "PARM_BSP2_SUPPORTED";
	case PARM_SKY_SPHERE: return "PARM_SKY_SPHERE";
	case PARAM_GAMEPAUSED: return "PARAM_GAMEPAUSED";
	case PARM_MAP_HAS_DELUXE: return "PARM_MAP_HAS_DELUXE";
	case PARM_MAX_ENTITIES: return "PARM_MAX_ENTITIES";
	case PARM_WIDESCREEN: return "PARM_WIDESCREEN";
	case PARM_FULLSCREEN: return "PARM_FULLSCREEN";
	case PARM_SCREEN_WIDTH: return "PARM_SCREEN_WIDTH";
	case PARM_SCREEN_HEIGHT: return "PARM_SCREEN_HEIGHT";
	case PARM_CLIENT_INGAME: return "PARM_CLIENT_INGAME";
	case PARM_FEATURES: return "PARM_FEATURES";
	case PARM_ACTIVE_TMU: return "PARM_ACTIVE_TMU";
	case PARM_LIGHTSTYLEVALUE: return "PARM_LIGHTSTYLEVALUE";
	case PARM_MAX_IMAGE_UNITS: return "PARM_MAX_IMAGE_UNITS";
	case PARM_CLIENT_ACTIVE: return "PARM_CLIENT_ACTIVE";
	case PARM_REBUILD_GAMMA: return "PARM_REBUILD_GAMMA";
	case PARM_DEDICATED_SERVER: return "PARM_DEDICATED_SERVER";
	case PARM_SURF_SAMPLESIZE: return "PARM_SURF_SAMPLESIZE";
	case PARM_GL_CONTEXT_TYPE: return "PARM_GL_CONTEXT_TYPE";
	case PARM_GLES_WRAPPER: return "PARM_GLES_WRAPPER";
	case PARM_STENCIL_ACTIVE: return "PARM_STENCIL_ACTIVE";
	case PARM_WATER_ALPHA: return "PARM_WATER_ALPHA";
	case PARM_TEX_MEMORY: return "PARM_TEX_MEMORY";
	case PARM_DELUXEDATA: return "PARM_DELUXEDATA";
	case PARM_SHADOWDATA: return "PARM_SHADOWDATA";
	default: return "UNKNOWN";
	}
}

static int VK_RefGetParm( int parm, int arg )
{
	vk_texture_t *tex = NULL;

	switch(parm){
	case PARM_TEX_WIDTH:
	case PARM_TEX_SRC_WIDTH: // TODO why is this separate?
		tex = findTexture(arg);
		return tex->width;
	case PARM_TEX_HEIGHT:
	case PARM_TEX_SRC_HEIGHT:
		tex = findTexture(arg);
		return tex->height;
	case PARM_TEX_FLAGS:
		tex = findTexture(arg);
		return tex->flags;
	}

	gEngine.Con_Printf("VK FIXME: %s(%s(%d), %d)\n", __FUNCTION__, getParmName(parm), parm, arg);

	return 0;
}
static void		GetDetailScaleForTexture( int texture, float *xScale, float *yScale )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void		GetExtraParmsForTexture( int texture, byte *red, byte *green, byte *blue, byte *alpha )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static float		GetFrameTime( void )
{
	gEngine.Con_Printf("VK FIXME: %s\n", __FUNCTION__);
	return 1.f;
}

// Set renderer info (tell engine about changes)
static void		R_SetCurrentEntity( struct cl_entity_s *ent )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void		R_SetCurrentModel( struct model_s *mod )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}


// Decals manipulating (draw & remove)
static void		DrawSingleDecal( struct decal_s *pDecal, struct msurface_s *fa )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static float		*R_DecalSetupVerts( struct decal_s *pDecal, struct msurface_s *surf, int texture, int *outCount )
{
	gEngine.Con_Printf("VK FIXME: %s\n", __FUNCTION__);
	return NULL;
}

static void		R_EntityRemoveDecals( struct model_s *mod )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}

// AVI
static void		AVI_UploadRawFrame( int texture, int cols, int rows, int width, int height, const byte *data )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}

// glState related calls (must use this instead of normal gl-calls to prevent de-synchornize local states between engine and the client)
static void		GL_Bind( int tmu, unsigned int texnum )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void		GL_SelectTexture( int tmu )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void		GL_LoadTextureMatrix( const float *glmatrix )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void		GL_TexMatrixIdentity( void )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void		GL_CleanUpTextureUnits( int last )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void		GL_TexGen( unsigned int coord, unsigned int mode )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void		GL_TextureTarget( unsigned int target )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void		GL_TexCoordArrayMode( unsigned int texmode )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void		GL_UpdateTexSize( int texnum, int width, int height, int depth )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}

// Misc renderer functions
static void		GL_DrawParticles( const struct ref_viewpass_s *rvp, qboolean trans_pass, float frametime )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static colorVec		R_LightVec( const float *start, const float *end, float *lightspot, float *lightvec )
{
	gEngine.Con_Printf("VK FIXME: %s\n", __FUNCTION__);
	return (colorVec){0};
}

static struct mstudiotex_s *R_StudioGetTexture( struct cl_entity_s *e )
{
	gEngine.Con_Printf("VK FIXME: %s\n", __FUNCTION__);
	return NULL;
}

// setup map bounds for ortho-projection when we in dev_overview mode
static void		GL_OrthoBounds( const float *mins, const float *maxs )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
// grab r_speeds message
static qboolean	R_SpeedsMessage( char *out, size_t size )
{
	//gEngine.Con_Printf("VK FIXME: %s\n", __FUNCTION__);
	return false;
}
// get visdata for current frame from custom renderer
static byte*		Mod_GetCurrentVis( void )
{
	gEngine.Con_Printf("VK FIXME: %s\n", __FUNCTION__);
	return NULL;
}

// GL_GetProcAddress for client renderer
static void*		R_GetProcAddress( const char *name )
{
	gEngine.Con_Printf("VK FIXME: %s\n", __FUNCTION__);
	return NULL;
}

// TriAPI Interface
// NOTE: implementation isn't required to be compatible
static void	TriRenderMode( int mode )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void	TriBegin( int primitiveCode )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void	TriEnd( void )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void	TriColor4f( float r, float g, float b, float a )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void	TriTexCoord2f( float u, float v )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void	TriVertex3fv( const float *worldPnt )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void	TriVertex3f( float x, float y, float z )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static int	TriWorldToScreen( const float *world, float *screen )
{
	gEngine.Con_Printf("VK FIXME: %s\n", __FUNCTION__);
	return 0;
}
static void	TriFog( float flFogColor[3], float flStart, float flEnd, int bOn )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void	R_ScreenToWorld( const float *screen, float *world  )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void	TriGetMatrix( const int pname, float *matrix )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void	TriFogParams( float flDensity, int iFogSkybox )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void	TriCullFace( TRICULLSTYLE mode )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}

// vgui drawing implementation
static void	VGUI_DrawInit( void )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void	VGUI_DrawShutdown( void )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void	VGUI_SetupDrawingText( int *pColor )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void	VGUI_SetupDrawingRect( int *pColor )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void	VGUI_SetupDrawingImage( int *pColor )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void	VGUI_BindTexture( int id )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void	VGUI_EnableTexture( qboolean enable )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void	VGUI_CreateTexture( int id, int width, int height )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void	VGUI_UploadTexture( int id, const char *buffer, int width, int height )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void	VGUI_UploadTextureBlock( int id, int drawX, int drawY, const byte *rgba, int blockWidth, int blockHeight )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void	VGUI_DrawQuad( const vpoint_t *ul, const vpoint_t *lr )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static void	VGUI_GetTextureSizes( int *width, int *height )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
static int		VGUI_GenerateTexture( void )
{
	gEngine.Con_Printf("VK FIXME: %s\n", __FUNCTION__);
	return 0;
}

ref_interface_t gReffuncs =
{
	.R_Init = R_VkInit,
	.R_Shutdown = R_VkShutdown,
	R_GetConfigName,
	R_SetDisplayTransform,

	GL_SetupAttributes,
	.GL_InitExtensions = NULL, // Unused in Vulkan renderer
	GL_ClearExtensions,

	R_BeginFrame,
	R_RenderScene, // Not called ever?
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
	VK_LoadTextureFromBuffer,
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
	VK_RunLightStyles,

	R_GetSpriteParms,
	R_GetSpriteTexture,

	Mod_LoadMapSprite,
	Mod_ProcessRenderData,
	Mod_StudioLoadTextures,

	CL_DrawParticles,
	CL_DrawTracers,
	CL_DrawBeams,
	R_BeamCull,

	VK_RefGetParm,
	GetDetailScaleForTexture,
	GetExtraParmsForTexture,
	GetFrameTime,

	R_SetCurrentEntity,
	R_SetCurrentModel,

	VK_FindTexture,
	VK_TextureName,
	VK_TextureData,
	VK_LoadTexture,
	VK_CreateTexture,
	VK_LoadTextureArray,
	VK_CreateTextureArray,
	VK_FreeTexture,

	DrawSingleDecal,
	R_DecalSetupVerts,
	R_EntityRemoveDecals,

	AVI_UploadRawFrame,

	GL_Bind,
	GL_SelectTexture,
	GL_LoadTextureMatrix,
	GL_TexMatrixIdentity,
	GL_CleanUpTextureUnits,
	GL_TexGen,
	GL_TextureTarget,
	GL_TexCoordArrayMode,
	GL_UpdateTexSize,
	NULL, // Reserved0
	NULL, // Reserved1

	GL_DrawParticles,
	R_LightVec,
	R_StudioGetTexture,

	GL_RenderFrame,
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
	TriWorldToScreen,
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

int EXPORT GetRefAPI( int version, ref_interface_t *funcs, ref_api_t *engfuncs, ref_globals_t *globals )
{
	if( version != REF_API_VERSION )
		return 0;

	// fill in our callbacks
	memcpy( funcs, &gReffuncs, sizeof( ref_interface_t ));
	memcpy( &gEngine, engfuncs, sizeof( ref_api_t ));
	gpGlobals = globals;

	return REF_API_VERSION;
}

void EXPORT GetRefHumanReadableName( char *out, size_t size )
{
	Q_strncpy( out, "Vulkan", size );
}
