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
#include "common/protocol.h"

// RefAPI changelog:
// 1. Initial release
// 2. FS functions are removed, instead we have full fs_api_t
// 3. SlerpBones, CalcBonePosition/Quaternion calls were moved to libpublic/mathlib
// 4. R_StudioEstimateFrame now has time argument
// 5. Removed GetSomethingByIndex calls, renderers are supposed to cache pointer values
//    Removed previously unused calls
//    Simplified remapping calls
//    GetRefAPI is now expected to return REF_API_VERSION
// 6. Removed timing from ref_globals_t.
//    Renderers are supposed to migrate to ref_client_t/ref_host_t using PARM_GET_CLIENT_PTR and PARM_GET_HOST_PTR
//    Removed functions to get internal engine structions. Use PARM_GET_*_PTR instead.
// 7. Gamma fixes.
// 8. Moved common code to engine.
//    Removed REF_{SOLID,ALPHA}SKY_TEXTURE. Replaced R_InitSkyClouds by R_SetSkyCloudsTextures.
//    Skybox loading is now done at engine side.
//    R_SetupSky callback accepts a pointer to an array of 6 integers representing box side textures.
//    Restored texture replacement from old Xash3D.
//    PARM_SKY_SPHERE and PARM_SURF_SAMPLESIZE are now handled at engine side.
//    VGUI rendering code is mostly moved back to engine.
//    Implemented texture replacement.
// 9. Removed gamma functions. Renderer is supposed to get them through PARM_GET_*_PTR.
//    Move hulls rendering back to engine
//    Removed lightstyle, dynamic and entity light functions. Renderer is supposed to get them through PARM_GET_*_PTR.
//    CL_RunLightStyles now accepts lightstyles array.
//    Removed R_DrawTileClear and Mod_LoadMapSprite, as they're implemented on engine side
//    Removed FillRGBABlend. Now FillRGBA accepts rendermode parameter.
// 10. Added R_GetWindowHandle to retrieve platform-specific window object.
#define REF_API_VERSION 10

#define TF_SKY		(TF_SKYSIDE|TF_NOMIPMAP|TF_ALLOW_NEAREST)
#define TF_FONT		(TF_NOMIPMAP|TF_CLAMP|TF_ALLOW_NEAREST)
#define TF_IMAGE		(TF_NOMIPMAP|TF_CLAMP)
#define TF_DECAL		(TF_CLAMP)

#define FCONTEXT_CORE_PROFILE		BIT( 0 )
#define FCONTEXT_DEBUG_ARB		BIT( 1 )

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

#define SKYBOX_MAX_SIDES 6 // a box can only have 6 sides

typedef enum
{
	DEMO_INACTIVE = 0,
	DEMO_XASH3D,
	DEMO_QUAKE1
} demo_mode;

typedef enum ref_window_type_e
{
	REF_WINDOW_TYPE_NULL = 0,
	REF_WINDOW_TYPE_WIN32, // HWND
	REF_WINDOW_TYPE_X11, // Display*
	REF_WINDOW_TYPE_WAYLAND, // wl_display*
	REF_WINDOW_TYPE_MACOS, // NSWindow*
	REF_WINDOW_TYPE_SDL, // SDL_Window*
} ref_window_type_t;

typedef struct
{
	msurface_t	*surf;
	int		cull;
} sortedface_t;

typedef struct ref_globals_s
{
	qboolean developer;

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

typedef struct ref_client_s
{
	double   time;
	double   oldtime;
	int      viewentity;
	int      playernum;
	int      maxclients;
	int      nummodels;
	model_t *models[MAX_MODELS+1];
	qboolean paused;
	vec3_t   simorg;
} ref_client_t;

typedef struct ref_host_s
{
	double realtime;
	double frametime;
	int    features;
} ref_host_t;

enum
{
	GL_KEEP_UNIT = -1,
	XASH_TEXTURE0 = 0,
	XASH_TEXTURE1,
	XASH_TEXTURE2,
	XASH_TEXTURE3,		// g-cont. 4 units should be enough
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
	PARM_GET_CLIENT_PTR    = -4, // ref_client_t
	PARM_GET_HOST_PTR      = -5, // ref_host_t
	PARM_CONNSTATE         = -6, // cls.state
	PARM_PLAYING_DEMO      = -7, // cls.demoplayback
	PARM_WATER_LEVEL       = -8, // cl.local.water_level
	PARM_GET_WORLD_PTR     = -9, // world
	PARM_LOCAL_HEALTH      = -10, // cl.local.health
	PARM_LOCAL_GAME        = -11,
	PARM_NUMENTITIES       = -12, // local game only
	PARM_GET_MOVEVARS_PTR  = -13, // clgame.movevars
	PARM_GET_PALETTE_PTR   = -14, // clgame.palette
	PARM_GET_VIEWENT_PTR   = -15, // clgame.viewent

	PARM_GET_TEXGAMMATABLE_PTR = -16,
	PARM_GET_LIGHTGAMMATABLE_PTR = -17,
	PARM_GET_SCREENGAMMATABLE_PTR = -18,
	PARM_GET_LINEARGAMMATABLE_PTR = -19,

	PARM_GET_LIGHTSTYLES_PTR = -20,
	PARM_GET_DLIGHTS_PTR = -21,
	PARM_GET_ELIGHTS_PTR = -22,

	// implemented by ref_dll

	// returns non-null integer if filtering is enabled for texture
	// pass -1 to query global filtering settings
	PARM_TEX_FILTERING     = -0x10000,
} ref_parm_e;

typedef struct ref_api_s
{
	intptr_t (*EngineGetParm)( int parm, int arg );	// generic

	// cvar handlers
	cvar_t   *(*Cvar_Get)( const char *szName, const char *szValue, int flags, const char *description );
	cvar_t   *(*pfnGetCvarPointer)( const char *name, int ignore_flags );
	float       (*pfnGetCvarFloat)( const char *szName );
	const char *(*pfnGetCvarString)( const char *szName ) PFN_RETURNS_NONNULL;
	void        (*Cvar_SetValue)( const char *name, float value );
	void        (*Cvar_Set)( const char *name, const char *value );
	void (*Cvar_RegisterVariable)( convar_t *var );
	void (*Cvar_FullSet)( const char *var_name, const char *value, int flags );

	// command handlers
	int         (*Cmd_AddCommand)( const char *cmd_name, void (*function)(void), const char *description );
	void        (*Cmd_RemoveCommand)( const char *cmd_name );
	int         (*Cmd_Argc)( void );
	const char *(*Cmd_Argv)( int arg ) PFN_RETURNS_NONNULL;
	const char *(*Cmd_Args)( void ) PFN_RETURNS_NONNULL;

	// cbuf
	void (*Cbuf_AddText)( const char *commands );
	void (*Cbuf_InsertText)( const char *commands );
	void (*Cbuf_Execute)( void );

	// logging
	void	(*Con_Printf)( const char *fmt, ... ) FORMAT_CHECK( 1 ); // typical console allowed messages
	void	(*Con_DPrintf)( const char *fmt, ... ) FORMAT_CHECK( 1 ); // -dev 1
	void	(*Con_Reportf)( const char *fmt, ... ) FORMAT_CHECK( 1 ); // -dev 2

	// debug print
	void	(*Con_NPrintf)( int pos, const char *fmt, ... ) FORMAT_CHECK( 2 );
	void	(*Con_NXPrintf)( struct con_nprint_s *info, const char *fmt, ... ) FORMAT_CHECK( 2 );
	void	(*CL_CenterPrint)( const char *s, float y );
	void (*Con_DrawStringLen)( const char *pText, int *length, int *height );
	int (*Con_DrawString)( int x, int y, const char *string, const rgba_t setColor );
	void	(*CL_DrawCenterPrint)( void );

	// entity management
	struct cl_entity_s *(*R_BeamGetEntity)( int index );
	struct cl_entity_s *(*CL_GetWaterEntity)( const vec3_t p );
	qboolean (*CL_AddVisibleEntity)( cl_entity_t *ent, int entityType );

	// brushes
	int (*Mod_SampleSizeForFace)( const struct msurface_s *surf );
	qboolean (*Mod_BoxVisible)( const vec3_t mins, const vec3_t maxs, const byte *visbits );
	mleaf_t *(*Mod_PointInLeaf)( const vec3_t p, mnode_t *node );
	void (*R_DrawWorldHull)( void );
	void (*R_DrawModelHull)( model_t *mod );

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

	// remap
	qboolean (*CL_EntitySetRemapColors)( cl_entity_t *e, model_t *mod, int top, int bottom );
	struct remap_info_s *(*CL_GetRemapInfoForEntity)( cl_entity_t *e );

	// utils
	void  (*CL_ExtraUpdate)( void );
	void  (*Host_Error)( const char *fmt, ... ) FORMAT_CHECK( 1 );
	void  (*COM_SetRandomSeed)( int lSeed );
	float (*COM_RandomFloat)( float rmin, float rmax );
	int   (*COM_RandomLong)( int rmin, int rmax );
	struct screenfade_s *(*GetScreenFade)( void );
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
	poolhandle_t (*_Mem_AllocPool)( const char *name, const char *filename, int fileline )
		WARN_UNUSED_RESULT;
	void  (*_Mem_FreePool)( poolhandle_t *poolptr, const char *filename, int fileline );
	void *(*_Mem_Alloc)( poolhandle_t poolptr, size_t size, qboolean clear, const char *filename, int fileline )
		ALLOC_CHECK( 2 ) WARN_UNUSED_RESULT;
	void *(*_Mem_Realloc)( poolhandle_t poolptr, void *memptr, size_t size, qboolean clear, const char *filename, int fileline )
		ALLOC_CHECK( 3 ) WARN_UNUSED_RESULT;
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

	// renderapi
	int		(*R_FatPVS)( const float *org, float radius, byte *visbuffer, qboolean merge, qboolean fullvis );
	const struct ref_overview_s *( *GetOverviewParms )( void );
	double		(*pfnTime)( void );				// Sys_DoubleTime

	// event api
	struct physent_s *(*EV_GetPhysent)( int idx );
	struct msurface_s *( *EV_TraceSurface )( int ground, float *vstart, float *vend );
	struct pmtrace_s *(*PM_TraceLine)( float *start, float *end, int flags, int usehull, int ignore_pe );
	struct pmtrace_s *(*EV_VisTraceLine )( float *start, float *end, int flags );
	struct pmtrace_s (*CL_TraceLine)( vec3_t start, vec3_t end, int flags );

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
	const struct bpc_desc_s *(*Image_GetPFDesc)( int idx );

	// client exports
	void	(*pfnDrawNormalTriangles)( void );
	void	(*pfnDrawTransparentTriangles)( void );
	render_interface_t	*drawFuncs;

	// filesystem exports
	fs_api_t	*fsapi;

	// for abstracting the engine's rendering
	ref_window_type_t (*R_GetWindowHandle)( void **handle, ref_window_type_t type );
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

	// scene rendering
	void (*R_GammaChanged)( qboolean do_reset_gamma );
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
	void (*R_ProcessEntData)( qboolean allocate, cl_entity_t *entities, unsigned int max_entities );
	void (*R_Flush)( unsigned int flush_flags );

	// debug
	void (*R_ShowTextures)( void );

	// texture management
	const byte *(*R_GetTextureOriginalBuffer)( unsigned int idx ); // not always available
	int (*GL_LoadTextureFromBuffer)( const char *name, rgbdata_t *pic, texFlags_t flags, qboolean update );
	void (*GL_ProcessTexture)( int texnum, float gamma, int topColor, int bottomColor );
	void (*R_SetupSky)( int *skyboxTextures );

	// 2D
	void (*R_Set2DMode)( qboolean enable );
	void (*R_DrawStretchRaw)( float x, float y, float w, float h, int cols, int rows, const byte *data, qboolean dirty );
	void (*R_DrawStretchPic)( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int texnum );
	void (*FillRGBA)( int rendermode, float x, float y, float w, float h, byte r, byte g, byte b, byte a ); // in screen space
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
	void (*R_SetSkyCloudsTextures)( int solidskyTexture, int alphaskyTexture );
	void (*GL_SubdivideSurface)( model_t *mod, msurface_t *fa );
	void (*CL_RunLightStyles)( lightstyle_t *ls );

	// sprites
	void (*R_GetSpriteParms)( int *frameWidth, int *frameHeight, int *numFrames, int currentFrame, const model_t *pSprite );
	int (*R_GetSpriteTexture)( const model_t *m_pSpriteModel, int frame );

	// model management
	// flags ignored for everything except spritemodels
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
	void	(*R_OverrideTextureSourceSize)( unsigned int texnum, unsigned int srcWidth, unsigned int srcHeight ); // used to override decal size for texture replacement

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
	void	(*VGUI_SetupDrawing)( qboolean rect );
	void	(*VGUI_UploadTextureBlock)( int drawX, int drawY, const byte *rgba, int blockWidth, int blockHeight );
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
	ENGINE_SHARED_CVAR_NAME( f, v_lightgamma, lightgamma ) \
	ENGINE_SHARED_CVAR_NAME( f, v_direct, direct ) \
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
	ENGINE_SHARED_CVAR( f, host_allow_materials ) \

#define DECLARE_ENGINE_SHARED_CVAR_LIST() \
	ENGINE_SHARED_CVAR_LIST( DECLARE_ENGINE_SHARED_CVAR )

#define DEFINE_ENGINE_SHARED_CVAR_LIST() \
	ENGINE_SHARED_CVAR_LIST( DEFINE_ENGINE_SHARED_CVAR )

#define RETRIEVE_ENGINE_SHARED_CVAR_LIST() \
	ENGINE_SHARED_CVAR_LIST( RETRIEVE_ENGINE_SHARED_CVAR )
#endif

#endif // REF_API
