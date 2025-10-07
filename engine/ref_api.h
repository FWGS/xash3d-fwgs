/*
ref_api.h - Xash3D render dll API
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
#pragma once
#ifndef REF_API
#define REF_API

#include <stdarg.h>
#include "com_image.h"
#include "vgui_api.h"
#include "render_api.h"
#include "triangleapi.h"
#include "const.h"
#include "cl_entity.h"
#include "com_model.h"
#include "studio.h"
#include "r_efx.h"
#include "com_image.h"
#include "filesystem.h"

// RefAPI changelog:
// 1. Initial release
// 2. FS functions are removed, instead we have full fs_api_t
// 3. SlerpBones, CalcBonePosition/Quaternion calls were moved to libpublic/mathlib
// 4. R_StudioEstimateFrame now has time argument
#define REF_API_VERSION 4


#define TF_SKY		(TF_SKYSIDE|TF_NOMIPMAP)
#define TF_FONT		(TF_NOMIPMAP|TF_CLAMP)
#define TF_IMAGE		(TF_NOMIPMAP|TF_CLAMP)
#define TF_DECAL		(TF_CLAMP)

#define FCONTEXT_CORE_PROFILE		BIT( 0 )
#define FCONTEXT_DEBUG_ARB		BIT( 1 )

#define FCVAR_READ_ONLY		(1<<17)	// cannot be set by user at all, and can't be requested by CvarGetPointer from game dlls

// screenshot types
#define VID_SCREENSHOT	0
#define VID_LEVELSHOT	1
#define VID_MINISHOT	2
#define VID_MAPSHOT		3	// special case for overview layer
#define VID_SNAPSHOT	4	// save screenshot into root dir and no gamma correction

// model flags (stored in model_t->flags)
#define MODEL_CONVEYOR		BIT( 0 )
#define MODEL_HAS_ORIGIN		BIT( 1 )
#define MODEL_LIQUID		BIT( 2 )	// model has only point hull
#define MODEL_TRANSPARENT		BIT( 3 )	// have transparent surfaces
#define MODEL_COLORED_LIGHTING	BIT( 4 )	// lightmaps stored as RGB
#define MODEL_WORLD			BIT( 29 )	// it's a worldmodel
#define MODEL_CLIENT		BIT( 30 )	// client sprite

// goes into world.flags
#define FWORLD_SKYSPHERE		BIT( 0 )
#define FWORLD_CUSTOM_SKYBOX		BIT( 1 )
#define FWORLD_WATERALPHA		BIT( 2 )
#define FWORLD_HAS_DELUXEMAP		BIT( 3 )

// special rendermode for screenfade modulate
// (probably will be expanded at some point)
#define kRenderScreenFadeModulate 0x1000

typedef enum
{
	DEMO_INACTIVE = 0,
	DEMO_XASH3D,
	DEMO_QUAKE1
} demo_mode;

typedef struct
{
	msurface_t	*surf;
	int		cull;
} sortedface_t;

typedef struct ref_globals_s
{
	qboolean developer;

	float time;    // cl.time
	float oldtime; // cl.oldtime
	double realtime; // host.realtime
	double frametime; // host.frametime

	// viewport width and height
	int      width;
	int      height;

	qboolean fullScreen;
	qboolean wideScreen;

	vec3_t vieworg;
	vec3_t viewangles;

	// todo: fill this without engine help
	// move to local

	// translucent sorted array
	sortedface_t	*draw_surfaces;	// used for sorting translucent surfaces
	int		max_surfaces;	// max surfaces per submodel (for all models)
	size_t		visbytes;		// cluster size

	int desktopBitsPixel;
} ref_globals_t;

enum
{
	GL_KEEP_UNIT = -1,
	XASH_TEXTURE0 = 0,
	XASH_TEXTURE1,
	XASH_TEXTURE2,
	XASH_TEXTURE3,		// g-cont. 4 units should be enough
	XASH_TEXTURE4,		// mittorn. bump+detail needs 5 for single-pass
	MAX_TEXTURE_UNITS = 32	// can't access to all over units without GLSL or cg
};

enum // r_speeds counters
{
	RS_ACTIVE_TENTS = 0,
};

// refdll must expose this default textures using this names
#define REF_DEFAULT_TEXTURE  "*default"
#define REF_GRAY_TEXTURE     "*gray"
#define REF_WHITE_TEXTURE    "*white"
#define REF_BLACK_TEXTURE    "*black"
#define REF_PARTICLE_TEXTURE "*particle"
#define REF_SOLIDSKY_TEXTURE "solid_sky"
#define REF_ALPHASKY_TEXTURE "alpha_sky"

typedef enum connstate_e
{
	ca_disconnected = 0,// not talking to a server
	ca_connecting,	// sending request packets to the server
	ca_connected,	// netchan_t established, waiting for svc_serverdata
	ca_validate,	// download resources, validating, auth on server
	ca_active,	// game views should be displayed
	ca_cinematic,	// playing a cinematic, not connected to a server
} connstate_t;

enum ref_defaultsprite_e
{
	REF_DOT_SPRITE, // cl_sprite_dot
	REF_CHROME_SPRITE // cl_sprite_shell
};

// the order of first three is important!
// so you can use this value in IEngineStudio.StudioIsHardware
enum ref_graphic_apis_e
{
	REF_SOFTWARE,	// hypothetical: just make a surface to draw on, in software
	REF_GL,		// create GL context
	REF_D3D,	// Direct3D
};

typedef enum
{
	SAFE_NO = 0,
	SAFE_NOMSAA,      // skip msaa
	SAFE_NOACC,       // don't set acceleration flag
	SAFE_NOSTENCIL,   // don't set stencil bits
	SAFE_NOALPHA,     // don't set alpha bits
	SAFE_NODEPTH,     // don't set depth bits
	SAFE_NOCOLOR,     // don't set color bits
	SAFE_DONTCARE,    // ignore everything, let SDL/EGL decide
	SAFE_LAST,        // must be last
} ref_safegl_context_t;

enum // OpenGL configuration attributes
{
	REF_GL_RED_SIZE,
	REF_GL_GREEN_SIZE,
	REF_GL_BLUE_SIZE,
	REF_GL_ALPHA_SIZE,
	REF_GL_DOUBLEBUFFER,
	REF_GL_DEPTH_SIZE,
	REF_GL_STENCIL_SIZE,
	REF_GL_MULTISAMPLEBUFFERS,
	REF_GL_MULTISAMPLESAMPLES,
	REF_GL_ACCELERATED_VISUAL,
	REF_GL_CONTEXT_MAJOR_VERSION,
	REF_GL_CONTEXT_MINOR_VERSION,
	REF_GL_CONTEXT_EGL,
	REF_GL_CONTEXT_FLAGS,
	REF_GL_CONTEXT_PROFILE_MASK,
	REF_GL_SHARE_WITH_CURRENT_CONTEXT,
	REF_GL_FRAMEBUFFER_SRGB_CAPABLE,
	REF_GL_CONTEXT_RELEASE_BEHAVIOR,
	REF_GL_CONTEXT_RESET_NOTIFICATION,
	REF_GL_CONTEXT_NO_ERROR,
	REF_GL_ATTRIBUTES_COUNT,
};

enum
{
	REF_GL_CONTEXT_PROFILE_CORE           = 0x0001,
	REF_GL_CONTEXT_PROFILE_COMPATIBILITY  = 0x0002,
	REF_GL_CONTEXT_PROFILE_ES             = 0x0004 /**< GLX_CONTEXT_ES2_PROFILE_BIT_EXT */
};

// binary compatible with SDL and EGL_KHR_create_context(0x0007 mask)
enum
{
	REF_GL_CONTEXT_DEBUG_FLAG              = 0x0001,
	REF_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG = 0x0002,
	REF_GL_CONTEXT_ROBUST_ACCESS_FLAG      = 0x0004,
	REF_GL_CONTEXT_RESET_ISOLATION_FLAG    = 0x0008
};

typedef enum ref_screen_rotation_e
{
	REF_ROTATE_NONE = 0,
	REF_ROTATE_CW = 1,
	REF_ROTATE_UD = 2,
	REF_ROTATE_CCW = 3,
} ref_screen_rotation_t;

typedef struct remap_info_s
{
	unsigned short	textures[MAX_SKINS];// alias textures
	struct mstudiotex_s	*ptexture;	// array of textures with local copy of remapped textures
	short		numtextures;	// textures count
	short		topcolor;		// cached value
	short		bottomcolor;	// cached value
	model_t		*model;		// for catch model changes
} remap_info_t;

typedef struct convar_s convar_t;
struct con_nprint_s;
struct engine_studio_api_s;
struct r_studio_interface_s;

typedef enum
{
	PARM_DEV_OVERVIEW      = -1,
	PARM_THIRDPERSON       = -2,
	PARM_QUAKE_COMPATIBLE  = -3,
	PARM_PLAYER_INDEX      = -4, // cl.playernum + 1
	PARM_VIEWENT_INDEX     = -5, // cl.viewentity
	PARM_CONNSTATE         = -6, // cls.state
	PARM_PLAYING_DEMO      = -7, // cls.demoplayback
	PARM_WATER_LEVEL       = -8, // cl.local.water_level
	PARM_MAX_CLIENTS       = -9, // cl.maxclients
	PARM_LOCAL_HEALTH      = -10, // cl.local.health
	PARM_LOCAL_GAME        = -11,
	PARM_NUMENTITIES       = -12, // local game only
	PARM_NUMMODELS         = -13, // cl.nummodels
} ref_parm_e;

typedef struct ref_api_s
{
	intptr_t (*EngineGetParm)( int parm, int arg );	// generic

	// cvar handlers
	cvar_t   *(*Cvar_Get)( const char *szName, const char *szValue, int flags, const char *description );
	cvar_t   *(*pfnGetCvarPointer)( const char *name, int ignore_flags );
	float       (*pfnGetCvarFloat)( const char *szName );
	const char *(*pfnGetCvarString)( const char *szName );
	void        (*Cvar_SetValue)( const char *name, float value );
	void        (*Cvar_Set)( const char *name, const char *value );
	void (*Cvar_RegisterVariable)( convar_t *var );
	void (*Cvar_FullSet)( const char *var_name, const char *value, int flags );

	// command handlers
	int         (*Cmd_AddCommand)( const char *cmd_name, void (*function)(void), const char *description );
	void        (*Cmd_RemoveCommand)( const char *cmd_name );
	int         (*Cmd_Argc)( void );
	const char *(*Cmd_Argv)( int arg );
	const char *(*Cmd_Args)( void );

	// cbuf
	void (*Cbuf_AddText)( const char *commands );
	void (*Cbuf_InsertText)( const char *commands );
	void (*Cbuf_Execute)( void );

	// logging
	void	(*Con_Printf)( const char *fmt, ... ) _format( 1 ); // typical console allowed messages
	void	(*Con_DPrintf)( const char *fmt, ... ) _format( 1 ); // -dev 1
	void	(*Con_Reportf)( const char *fmt, ... ) _format( 1 ); // -dev 2

	// debug print
	void	(*Con_NPrintf)( int pos, const char *fmt, ... ) _format( 2 );
	void	(*Con_NXPrintf)( struct con_nprint_s *info, const char *fmt, ... ) _format( 2 );
	void	(*CL_CenterPrint)( const char *s, float y );
	void (*Con_DrawStringLen)( const char *pText, int *length, int *height );
	int (*Con_DrawString)( int x, int y, const char *string, rgba_t setColor );
	void	(*CL_DrawCenterPrint)( void );

	// entity management
	struct cl_entity_s *(*GetLocalPlayer)( void );
	struct cl_entity_s *(*GetViewModel)( void );
	struct cl_entity_s *(*GetEntityByIndex)( int idx );
	struct cl_entity_s *(*R_BeamGetEntity)( int index );
	struct cl_entity_s *(*CL_GetWaterEntity)( const vec3_t p );
	qboolean (*CL_AddVisibleEntity)( cl_entity_t *ent, int entityType );

	// brushes
	int (*Mod_SampleSizeForFace)( struct msurface_s *surf );
	qboolean (*Mod_BoxVisible)( const vec3_t mins, const vec3_t maxs, const byte *visbits );
	struct world_static_s *(*GetWorld)( void ); // returns &world
	mleaf_t *(*Mod_PointInLeaf)( const vec3_t p, mnode_t *node );
	void (*Mod_CreatePolygonsForHull)( int hullnum );

	// studio models
	void *(*R_StudioGetAnim)( studiohdr_t *m_pStudioHeader, model_t *m_pSubModel, mstudioseqdesc_t *pseqdesc );
	void	(*pfnStudioEvent)( const struct mstudioevent_s *event, const cl_entity_t *entity );

	// efx
	void (*CL_DrawEFX)( float time, qboolean fTrans );
	void (*CL_ThinkParticle)( double frametime, particle_t *p );
	void (*R_FreeDeadParticles)( particle_t **ppparticles );
	particle_t *(*CL_AllocParticleFast)( void ); // unconditionally give new particle pointer from cl_free_particles
	struct dlight_s *(*CL_AllocElight)( int key );
	struct model_s *(*GetDefaultSprite)( enum ref_defaultsprite_e spr );
	void		(*R_StoreEfrags)( struct efrag_s **ppefrag, int framecount );// store efrags for static entities

	// model management
	model_t *(*Mod_ForName)( const char *name, qboolean crash, qboolean trackCRC );
	void *(*Mod_Extradata)( int type, model_t *model );
	struct model_s *(*pfnGetModelByIndex)( int index ); // CL_ModelHandle
	struct model_s *(*Mod_GetCurrentLoadingModel)( void ); // loadmodel
	void (*Mod_SetCurrentLoadingModel)( struct model_s* ); // loadmodel

	// remap
	struct remap_info_s *(*CL_GetRemapInfoForEntity)( cl_entity_t *e );
	void (*CL_AllocRemapInfo)( cl_entity_t *entity, model_t *model, int topcolor, int bottomcolor );
	void (*CL_FreeRemapInfo)( struct remap_info_s *info );
	void (*CL_UpdateRemapInfo)( cl_entity_t *entity, int topcolor, int bottomcolor );

	// utils
	void  (*CL_ExtraUpdate)( void );
	void  (*Host_Error)( const char *fmt, ... ) _format( 1 );
	void  (*COM_SetRandomSeed)( int lSeed );
	float (*COM_RandomFloat)( float rmin, float rmax );
	int   (*COM_RandomLong)( int rmin, int rmax );
	struct screenfade_s *(*GetScreenFade)( void );
	struct client_textmessage_s *(*pfnTextMessageGet)( const char *pName );
	void (*GetPredictedOrigin)( vec3_t v );
	color24 *(*CL_GetPaletteColor)(int color); // clgame.palette[color]
	void (*CL_GetScreenInfo)( int *width, int *height ); // clgame.scrInfo, ptrs may be NULL
	void (*SetLocalLightLevel)( int level ); // cl.local.light_level
	int (*Sys_CheckParm)( const char *flag );

	// studio interface
	player_info_t *(*pfnPlayerInfo)( int index );
	entity_state_t *(*pfnGetPlayerState)( int index );
	void *(*Mod_CacheCheck)( struct cache_user_s *c );
	void (*Mod_LoadCacheFile)( const char *path, struct cache_user_s *cu );
	void *(*Mod_Calloc)( int number, size_t size );
	int	(*pfnGetStudioModelInterface)( int version, struct r_studio_interface_s **ppinterface, struct engine_studio_api_s *pstudio );

	// memory
	poolhandle_t (*_Mem_AllocPool)( const char *name, const char *filename, int fileline );
	void  (*_Mem_FreePool)( poolhandle_t *poolptr, const char *filename, int fileline );
	void *(*_Mem_Alloc)( poolhandle_t poolptr, size_t size, qboolean clear, const char *filename, int fileline );
	void *(*_Mem_Realloc)( poolhandle_t poolptr, void *memptr, size_t size, qboolean clear, const char *filename, int fileline );
	void  (*_Mem_Free)( void *data, const char *filename, int fileline );

	// library management
	void *(*COM_LoadLibrary)( const char *name, int build_ordinals_table, qboolean directpath );
	void  (*COM_FreeLibrary)( void *handle );
	void *(*COM_GetProcAddress)( void *handle, const char *name );

	// video init
	// try to create window
	// will call GL_SetupAttributes in case of REF_GL
	qboolean  (*R_Init_Video)( int type ); // will also load and execute renderer config(see R_GetConfigName)
	void (*R_Free_Video)( void );

	// GL
	int   (*GL_SetAttribute)( int attr, int value );
	int   (*GL_GetAttribute)( int attr, int *value );
	void *(*GL_GetProcAddress)( const char *name );
	void (*GL_SwapBuffers)( void );

	// SW
	qboolean (*SW_CreateBuffer)( int width, int height, uint *stride, uint *bpp, uint *r, uint *g, uint *b );
	void *(*SW_LockBuffer)( void );
	void (*SW_UnlockBuffer)( void );

	// gamma
	void (*BuildGammaTable)( float lightgamma, float brightness );
	byte		(*LightToTexGamma)( byte color );	// software gamma support
	qboolean	(*R_DoResetGamma)( void );

	// renderapi
	lightstyle_t*	(*GetLightStyle)( int number );
	dlight_t*	(*GetDynamicLight)( int number );
	dlight_t*	(*GetEntityLight)( int number );
	int		(*R_FatPVS)( const float *org, float radius, byte *visbuffer, qboolean merge, qboolean fullvis );
	const struct ref_overview_s *( *GetOverviewParms )( void );
	double		(*pfnTime)( void );				// Sys_DoubleTime

	// event api
	struct physent_s *(*EV_GetPhysent)( int idx );
	struct msurface_s *( *EV_TraceSurface )( int ground, float *vstart, float *vend );
	struct pmtrace_s *(*PM_TraceLine)( float *start, float *end, int flags, int usehull, int ignore_pe );
	struct pmtrace_s *(*EV_VisTraceLine )( float *start, float *end, int flags );
	struct pmtrace_s (*CL_TraceLine)( vec3_t start, vec3_t end, int flags );
	struct movevars_s *(*pfnGetMoveVars)( void );

	// imagelib
	void (*Image_AddCmdFlags)( uint flags ); // used to check if hardware dxt is supported
	void (*Image_SetForceFlags)( uint flags );
	void (*Image_ClearForceFlags)( void );
	qboolean (*Image_CustomPalette)( void );
	qboolean (*Image_Process)( rgbdata_t **pix, int width, int height, uint flags, float reserved );
	rgbdata_t *(*FS_LoadImage)( const char *filename, const byte *buffer, size_t size );
	qboolean (*FS_SaveImage)( const char *filename, rgbdata_t *pix );
	rgbdata_t *(*FS_CopyImage)( rgbdata_t *in );
	void (*FS_FreeImage)( rgbdata_t *pack );
	void (*Image_SetMDLPointer)( byte *p );
	poolhandle_t (*Image_GetPool)( void );
	const struct bpc_desc_s *(*Image_GetPFDesc)( int idx );

	// client exports
	void	(*pfnDrawNormalTriangles)( void );
	void	(*pfnDrawTransparentTriangles)( void );
	render_interface_t	*drawFuncs;

	// filesystem exports
	fs_api_t	*fsapi;
} ref_api_t;

struct mip_s;

// render callbacks
typedef struct ref_interface_s
{
	// construct, destruct
	qboolean (*R_Init)( void ); // context is true if you need context management
	// const char *(*R_GetInitError)( void );
	void (*R_Shutdown)( void );
	const char *(*R_GetConfigName)( void ); // returns config name without extension
	qboolean (*R_SetDisplayTransform)( ref_screen_rotation_t rotate, int x, int y, float scale_x, float scale_y );

	// only called for GL contexts
	void (*GL_SetupAttributes)( int safegl );
	void (*GL_InitExtensions)( void );
	void (*GL_ClearExtensions)( void );

	void (*R_BeginFrame)( qboolean clearScene );
	void (*R_RenderScene)( void );
	void (*R_EndFrame)( void );
	void (*R_PushScene)( void );
	void (*R_PopScene)( void );
	void (*GL_BackendStartFrame)( void );
	void (*GL_BackendEndFrame)( void );

	void (*R_ClearScreen)( void ); // clears color buffer on GL
	void (*R_AllowFog)( qboolean allow );
	void (*GL_SetRenderMode)( int renderMode );

	qboolean (*R_AddEntity)( struct cl_entity_s *clent, int type );
	void (*CL_AddCustomBeam)( cl_entity_t *pEnvBeam );
	void		(*R_ProcessEntData)( qboolean allocate );

	// debug
	void (*R_ShowTextures)( void );

	// texture management
	const byte *(*R_GetTextureOriginalBuffer)( unsigned int idx ); // not always available
	int (*GL_LoadTextureFromBuffer)( const char *name, rgbdata_t *pic, texFlags_t flags, qboolean update );
	void (*GL_ProcessTexture)( int texnum, float gamma, int topColor, int bottomColor );
	void (*R_SetupSky)( const char *skyname );

	// 2D
	void (*R_Set2DMode)( qboolean enable );
	void (*R_DrawStretchRaw)( float x, float y, float w, float h, int cols, int rows, const byte *data, qboolean dirty );
	void (*R_DrawStretchPic)( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int texnum );
	void (*R_DrawTileClear)( int texnum, int x, int y, int w, int h );
	void (*FillRGBA)( float x, float y, float w, float h, int r, int g, int b, int a ); // in screen space
	void (*FillRGBABlend)( float x, float y, float w, float h, int r, int g, int b, int a ); // in screen space
	int  (*WorldToScreen)( const vec3_t world, vec3_t screen );  // Returns 1 if it's z clipped

	// screenshot, cubemapshot
	qboolean (*VID_ScreenShot)( const char *filename, int shot_type );
	qboolean (*VID_CubemapShot)( const char *base, uint size, const float *vieworg, qboolean skyshot );

	// light
	colorVec (*R_LightPoint)( const float *p );

	// decals
	// Shoots a decal onto the surface of the BSP.  position is the center of the decal in world coords
	void (*R_DecalShoot)( int textureIndex, int entityIndex, int modelIndex, vec3_t pos, int flags, float scale );
	void (*R_DecalRemoveAll)( int texture );
	int (*R_CreateDecalList)( struct decallist_s *pList );
	void (*R_ClearAllDecals)( void );

	// studio interface
	float (*R_StudioEstimateFrame)( cl_entity_t *e, mstudioseqdesc_t *pseqdesc, double time );
	void (*R_StudioLerpMovement)( cl_entity_t *e, double time, vec3_t origin, vec3_t angles );
	void (*CL_InitStudioAPI)( void );

	// bmodel
	void (*R_InitSkyClouds)( struct mip_s *mt, struct texture_s *tx, qboolean custom_palette );
	void (*GL_SubdivideSurface)( msurface_t *fa );
	void (*CL_RunLightStyles)( void );

	// sprites
	void (*R_GetSpriteParms)( int *frameWidth, int *frameHeight, int *numFrames, int currentFrame, const model_t *pSprite );
	int (*R_GetSpriteTexture)( const model_t *m_pSpriteModel, int frame );

	// model management
	// flags ignored for everything except spritemodels
	void (*Mod_LoadMapSprite)( struct model_s *mod, const void *buffer, size_t size, qboolean *loaded );
	qboolean (*Mod_ProcessRenderData)( model_t *mod, qboolean create, const byte *buffer );
	void (*Mod_StudioLoadTextures)( model_t *mod, void *data );

	// efx implementation
	void (*CL_DrawParticles)( double frametime, particle_t *particles, float partsize );
	void (*CL_DrawTracers)( double frametime, particle_t *tracers );
	void (*CL_DrawBeams)( int fTrans , BEAM *beams );
	qboolean (*R_BeamCull)( const vec3_t start, const vec3_t end, qboolean pvsOnly );

	// Xash3D Render Interface
	// Get renderer info (doesn't changes engine state at all)
	int			(*RefGetParm)( int parm, int arg );	// generic
	void		(*GetDetailScaleForTexture)( int texture, float *xScale, float *yScale );
	void		(*GetExtraParmsForTexture)( int texture, byte *red, byte *green, byte *blue, byte *alpha );
	float		(*GetFrameTime)( void );

	// Set renderer info (tell engine about changes)
	void		(*R_SetCurrentEntity)( struct cl_entity_s *ent ); // tell engine about both currententity and currentmodel
	void		(*R_SetCurrentModel)( struct model_s *mod );	// change currentmodel but leave currententity unchanged

	// Texture tools
	int		(*GL_FindTexture)( const char *name );
	const char*	(*GL_TextureName)( unsigned int texnum );
	const byte*	(*GL_TextureData)( unsigned int texnum ); // may be NULL
	int		(*GL_LoadTexture)( const char *name, const byte *buf, size_t size, int flags );
	int		(*GL_CreateTexture)( const char *name, int width, int height, const void *buffer, texFlags_t flags );
	int		(*GL_LoadTextureArray)( const char **names, int flags );
	int		(*GL_CreateTextureArray)( const char *name, int width, int height, int depth, const void *buffer, texFlags_t flags );
	void		(*GL_FreeTexture)( unsigned int texnum );

	// Decals manipulating (draw & remove)
	void		(*DrawSingleDecal)( struct decal_s *pDecal, struct msurface_s *fa );
	float		*(*R_DecalSetupVerts)( struct decal_s *pDecal, struct msurface_s *surf, int texture, int *outCount );
	void		(*R_EntityRemoveDecals)( struct model_s *mod ); // remove all the decals from specified entity (BSP only)

	// AVI
	void		(*AVI_UploadRawFrame)( int texture, int cols, int rows, int width, int height, const byte *data );

	// glState related calls (must use this instead of normal gl-calls to prevent de-synchornize local states between engine and the client)
	void		(*GL_Bind)( int tmu, unsigned int texnum );
	void		(*GL_SelectTexture)( int tmu );
	void		(*GL_LoadTextureMatrix)( const float *glmatrix );
	void		(*GL_TexMatrixIdentity)( void );
	void		(*GL_CleanUpTextureUnits)( int last );	// pass 0 for clear all the texture units
	void		(*GL_TexGen)( unsigned int coord, unsigned int mode );
	void		(*GL_TextureTarget)( unsigned int target ); // change texture unit mode without bind texture
	void		(*GL_TexCoordArrayMode)( unsigned int texmode );
	void		(*GL_UpdateTexSize)( int texnum, int width, int height, int depth ); // recalc statistics
	void		(*GL_Reserved0)( void );	// for potential interface expansion without broken compatibility
	void		(*GL_Reserved1)( void );

	// Misc renderer functions
	void		(*GL_DrawParticles)( const struct ref_viewpass_s *rvp, qboolean trans_pass, float frametime );
	colorVec		(*LightVec)( const float *start, const float *end, float *lightspot, float *lightvec );
	struct mstudiotex_s *( *StudioGetTexture )( struct cl_entity_s *e );

	// passed through R_RenderFrame (0 - use engine renderer, 1 - use custom client renderer)
	void		(*GL_RenderFrame)( const struct ref_viewpass_s *rvp );
	// setup map bounds for ortho-projection when we in dev_overview mode
	void		(*GL_OrthoBounds)( const float *mins, const float *maxs );
	// grab r_speeds message
	qboolean	(*R_SpeedsMessage)( char *out, size_t size );
	// get visdata for current frame from custom renderer
	byte*		(*Mod_GetCurrentVis)( void );
	// tell the renderer what new map is started
	void		(*R_NewMap)( void );
	// clear the render entities before each frame
	void		(*R_ClearScene)( void );
	// GL_GetProcAddress for client renderer
	void*		(*R_GetProcAddress)( const char *name );

	// TriAPI Interface
	// NOTE: implementation isn't required to be compatible
	void	(*TriRenderMode)( int mode );
	void	(*Begin)( int primitiveCode );
	void	(*End)( void );
	void	(*Color4f)( float r, float g, float b, float a ); // real glColor4f
	void	(*Color4ub)( unsigned char r, unsigned char g, unsigned char b, unsigned char a ); // real glColor4ub
	void	(*TexCoord2f)( float u, float v );
	void	(*Vertex3fv)( const float *worldPnt );
	void	(*Vertex3f)( float x, float y, float z );
	void	(*Fog)( float flFogColor[3], float flStart, float flEnd, int bOn ); //Works just like GL_FOG, flFogColor is r/g/b.
	void	(*ScreenToWorld)( const float *screen, float *world  );
	void	(*GetMatrix)( const int pname, float *matrix );
	void	(*FogParams)( float flDensity, int iFogSkybox );
	void    (*CullFace)( TRICULLSTYLE mode );

	// vgui drawing implementation
	void	(*VGUI_DrawInit)( void );
	void	(*VGUI_DrawShutdown)( void );
	void	(*VGUI_SetupDrawingText)( int *pColor );
	void	(*VGUI_SetupDrawingRect)( int *pColor );
	void	(*VGUI_SetupDrawingImage)( int *pColor );
	void	(*VGUI_BindTexture)( int id );
	void	(*VGUI_EnableTexture)( qboolean enable );
	void	(*VGUI_CreateTexture)( int id, int width, int height );
	void	(*VGUI_UploadTexture)( int id, const char *buffer, int width, int height );
	void	(*VGUI_UploadTextureBlock)( int id, int drawX, int drawY, const byte *rgba, int blockWidth, int blockHeight );
	void	(*VGUI_DrawQuad)( const vpoint_t *ul, const vpoint_t *lr );
	void	(*VGUI_GetTextureSizes)( int *width, int *height );
	int		(*VGUI_GenerateTexture)( void );
} ref_interface_t;

typedef int (*REFAPI)( int version, ref_interface_t *pFunctionTable, ref_api_t* engfuncs, ref_globals_t *pGlobals );
#define GET_REF_API "GetRefAPI"

#ifdef REF_DLL
#define DEFINE_ENGINE_SHARED_CVAR( x, y ) cvar_t *x = NULL;
#define DECLARE_ENGINE_SHARED_CVAR( x, y ) extern cvar_t *x;
#define RETRIEVE_ENGINE_SHARED_CVAR( x, y ) \
	if(!( x = gEngfuncs.pfnGetCvarPointer( #y, 0 ) )) \
		gEngfuncs.Host_Error( S_ERROR "engine didn't gave us %s cvar pointer\n", #y );
#define ENGINE_SHARED_CVAR_NAME( f, x, y ) f( x, y )
#define ENGINE_SHARED_CVAR( f, x ) ENGINE_SHARED_CVAR_NAME( f, x, x )

// cvars that's logic is shared between renderer and engine
// actually, they are just created on engine side for convinience
// and must be retrieved by renderer side
// sometimes it's done to standartize cvars to make it easier for users
#define ENGINE_SHARED_CVAR_LIST( f ) \
	ENGINE_SHARED_CVAR_NAME( f, vid_gamma, gamma ) \
	ENGINE_SHARED_CVAR_NAME( f, vid_brightness, brightness ) \
	ENGINE_SHARED_CVAR( f, r_showtextures ) \
	ENGINE_SHARED_CVAR( f, r_speeds ) \
	ENGINE_SHARED_CVAR( f, r_fullbright ) \
	ENGINE_SHARED_CVAR( f, r_norefresh ) \
	ENGINE_SHARED_CVAR( f, r_lightmap ) \
	ENGINE_SHARED_CVAR( f, r_dynamic ) \
	ENGINE_SHARED_CVAR( f, r_drawentities ) \
	ENGINE_SHARED_CVAR( f, r_decals ) \
	ENGINE_SHARED_CVAR( f, r_showhull ) \
	ENGINE_SHARED_CVAR( f, gl_vsync ) \
	ENGINE_SHARED_CVAR( f, gl_clear ) \
	ENGINE_SHARED_CVAR( f, cl_himodels ) \
	ENGINE_SHARED_CVAR( f, cl_lightstyle_lerping ) \
	ENGINE_SHARED_CVAR( f, tracerred ) \
	ENGINE_SHARED_CVAR( f, tracergreen ) \
	ENGINE_SHARED_CVAR( f, tracerblue ) \
	ENGINE_SHARED_CVAR( f, traceralpha ) \
	ENGINE_SHARED_CVAR( f, r_sprite_lerping ) \
	ENGINE_SHARED_CVAR( f, r_sprite_lighting ) \
	ENGINE_SHARED_CVAR( f, r_drawviewmodel ) \
	ENGINE_SHARED_CVAR( f, r_glowshellfreq ) \
	ENGINE_SHARED_CVAR( f, r_lighting_modulate ) \

#define DECLARE_ENGINE_SHARED_CVAR_LIST() \
	ENGINE_SHARED_CVAR_LIST( DECLARE_ENGINE_SHARED_CVAR )

#define DEFINE_ENGINE_SHARED_CVAR_LIST() \
	ENGINE_SHARED_CVAR_LIST( DEFINE_ENGINE_SHARED_CVAR )

#define RETRIEVE_ENGINE_SHARED_CVAR_LIST() \
	ENGINE_SHARED_CVAR_LIST( RETRIEVE_ENGINE_SHARED_CVAR )
#endif

#endif // REF_API
