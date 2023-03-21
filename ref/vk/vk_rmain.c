#include "vk_core.h"
#include "vk_cvar.h"
#include "vk_common.h"
#include "vk_textures.h"
#include "vk_renderstate.h"
#include "vk_overlay.h"
#include "vk_scene.h"
#include "vk_framectl.h"
#include "vk_lightmap.h"
#include "vk_sprite.h"
#include "vk_studio.h"
#include "vk_beams.h"
#include "vk_brush.h"
#include "vk_rpart.h"
#include "vk_triapi.h"
#include "r_slows.h"

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
	PRINT_NOT_IMPLEMENTED_ARGS("(%d, %d, %d, %f, %f)", rotate, x, y, scale_x, scale_y);

	return true;
}

// only called for GL contexts
static void GL_SetupAttributes( int safegl )
{
	PRINT_NOT_IMPLEMENTED();
}
static void GL_ClearExtensions( void )
{
	PRINT_NOT_IMPLEMENTED();
}
static void GL_BackendStartFrame_UNUSED( void )
{
	/* Unused in Vulkan renderer. GL renderer only uses this to clear the r_speeds_msg string */
}
static void GL_BackendEndFrame_UNUSED( void )
{
	/* Unused in Vulkan renderer. GL renderer only uses this to populate r_speeds_msg string. In Vulkan this is done naturally in R_EndFrame */
}

// debug
static void R_ShowTextures( void )
{
	PRINT_NOT_IMPLEMENTED();
	//PRINT_NOT_IMPLEMENTED();
}

// texture management
static const byte *R_GetTextureOriginalBuffer( unsigned int idx )
{
	PRINT_NOT_IMPLEMENTED();
	return NULL;
}

static void GL_ProcessTexture( int texnum, float gamma, int topColor, int bottomColor )
{
	PRINT_NOT_IMPLEMENTED();
}

static qboolean VID_CubemapShot( const char *base, uint size, const float *vieworg, qboolean skyshot )
{
	PRINT_NOT_IMPLEMENTED();
	return false;
}

// light
static colorVec R_LightPoint( const float *p )
{
	PRINT_NOT_IMPLEMENTED();
	return (colorVec){0};
}

// decals
// Shoots a decal onto the surface of the BSP.  position is the center of the decal in world coords
static void R_DecalShoot( int textureIndex, int entityIndex, int modelIndex, vec3_t pos, int flags, float scale )
{
	PRINT_NOT_IMPLEMENTED();
}
static void R_DecalRemoveAll( int texture )
{
	PRINT_NOT_IMPLEMENTED();
}
static int R_CreateDecalList( struct decallist_s *pList )
{
	PRINT_NOT_IMPLEMENTED();
	return 0;
}
static void R_ClearAllDecals( void )
{
	PRINT_NOT_IMPLEMENTED();
}

// studio interface
static float R_StudioEstimateFrame( cl_entity_t *e, mstudioseqdesc_t *pseqdesc )
{
	PRINT_NOT_IMPLEMENTED();
	return 1.f;
}

static void R_StudioLerpMovement( cl_entity_t *e, double time, vec3_t origin, vec3_t angles )
{
	PRINT_NOT_IMPLEMENTED();
}

// bmodel
static void R_InitSkyClouds( struct mip_s *mt, struct texture_s *tx, qboolean custom_palette )
{
	PRINT_NOT_IMPLEMENTED();
}

extern void GL_SubdivideSurface( msurface_t *fa );


static void Mod_LoadAliasModel( model_t *mod, const void *buffer, qboolean *loaded )
{
	PRINT_NOT_IMPLEMENTED_ARGS("(%p, %s), %p, %d", mod, mod->name, buffer, *loaded);
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
				Mod_LoadStudioModel( mod, buffer, &loaded );
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

	if( !create ) {
		switch( mod->type )
		{
			case mod_brush:
				VK_BrushModelDestroy( mod );
				break;
			default:
				PRINT_NOT_IMPLEMENTED_ARGS("destroy (%p, %d, %s)", mod, mod->type, mod->name);
		}
	}

	return loaded;
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
	case PARM_MODERNFLASHLIGHT: return "PARM_MODERNFLASHLIGHT";
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
	case PARM_MODERNFLASHLIGHT:
		if (CVAR_TO_BOOL( vk_rtx )) {
			return true;
		}
		return false;
	}

	PRINT_NOT_IMPLEMENTED_ARGS("(%s(%d), %d)", getParmName(parm), parm, arg);

	return 0;
}
static void		GetDetailScaleForTexture( int texture, float *xScale, float *yScale )
{
	PRINT_NOT_IMPLEMENTED();
}
static void		GetExtraParmsForTexture( int texture, byte *red, byte *green, byte *blue, byte *alpha )
{
	PRINT_NOT_IMPLEMENTED();
}
static float		GetFrameTime( void )
{
	PRINT_NOT_IMPLEMENTED();
	return 1.f;
}

// Set renderer info (tell engine about changes)
static void		R_SetCurrentEntity( struct cl_entity_s *ent )
{
	PRINT_NOT_IMPLEMENTED();
}
static void		R_SetCurrentModel( struct model_s *mod )
{
	PRINT_NOT_IMPLEMENTED();
}


// Decals manipulating (draw & remove)
static void		DrawSingleDecal( struct decal_s *pDecal, struct msurface_s *fa )
{
	PRINT_NOT_IMPLEMENTED();
}
static float		*R_DecalSetupVerts( struct decal_s *pDecal, struct msurface_s *surf, int texture, int *outCount )
{
	PRINT_NOT_IMPLEMENTED();
	return NULL;
}

static void		R_EntityRemoveDecals( struct model_s *mod )
{
	PRINT_NOT_IMPLEMENTED();
}

// AVI
static void		AVI_UploadRawFrame( int texture, int cols, int rows, int width, int height, const byte *data )
{
	PRINT_NOT_IMPLEMENTED();
}

// glState related calls (must use this instead of normal gl-calls to prevent de-synchornize local states between engine and the client)
static void GL_Bind( int tmu, unsigned int texnum )
{
	if (tmu != 0) {
		PRINT_NOT_IMPLEMENTED_ARGS("non-zero tmu=%d", tmu);
	}

	TriSetTexture(texnum);
}
static void		GL_SelectTexture( int tmu )
{
	PRINT_NOT_IMPLEMENTED();
}
static void		GL_LoadTextureMatrix( const float *glmatrix )
{
	PRINT_NOT_IMPLEMENTED();
}
static void		GL_TexMatrixIdentity( void )
{
	PRINT_NOT_IMPLEMENTED();
}
static void		GL_CleanUpTextureUnits( int last )
{
	PRINT_NOT_IMPLEMENTED();
}
static void		GL_TexGen( unsigned int coord, unsigned int mode )
{
	PRINT_NOT_IMPLEMENTED();
}
static void		GL_TextureTarget( unsigned int target )
{
	PRINT_NOT_IMPLEMENTED();
}
static void		GL_TexCoordArrayMode( unsigned int texmode )
{
	PRINT_NOT_IMPLEMENTED();
}
static void		GL_UpdateTexSize( int texnum, int width, int height, int depth )
{
	PRINT_NOT_IMPLEMENTED();
}

// Misc renderer functions
static void		GL_DrawParticles( const struct ref_viewpass_s *rvp, qboolean trans_pass, float frametime )
{
	PRINT_NOT_IMPLEMENTED();
}

colorVec		R_LightVec( const float *start, const float *end, float *lightspot, float *lightvec );

static struct mstudiotex_s *R_StudioGetTexture( struct cl_entity_s *e )
{
	PRINT_NOT_IMPLEMENTED();
	return NULL;
}

// setup map bounds for ortho-projection when we in dev_overview mode
static void		GL_OrthoBounds( const float *mins, const float *maxs )
{
	PRINT_NOT_IMPLEMENTED();
}

// get visdata for current frame from custom renderer
static byte*		Mod_GetCurrentVis( void )
{
	PRINT_NOT_IMPLEMENTED();
	return NULL;
}

// GL_GetProcAddress for client renderer
static void*		R_GetProcAddress( const char *name )
{
	PRINT_NOT_IMPLEMENTED();
	return NULL;
}

// TriAPI Interface
static void	TriFog( float flFogColor[3], float flStart, float flEnd, int bOn )
{
	PRINT_NOT_IMPLEMENTED();
}
static void	R_ScreenToWorld( const float *screen, float *world  )
{
	PRINT_NOT_IMPLEMENTED();
}
static void	TriGetMatrix( const int pname, float *matrix )
{
	PRINT_NOT_IMPLEMENTED();
}
static void	TriFogParams( float flDensity, int iFogSkybox )
{
	PRINT_NOT_IMPLEMENTED();
}
static void	TriCullFace( TRICULLSTYLE mode )
{
	PRINT_NOT_IMPLEMENTED();
}

// vgui drawing implementation
static void	VGUI_DrawInit( void )
{
	PRINT_NOT_IMPLEMENTED();
}
static void	VGUI_DrawShutdown( void )
{
	PRINT_NOT_IMPLEMENTED();
}
static void	VGUI_SetupDrawingText( int *pColor )
{
	PRINT_NOT_IMPLEMENTED();
}
static void	VGUI_SetupDrawingRect( int *pColor )
{
	PRINT_NOT_IMPLEMENTED();
}
static void	VGUI_SetupDrawingImage( int *pColor )
{
	PRINT_NOT_IMPLEMENTED();
}
static void	VGUI_BindTexture( int id )
{
	PRINT_NOT_IMPLEMENTED();
}
static void	VGUI_EnableTexture( qboolean enable )
{
	PRINT_NOT_IMPLEMENTED();
}
static void	VGUI_CreateTexture( int id, int width, int height )
{
	PRINT_NOT_IMPLEMENTED();
}
static void	VGUI_UploadTexture( int id, const char *buffer, int width, int height )
{
	PRINT_NOT_IMPLEMENTED();
}
static void	VGUI_UploadTextureBlock( int id, int drawX, int drawY, const byte *rgba, int blockWidth, int blockHeight )
{
	PRINT_NOT_IMPLEMENTED();
}
static void	VGUI_DrawQuad( const vpoint_t *ul, const vpoint_t *lr )
{
	PRINT_NOT_IMPLEMENTED();
}
static void	VGUI_GetTextureSizes( int *width, int *height )
{
	PRINT_NOT_IMPLEMENTED();
}
static int		VGUI_GenerateTexture( void )
{
	PRINT_NOT_IMPLEMENTED();
	return 0;
}

static const ref_device_t *pfnGetRenderDevice( unsigned int idx )
{
	if( idx >= vk_core.num_devices )
		return NULL;

	return &vk_core.devices[idx];
}

static const ref_interface_t gReffuncs =
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
	.GL_BackendStartFrame = GL_BackendStartFrame_UNUSED,
	.GL_BackendEndFrame = GL_BackendEndFrame_UNUSED,

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
	XVK_SetupSky,

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

	VK_RenderFrame,
	GL_OrthoBounds,
	.R_SpeedsMessage = R_SpeedsMessage,
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

	pfnGetRenderDevice,
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
