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
#include "cvar.h"
#include "com_image.h"

#define REF_API_VERSION 1


#define TF_SKY		(TF_SKYSIDE|TF_NOMIPMAP)
#define TF_FONT		(TF_NOMIPMAP|TF_CLAMP)
#define TF_IMAGE		(TF_NOMIPMAP|TF_CLAMP)
#define TF_DECAL		(TF_CLAMP)


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

typedef struct
{
	msurface_t	*surf;
	int		cull;
} sortedface_t;

typedef struct ref_globals_s
{
	qboolean developer;
	qboolean video_prepped;

	float time;    // cl.time
	float oldtime; // cl.oldtime
	double realtime; // host.realtime
	double frametime; // host.frametime

	int parsecount; // cl.parsecount
	int parsecountmod; // cl.parsecountmod

	// viewport width and height
	int      width;
	int      height;
	qboolean fullScreen;
	qboolean wideScreen;

	vec3_t vieworg;
	vec3_t viewangles;
	vec3_t vforward, vright, vup;

	cl_entity_t	*currententity;
	model_t		*currentmodel;

	float fov_x, fov_y;

	// todo: fill this without engine help
	// move to local

	// translucent sorted array
	sortedface_t	*draw_surfaces;	// used for sorting translucent surfaces
	int		max_surfaces;	// max surfaces per submodel (for all models)
	size_t		visbytes;		// cluster size
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

enum ref_shared_texture_e
{
	REF_DEFAULT_TEXTURE,
	REF_GRAY_TEXTURE,
	REF_WHITE_TEXTURE,
	REF_SOLIDSKY_TEXTURE,
	REF_ALPHASKY_TEXTURE,
};

typedef enum ref_connstate_e
{
	ref_ca_disconnected = 0,// not talking to a server
	ref_ca_connecting,	// sending request packets to the server
	ref_ca_connected,	// netchan_t established, waiting for svc_serverdata
	ref_ca_validate,	// download resources, validating, auth on server
	ref_ca_active,	// game views should be displayed
	ref_ca_cinematic,	// playing a cinematic, not connected to a server
} ref_connstate_t;

enum ref_defaultsprite_e
{
	REF_DOT_SPRITE, // cl_sprite_dot
	REF_CHROME_SPRITE // cl_sprite_shell
};

struct con_nprint_s;
struct remap_info_s;

typedef struct ref_api_s
{
	qboolean (*CL_IsDevOverviewMode)( void );
	qboolean (*CL_IsThirdPersonMode)( void );
	qboolean (*Host_IsQuakeCompatible)( void );
	int (*GetPlayerIndex)( void ); // cl.playernum + 1
	int (*GetViewEntIndex)( void ); // cl.viewentity
	ref_connstate_t (*CL_GetConnState)( void ); // cls.state == ca_connected
	int (*IsDemoPlaying)( void ); // cls.demoplayback
	int (*GetWaterLevel)( void ); // cl.local.waterlevel
	int	(*CL_GetRenderParm)( int parm, int arg );	// generic	int	(*GetMaxClients)( void );
	int	(*GetMaxClients)( void ); // cl.maxclients
	int (*GetLocalHealth)( void ); // cl.local.health
	qboolean (*Host_IsLocalGame)( void );

	// cvar handlers
	convar_t   *(*Cvar_Get)( const char *szName, const char *szValue, int flags, const char *description );
	convar_t   *(*pfnGetCvarPointer)( const char *name );
	float       (*pfnGetCvarFloat)( const char *szName );
	const char *(*pfnGetCvarString)( const char *szName );
	void        (*Cvar_SetValue)( const char *name, float value );
	void (*Cvar_RegisterVariable)( convar_t *var );
	void (*Cvar_FullSet)( const char *var_name, const char *value, int flags );

	// command handlers
	int         (*Cmd_AddCommand)( const char *cmd_name, void (*function)(void), const char *description );
	int         (*Cmd_RemoveCommand)( const char *cmd_name );
	int         (*Cmd_Argc)( void );
	const char *(*Cmd_Argv)( int arg );

	// cbuf
	void (*Cbuf_AddText)( const char *commands );
	void (*Cbuf_InsertText)( const char *commands );
	void (*Cbuf_Execute)( void );
	void (*Cbuf_SetOpenGLConfigHack)( qboolean set ); // host.apply_opengl_config

	// logging
	void	(*Con_VPrintf)( const char *fmt, va_list args );
	void	(*Con_Printf)( const char *fmt, ... ); // typical console allowed messages
	void	(*Con_DPrintf)( const char *fmt, ... ); // -dev 1
	void	(*Con_Reportf)( const char *fmt, ... ); // -dev 2

	// debug print
	void	(*Con_NPrintf)( int pos, const char *fmt, ... );
	void	(*Con_NXPrintf)( struct con_nprint_s *info, const char *fmt, ... );
	void	(*CL_CenterPrint)( const char *fmt, ... );
	void (*Con_DrawStringLen)( const char *pText, int *length, int *height );
	int (*Con_DrawString)( int x, int y, const char *string, rgba_t setColor );
	void	(*CL_DrawCenterPrint)();

	// entity management
	struct cl_entity_s *(*GetLocalPlayer)( void );
	struct cl_entity_s *(*GetViewModel)( void );
	struct cl_entity_s *(*GetEntityByIndex)( int idx );
	int (*pfnNumberOfEntities)( void );
	struct cl_entity_s *(*R_BeamGetEntity)( int index );
	struct cl_entity_s *(*CL_GetWaterEntity)( vec3_t p );
	qboolean (*CL_AddVisibleEntity)( cl_entity_t *ent, int entityType );

	// brushes
	int (*Mod_SampleSizeForFace)( struct msurface_s *surf );
	qboolean (*Mod_BoxVisible)( const vec3_t mins, const vec3_t maxs, const byte *visbits );
	struct world_static_s *(*GetWorld)( void ); // returns &world
	mleaf_t *(*Mod_PointInLeaf)( const vec3_t p, mnode_t *node );
	void (*Mod_CreatePolygonsForHull)( int hullnum );

	// studio models
	void (*R_StudioSlerpBones)( int numbones, vec4_t q1[], float pos1[][3], vec4_t q2[], float pos2[][3], float s );
	void (*R_StudioCalcBoneQuaternion)( int frame, float s, mstudiobone_t *pbone, mstudioanim_t *panim, float *adj, vec4_t q );
	void (*R_StudioCalcBonePosition)( int frame, float s, mstudiobone_t *pbone, mstudioanim_t *panim, vec3_t adj, vec3_t pos );
	void *(*R_StudioGetAnim)( studiohdr_t *m_pStudioHeader, model_t *m_pSubModel, mstudioseqdesc_t *pseqdesc );
	void	(*pfnStudioEvent)( const struct mstudioevent_s *event, const cl_entity_t *entity );

	// efx
	void (*CL_DrawEFX)( float time, qboolean fTrans );
	void (*CL_ThinkParticle)( double frametime, particle_t *p );
	void (*R_FreeDeadParticles)( particle_t **ppparticles );
	particle_t *(*CL_AllocParticleFast)( void ); // unconditionally give new particle pointer from cl_free_particles
	efrag_t* (*GetEfragsFreeList)( void ); // clgame.free_efrags
	void (*SetEfragsFreeList)( efrag_t* ); // clgame.free_efrags
	color24 *(*GetTracerColors)( int num );
	struct dlight_s *(*CL_AllocElight)( int key );
	struct model_s *(*GetDefaultSprite)( enum ref_defaultsprite_e spr );

	// model management
	model_t *(*Mod_ForName)( const char *name, qboolean crash, qboolean trackCRC );
	void *(*Mod_Extradata)( int type, model_t *model );
	struct model_s *(*pfnGetModelByIndex)( int index ); // CL_ModelHandle
	struct model_s *(*Mod_GetCurrentLoadingModel)( void ); // loadmodel
	void (*Mod_SetCurrentLoadingModel)( struct model_s* ); // loadmodel
	int (*CL_NumModels)( void );

	// remap
	struct remap_info_s *(*CL_GetRemapInfoForEntity)( cl_entity_t *e );
	void (*CL_AllocRemapInfo)( int topcolor, int bottomcolor );
	void (*CL_FreeRemapInfo)( struct remap_info_s *info );
	void (*CL_UpdateRemapInfo)( int topcolor, int bottomcolor );

	// utils
	void  (*CL_ExtraUpdate)( void );
	uint  (*COM_HashKey)( const char *strings, uint hashSize );
	void  (*Host_Error)( const char *fmt, ... );
	int   (*CL_FxBlend)( cl_entity_t *e );
	void  (*COM_SetRandomSeed)( int lSeed );
	float (*COM_RandomFloat)( float rmin, float rmax );
	int   (*COM_RandomLong)( int rmin, int rmax );
	struct screenfade_s *(*GetScreenFade)( void );
	struct client_textmessage_s *(*pfnTextMessageGet)( const char *pName );
	void (*GetPredictedOrigin)( vec3_t v );
	byte *(*CL_GetPaletteColor)(int color); // clgame.palette[color]
	void (*CL_GetScreenInfo)( int *width, int *height ); // clgame.scrInfo, ptrs may be NULL
	void (*SetLocalLightLevel)( int level ); // cl.local.light_level

	// studio interface
	player_info_t *(*pfnPlayerInfo)( int index );
	entity_state_t *(*pfnGetPlayerState)( int index );
	void *(*Mod_CacheCheck)( struct cache_user_s *c );
	void (*Mod_LoadCacheFile)( const char *path, struct cache_user_s *cu );
	void *(*Mod_Calloc)( int number, size_t size );
	int	(*pfnGetStudioModelInterface)( int version, struct r_studio_interface_s **ppinterface, struct engine_studio_api_s *pstudio );

	// memory
	byte *(*_Mem_AllocPool)( const char *name, const char *filename, int fileline );
	void  (*_Mem_FreePool)( byte **poolptr, const char *filename, int fileline );
	void *(*_Mem_Alloc)( byte *poolptr, size_t size, qboolean clear, const char *filename, int fileline );
	void *(*_Mem_Realloc)( byte *poolptr, void *memptr, size_t size, qboolean clear, const char *filename, int fileline );
	void  (*_Mem_Free)( void *data, const char *filename, int fileline );

	// library management
	void *(*COM_LoadLibrary)( const char *name );
	void  (*COM_FreeLibrary)( void *handle );
	void *(*COM_GetProcAddress)( void *handle, const char *name );

	// filesystem
	byte*	(*COM_LoadFile)( const char *path, fs_offset_t *pLength, qboolean gamedironly );
	char*	(*COM_ParseFile)( char *data, char *token );
	// use Mem_Free instead
	// void	(*COM_FreeFile)( void *buffer );
	int (*FS_FileExists)( const char *filename, int gamedironly );
	void (*FS_AllowDirectPaths)( qboolean enable );

	// GL
	int   (*GL_SetAttribute)( int attr, int value );
	int   (*GL_GetAttribute)( int attr );
	int   (*GL_CreateContext)( void ); // TODO
	void  (*GL_DestroyContext)( );
	void *(*GL_GetProcAddress)( const char *name );

	// gamma
	void (*BuildGammaTable)( float lightgamma, float brightness );
	byte		(*LightToTexGamma)( byte color );	// software gamma support

	// renderapi
	lightstyle_t*	(*GetLightStyle)( int number );
	dlight_t*	(*GetDynamicLight)( int number );
	dlight_t*	(*GetEntityLight)( int number );
	int		(*R_FatPVS)( const float *org, float radius, byte *visbuffer, qboolean merge, qboolean fullvis );
	void		*(*AVI_LoadVideo)( const char *filename, qboolean load_audio );
	int		(*AVI_GetVideoInfo)( void *Avi, long *xres, long *yres, float *duration );
	long		(*AVI_GetVideoFrameNumber)( void *Avi, float time );
	byte		*(*AVI_GetVideoFrame)( void *Avi, long frame );
	void		(*AVI_FreeVideo)( void *Avi );
	int		(*AVI_IsActive)( void *Avi );
	void		(*AVI_StreamSound)( void *Avi, int entnum, float fvol, float attn, float synctime );
	int		(*SPR_LoadExt)( const char *szPicName, unsigned int texFlags ); // extended version of SPR_Load
	const struct ref_overview_s *( *GetOverviewParms )( void );
	const char	*( *GetFileByIndex )( int fileindex );
	void		*(*pfnMemAlloc)( size_t cb, const char *filename, const int fileline );
	void		(*pfnMemFree)( void *mem, const char *filename, const int fileline );
	char		**(*pfnGetFilesList)( const char *pattern, int *numFiles, int gamedironly );
	unsigned int	(*pfnFileBufferCRC32)( const void *buffer, const int length );
	int		(*COM_CompareFileTime)( const char *filename1, const char *filename2, int *iCompare );
	void*		( *pfnGetModel )( int modelindex );
	float		(*pfnTime)( void );				// Sys_DoubleTime
	void		(*Cvar_Set)( const char *name, const char *value );
	void		(*S_FadeMusicVolume)( float fadePercent );	// fade background track (0-100 percents)
	void		(*SetRandomSeed)( long lSeed );		// set custom seed for RANDOM_FLOAT\RANDOM_LONG for predictable random

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
	qboolean (*Image_Process)( rgbdata_t **pix, int width, int height, uint flags, float bumpscale );
	rgbdata_t *(*FS_LoadImage)( const char *filename, const byte *buffer, size_t size );
	qboolean (*FS_SaveImage)( const char *filename, rgbdata_t *pix );
	rgbdata_t *(*FS_CopyImage)( rgbdata_t *in );
	void (*FS_FreeImage)( rgbdata_t *pack );
	void (*Image_SetMDLPointer)( byte *p );
	byte *(*Image_GetPool)( void );
	struct bpc_desc_s *(*Image_GetPFDesc)( int idx );

	// client exports
	void	(*pfnDrawNormalTriangles)( void );
	void	(*pfnDrawTransparentTriangles)( void );
	render_interface_t	drawFuncs;
} ref_api_t;

struct mip_s;

// render callbacks
typedef struct ref_interface_s
{
	// construct, destruct
	qboolean (*R_Init)( qboolean context ); // context is true if you need context management
	const char *(*R_GetInitError)( void );
	void (*R_Shutdown)( void );

	//
	void (*GL_InitExtensions)( void );
	void (*GL_ClearExtensions)( void );


	void (*R_BeginFrame)( qboolean clearScene );
	void (*R_RenderScene)( void );
	// void (*R_RenderFrame)( struct ref_viewpass_s *rvp ); part of RenderInterface
	void (*R_EndFrame)( void );
	void (*R_PushScene)( void );
	void (*R_PopScene)( void );
	// void (*R_ClearScene)( void ); part of RenderInterface
	void (*GL_BackendStartFrame)( void );
	void (*GL_BackendEndFrame)( void );

	void (*R_ClearScreen)( void ); // clears color buffer on GL
	void (*R_AllowFog)( qboolean allow );
	void (*GL_SetRenderMode)( int renderMode );

	int (*R_AddEntity)( int entityType, cl_entity_t *ent );
	void (*CL_AddCustomBeam)( cl_entity_t *pEnvBeam );

	// view info
	qboolean (*IsNormalPass)( void );

	// debug
	void (*R_ShowTextures)( void );
	void (*R_ShowTree)( void );
	void (*R_IncrementSpeedsCounter)( int counterType );

	// texture management
	const byte *(*R_GetTextureOriginalBuffer)( unsigned int idx ); // not always available
	int (*GL_LoadTextureFromBuffer)( const char *name, rgbdata_t *pic, texFlags_t flags, qboolean update );
	int (*R_GetBuiltinTexture)( enum ref_shared_texture_e type );
	void (*R_FreeSharedTexture)( enum ref_shared_texture_e type );
	void (*GL_ProcessTexture)( int texnum, float gamma, int topColor, int bottomColor );
	void (*R_SetupSky)( const char *skyname );


	// 2D
	void (*R_Set2DMode)( qboolean enable );
	void (*R_DrawStretchRaw)( float x, float y, float w, float h, int cols, int rows, const byte *data, qboolean dirty );
	void (*R_DrawStretchPic)( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int texnum );
	void (*R_DrawTileClear)( int texnum, int x, int y, int w, int h );
	void (*FillRGBA)( float x, float y, float w, float h, int r, int g, int b, int a ); // in screen space
	void (*FillRGBABlend)( float x, float y, float w, float h, int r, int g, int b, int a ); // in screen space

	// screenshot, cubemapshot
	qboolean (*VID_ScreenShot)( const char *filename, int shot_type );
	qboolean (*VID_CubemapShot)( const char *base, uint size, const float *vieworg, qboolean skyshot );

	// light
	colorVec (*R_LightPoint)( const float *p );

	void (*R_AddEfrags)( struct cl_entity_s *ent );
	void (*R_RemoveEfrags)( struct cl_entity_s *ent );

	// decals
	// Shoots a decal onto the surface of the BSP.  position is the center of the decal in world coords
	void (*R_DecalShoot)( int textureIndex, int entityIndex, int modelIndex, vec3_t pos, int flags, float scale );
	void (*R_DecalRemoveAll)( int texture );
	int (*R_CreateDecalList)( struct decallist_s *pList );
	void (*R_ClearAllDecals)( void );

	// studio interface
	float (*R_StudioEstimateFrame)( cl_entity_t *e, mstudioseqdesc_t *pseqdesc );
	void (*R_StudioLerpMovement)( cl_entity_t *e, double time, vec3_t origin, vec3_t angles );
	void (*CL_InitStudioAPI)( void );

	// bmodel
	void (*R_InitSkyClouds)( struct mip_s *mt, struct texture_s *tx, qboolean custom_palette );
	void (*GL_SubdivideSurface)( msurface_t *fa );
	void (*CL_RunLightStyles)( void );

	// sprites
	void (*R_GetSpriteParms)( int *frameWidth, int *frameHeight, int *numFrames, int currentFrame, const model_t *pSprite );

	// model management
	// flags ignored for everything except spritemodels
	void (*Mod_LoadModel)( modtype_t desiredType, model_t *mod, const byte *buf, qboolean *loaded, int flags );
	void (*Mod_LoadMapSprite)( struct model_s *mod, const void *buffer, size_t size, qboolean *loaded );
	void (*Mod_UnloadModel)( model_t *mod );
	void (*Mod_StudioLoadTextures)( model_t *mod, void *data );
	void (*Mod_StudioUnloadTextures)( void *data );

	// particle renderer
	void (*CL_Particle)( const vec3_t origin, int color, float life, int zpos, int zvel ); // debug thing

	// efx implementation
	void (*CL_DrawParticles)( double frametime, particle_t *particles, float partsize );
	void (*CL_DrawTracers)( double frametime, particle_t *tracers );
	void (*CL_DrawBeams)( int fTrans , BEAM *beams );
	qboolean (*R_BeamCull)( const vec3_t start, const vec3_t end, qboolean pvsOnly );

	// Xash3D Render Interface
	render_api_t *RenderAPI;         // partial RenderAPI implementation
	render_interface_t *RenderIface; // compatible RenderInterface implementation: renderer should call client RenderInterface by itself

	// TriAPI Interface
	// NOTE: implementation isn't required to be compatible
	void	(*TriRenderMode)( int mode );
	void	(*Begin)( int primitiveCode );
	void	(*End)( void );
	void	(*Color4f)( float r, float g, float b, float a );
	void	(*Color4ub)( unsigned char r, unsigned char g, unsigned char b, unsigned char a );
	void	(*TexCoord2f)( float u, float v );
	void	(*Vertex3fv)( const float *worldPnt );
	void	(*Vertex3f)( float x, float y, float z );
	int	(*SpriteTexture)( struct model_s *pSpriteModel, int frame );
	int	(*WorldToScreen)( const float *world, float *screen );  // Returns 1 if it's z clipped
	void	(*Fog)( float flFogColor[3], float flStart, float flEnd, int bOn ); //Works just like GL_FOG, flFogColor is r/g/b.
	void	(*ScreenToWorld)( const float *screen, float *world  );
	void	(*GetMatrix)( const int pname, float *matrix );
	void	(*FogParams)( float flDensity, int iFogSkybox );
	void    (*CullFace)( TRICULLSTYLE mode );

	// vgui drawing implementation
	vguiapi_t *VGuiAPI;

	// efx api
	efx_api_t *EfxAPI;
} ref_interface_t;

typedef int (*REFAPI)( int version, ref_interface_t *pFunctionTable, ref_api_t* engfuncs, ref_globals_t *pGlobals );

#endif // REF_API
