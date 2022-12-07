/*
gu_local.h - renderer local declarations
Copyright (C) 2010 Uncle Mike
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

#ifndef GL_LOCAL_H
#define GL_LOCAL_H

#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>

#include "port.h"
#include "xash3d_types.h"
#include "cvardef.h"
#include "const.h"
#include "com_model.h"
#include "cl_entity.h"
#include "render_api.h"
#include "protocol.h"
#include "dlight.h"
#include "gu_frustum.h"
#include "ref_api.h"
#include "xash3d_mathlib.h"
#include "ref_params.h"
#include "enginefeatures.h"
#include "com_strings.h"
#include "pm_movevars.h"
//#include "cvar.h"
#include "gu_helper.h"
#include "gu_extension.h"
#include "gu_vram.h"
#include "wadfile.h"

#ifndef offsetof
#define offsetof(s,m)       (size_t)&(((s *)0)->m)
#endif // offsetof

#define ASSERT(x) if(!( x )) gEngfuncs.Host_Error( "assert failed at %s:%i\n", __FILE__, __LINE__ )
#define Assert(x) if(!( x )) gEngfuncs.Host_Error( "assert failed at %s:%i\n", __FILE__, __LINE__ )

#include <stdio.h>

#define CVAR_DEFINE( cv, cvname, cvstr, cvflags, cvdesc )	cvar_t cv = { cvname, cvstr, cvflags, 0.0f, (void *)CVAR_SENTINEL, cvdesc }
#define CVAR_DEFINE_AUTO( cv, cvstr, cvflags, cvdesc )	cvar_t cv = { #cv, cvstr, cvflags, 0.0f, (void *)CVAR_SENTINEL, cvdesc }
#define CVAR_TO_BOOL( x )		((x) && ((x)->value != 0.0f) ? true : false )

#define WORLD (gEngfuncs.GetWorld())
#define WORLDMODEL (gEngfuncs.pfnGetModelByIndex( 1 ))
#define MOVEVARS (gEngfuncs.pfnGetMoveVars())

// make mod_ref.h?
#define LM_SAMPLE_SIZE             16


extern byte	*r_temppool;

#define LIGHTMAP_BPP	1 //1 2 3 4

#if LIGHTMAP_BPP == 1
#define LIGHTMAP_FORMAT	PF_RGB_332
#elif LIGHTMAP_BPP == 2
#define LIGHTMAP_FORMAT	PF_RGB_5650
#elif LIGHTMAP_BPP == 3
#define LIGHTMAP_FORMAT	PF_RGB_24
#elif LIGHTMAP_BPP == 4
#define LIGHTMAP_FORMAT	PF_RGBA_32
#else
#error (1 > LIGHTMAP_BPP > 4)
#endif

#if 1

#define BLOCK_SIZE		tr.block_size	// lightmap blocksize
#define BLOCK_SIZE_DEFAULT	128		// for keep backward compatibility
#define BLOCK_SIZE_MAX	128

#define MAX_TEXTURES	1536
#define MAX_DETAIL_TEXTURES	64
#define MAX_LIGHTMAPS	64
#define SUBDIVIDE_SIZE	64
#define MAX_DECAL_SURFS	256
#define MAX_DRAW_STACK	2		// normal view and menu view

#else
	
#define BLOCK_SIZE		tr.block_size	// lightmap blocksize
#define BLOCK_SIZE_DEFAULT	128		// for keep backward compatibility
#define BLOCK_SIZE_MAX	1024

#define MAX_TEXTURES	4096
#define MAX_DETAIL_TEXTURES	256
#define MAX_LIGHTMAPS	256
#define SUBDIVIDE_SIZE	64
#define MAX_DECAL_SURFS	4096
#define MAX_DRAW_STACK	2		// normal view and menu view

#endif

#define SHADEDOT_QUANT 	16		// precalculated dot products for quantized angles
#define SHADE_LAMBERT	1.495f

#if 1
#define DEFAULT_ALPHATEST	0x00
#else
#define DEFAULT_ALPHATEST	0.0f
#endif

// refparams
#define RP_NONE		0
#define RP_ENVVIEW		BIT( 0 )	// used for cubemapshot
#define RP_OLDVIEWLEAF	BIT( 1 )
#define RP_CLIPPLANE	BIT( 2 )

#define RP_NONVIEWERREF	(RP_ENVVIEW)
#define R_ModelOpaque( rm )	( rm == kRenderNormal )
#define R_StaticEntity( ent )	( VectorIsNull( ent->origin ) && VectorIsNull( ent->angles ))
#define RP_LOCALCLIENT( e )	((e) != NULL && (e)->index == ENGINE_GET_PARM( PARM_PLAYER_INDEX ) && e->player )
#define RP_NORMALPASS()	( FBitSet( RI.params, RP_NONVIEWERREF ) == 0 )

#define CL_IsViewEntityLocalPlayer() ( ENGINE_GET_PARM( PARM_VIEWENT_INDEX ) == ENGINE_GET_PARM( PARM_PLAYER_INDEX ) )

#define CULL_VISIBLE	0		// not culled
#define CULL_BACKSIDE	1		// backside of transparent wall
#define CULL_FRUSTUM	2		// culled by frustum
#define CULL_VISFRAME	3		// culled by PVS
#define CULL_OTHER		4		// culled by other reason

typedef struct gltexture_s
{

	char		name[128];	// game path, including extension (can be store image programs)
	short		srcWidth;		// keep unscaled sizes
	short		srcHeight;
	short		width;		// upload width\height
	short		height;
	//short		depth;		// texture depth or count of layers for 2D_ARRAY
	byte		numMips;		// mipmap count
	byte		*dstTexture;	// texture pointer
	byte		*dstPalette;
	byte		bpp;
	int			format;		// uploaded format
	//GLint		encode;		// using GLSL decoder
	texFlags_t	flags;

	rgba_t		fogParams;	// some water textures
							// contain info about underwater fog

	rgbdata_t	*original;	// keep original image

	// debug info
	size_t		size;		// upload size for debug targets

	// detail textures stuff
	float		xscale;
	float		yscale;
#if 0
	int			servercount;
#endif
	uint		hashValue;
	struct gltexture_s	*nextHash;
} gl_texture_t;

#if 1
typedef struct 
{
	float x, y, z;
}gu_vert_fv_t;

typedef struct 
{
	unsigned int c;
	float x, y, z;
}gu_vert_fcv_t;

typedef struct 
{
	float u, v;
	float x, y, z;
}gu_vert_ftv_t;

typedef struct 
{
	float u, v;
	unsigned int c;
	float x, y, z;
}gu_vert_ftcv_t;

typedef struct
{
	float u, v;
	unsigned int c;
	float nx, ny, nz;
	float x, y, z;
}gu_vert_ftcnv_t;

typedef struct 
{
	short x, y, z;
}gu_vert_hv_t;

typedef struct 
{
	short u, v;
	short x, y, z;
}gu_vert_htv_t;

typedef struct 
{
	short u, v;
	unsigned int c;
	short x, y, z;
}gu_vert_htcv_t;
#endif

typedef struct
{
	int		params;		// rendering parameters

	qboolean		drawWorld;	// ignore world for drawing PlayerModel
	qboolean		isSkyVisible;	// sky is visible
	qboolean		onlyClientDraw;	// disabled by client request
	qboolean		drawOrtho;	// draw world as orthogonal projection	

	float		fov_x, fov_y;	// current view fov

	cl_entity_t	*currententity;
	model_t		*currentmodel;
	cl_entity_t	*currentbeam;	// same as above but for beams

	int		viewport[4];
	gl_frustum_t	frustum;

	mleaf_t		*viewleaf;
	mleaf_t		*oldviewleaf;
	vec3_t		pvsorigin;
	vec3_t		vieworg;		// locked vieworigin
	vec3_t		viewangles;
	vec3_t		vforward;
	vec3_t		vright;
	vec3_t		vup;

	vec3_t		cullorigin;
	vec3_t		cull_vforward;
	vec3_t		cull_vright;
	vec3_t		cull_vup;

	float		farClip;

	qboolean		fogCustom;
	qboolean		fogEnabled;
	qboolean		fogSkybox;
	vec4_t		fogColor;
	float		fogDensity;
	float		fogStart;
	float		fogEnd;
	int		cached_contents;	// in water
	int		cached_waterlevel;	// was in water

	float		skyMins[2][6];
	float		skyMaxs[2][6];

	matrix4x4		objectMatrix;		// currententity matrix
	matrix4x4		worldviewMatrix;		// modelview for world
	matrix4x4		modelviewMatrix;		// worldviewMatrix * objectMatrix

	matrix4x4		projectionMatrix;
	matrix4x4		worldviewProjectionMatrix;	// worldviewMatrix * projectionMatrix
	byte		visbytes[(MAX_MAP_LEAFS+7)/8];// actual PVS for current frame

	float		viewplanedist;
	mplane_t		clipPlane;
} ref_instance_t;

typedef struct
{
	cl_entity_t	*solid_entities[MAX_VISIBLE_PACKET];	// opaque moving or alpha brushes
	cl_entity_t	*trans_entities[MAX_VISIBLE_PACKET];	// translucent brushes
	cl_entity_t	*beam_entities[MAX_VISIBLE_PACKET];
	uint		num_solid_entities;
	uint		num_trans_entities;
	uint		num_beam_entities;
} draw_list_t;

typedef struct
{
	int		defaultTexture;   	// use for bad textures
	int		particleTexture;
	int		whiteTexture;
	int		grayTexture;
	int		blackTexture;
	int		solidskyTexture;	// quake1 solid-sky layer
	int		alphaskyTexture;	// quake1 alpha-sky layer
	int		lightmapTextures[MAX_LIGHTMAPS];
	int		dlightTexture;	// custom dlight texture
	int		skyboxTextures[6];	// skybox sides
	int		cinTexture;      	// cinematic texture

	int		skytexturenum;	// this not a gl_texturenum!
#if 0
	int		skyboxbasenum;	// start with 5800
#endif
	// entity lists
	draw_list_t	draw_stack[MAX_DRAW_STACK];
	int		draw_stack_pos;
	draw_list_t	*draw_list;

	msurface_t	*draw_decals[MAX_DECAL_SURFS];
	int		num_draw_decals;

	// OpenGL matrix states
	qboolean		modelviewIdentity;

	int		visframecount;	// PVS frame
	int		dlightframecount;	// dynamic light frame
	int		realframecount;	// not including viewpasses
	int		framecount;

	qboolean		ignore_lightgamma;
	qboolean		fCustomRendering;
	qboolean		fResetVis;
	qboolean		fFlipViewModel;

	byte		visbytes[(MAX_MAP_LEAFS+7)/8];	// member custom PVS
	int		lightstylevalue[MAX_LIGHTSTYLES];	// value 0 - 65536
	int		block_size;			// lightmap blocksize

	double		frametime;	// special frametime for multipass rendering (will set to 0 on a nextview)
	float		blend;		// global blend value

	// cull info
	vec3_t		modelorg;		// relative to viewpoint

	qboolean fCustomSkybox;
} gl_globals_t;

typedef struct
{
	uint		c_world_polys;
	uint		c_studio_polys;
	uint		c_sprite_polys;
	uint		c_alias_polys;
	uint		c_world_leafs;

	uint		c_view_beams_count;
	uint		c_active_tents_count;
	uint		c_alias_models_drawn;
	uint		c_studio_models_drawn;
	uint		c_sprite_models_drawn;
	uint		c_particle_count;

	uint		c_client_ents;	// entities that moved to client
	double		t_world_node;
	double		t_world_draw;
} ref_speeds_t;

extern ref_speeds_t		r_stats;
extern ref_instance_t	RI;
extern gl_globals_t	tr;

extern float		gldepthmin, gldepthmax;
#define r_numEntities	(tr.draw_list->num_solid_entities + tr.draw_list->num_trans_entities)
#define r_numStatics	(r_stats.c_client_ents)

//
// gu_backend.c
//
void GL_BackendStartFrame( void );
void GL_BackendEndFrame( void );
void GL_CleanUpTextureUnits( int last );
void GL_Bind( GLint tmu, GLenum texnum );
void GL_MultiTexCoord2f( GLenum texture, GLfloat s, GLfloat t );
void GL_SetTexCoordArrayMode( GLenum mode );
void GL_LoadTexMatrix( const matrix4x4 m );
void GL_LoadTexMatrixExt( const float *glmatrix );
void GL_LoadMatrix( const matrix4x4 source );
void GL_TexGen( GLenum coord, GLenum mode );
void GL_SelectTexture( GLint texture );
void GL_CleanupAllTextureUnits( void );
void GL_LoadIdentityTexMatrix( void );
void GL_DisableAllTexGens( void );
void GL_SetRenderMode( int mode );
void GL_TextureTarget( uint target );
void GL_Cull( GLenum cull );
void R_ShowTextures( void );
void SCR_TimeRefresh_f( void );

//
// gu_beams.c
//
void CL_DrawBeams( int fTrans, BEAM *active_beams );
qboolean R_BeamCull( const vec3_t start, const vec3_t end, qboolean pvsOnly );

//
// gu_cull.c
//
int R_CullModel( cl_entity_t *e, const vec3_t absmin, const vec3_t absmax );
qboolean R_CullBox( const vec3_t mins, const vec3_t maxs );
qboolean R_CullSphere( const vec3_t centre, const float radius );
int R_CullSurface( msurface_t *surf, gl_frustum_t *frustum, uint clipflags );

//
// gu_decals.c
//
void DrawSurfaceDecals( msurface_t *fa, qboolean single, qboolean reverse );
float *R_DecalSetupVerts( decal_t *pDecal, msurface_t *surf, int texture, int *outCount );
void DrawSingleDecal( decal_t *pDecal, msurface_t *fa );
void R_EntityRemoveDecals( model_t *mod );
void DrawDecalsBatch( void );
void R_ClearDecals( void );

//
// gu_draw.c
//
void R_Set2DMode( qboolean enable );
void R_DrawTileClear( int texnum, int x, int y, int w, int h );
void R_UploadStretchRaw( int texture, int cols, int rows, int width, int height, const byte *data );

//
// gu_drawhulls.c
//
void R_DrawWorldHull( void );
void R_DrawModelHull( void );

//
// gu_image.c
//
gl_texture_t *R_GetTexture( GLenum texnum );
#define GL_LoadTextureInternal( name, pic, flags ) GL_LoadTextureFromBuffer( name, pic, flags, false )
#define GL_UpdateTextureInternal( name, pic, flags ) GL_LoadTextureFromBuffer( name, pic, flags, true )
int GL_LoadTexture( const char *name, const byte *buf, size_t size, int flags );
int GL_LoadTextureArray( const char **names, int flags );
int GL_LoadTextureFromBuffer( const char *name, rgbdata_t *pic, texFlags_t flags, qboolean update );
byte *GL_ResampleTexture( const byte *source, int in_w, int in_h, int out_w, int out_h, qboolean isNormalMap );
#define PC_SWF( X ) ( ( X ) | ( 1 << 15 ) )
#define PC_HWF( X ) ( X )
void GL_PixelConverter( byte *dst, const byte *src, int size, int inFormat, int outFormat );
int GL_CreateTexture( const char *name, int width, int height, const void *buffer, texFlags_t flags );
int GL_CreateTextureArray( const char *name, int width, int height, int depth, const void *buffer, texFlags_t flags );
void GL_ProcessTexture( int texnum, float gamma, int topColor, int bottomColor );
qboolean GL_UpdateTexture( int texnum, int xoff, int yoff, int width, int height, const void *buffer );
void GL_UpdateTexSize( int texnum, int width, int height, int depth );
int GL_FindTexture( const char *name );
void GL_FreeTexture( GLenum texnum );
const char *GL_Target( GLenum target );
void R_InitDlightTexture( void );
void R_TextureList_f( void );
void R_InitImages( void );
void R_ShutdownImages( void );
int GL_TexMemory( void );

//
// gu_rlight.c
//
void CL_RunLightStyles( void );
void R_PushDlights( void );
void R_AnimateLight( void );
void R_GetLightSpot( vec3_t lightspot );
void R_MarkLights( dlight_t *light, int bit, mnode_t *node );
colorVec R_LightVec( const vec3_t start, const vec3_t end, vec3_t lightspot, vec3_t lightvec );
int R_CountSurfaceDlights( msurface_t *surf );
colorVec R_LightPoint( const vec3_t p0 );
int R_CountDlights( void );

//
// gu_rmain.c
//
void R_ClearScene( void );
void R_LoadIdentity( void );
void R_RenderScene( void );
void R_DrawCubemapView( const vec3_t origin, const vec3_t angles, int size );
void R_SetupRefParams( const struct ref_viewpass_s *rvp );
void R_TranslateForEntity( cl_entity_t *e );
void R_RotateForEntity( cl_entity_t *e );
void R_SetupGL( qboolean set_gl_state );
void R_AllowFog( qboolean allowed );
void R_SetupFrustum( void );
void R_FindViewLeaf( void );
void R_CheckGamma( void );
void R_PushScene( void );
void R_PopScene( void );
void R_DrawFog( void );
int CL_FxBlend( cl_entity_t *e );

//
// gu_rmath.c
//
void Matrix4x4_ToFMatrix4( const matrix4x4 in, ScePspFMatrix4 *out );
void Matrix4x4_FromFMatrix4( matrix4x4 out, ScePspFMatrix4 *in );
void Matrix4x4_Concat( matrix4x4 out, const matrix4x4 in1, const matrix4x4 in2 );
void Matrix4x4_ConcatTranslate( matrix4x4 out, float x, float y, float z );
void Matrix4x4_ConcatRotate( matrix4x4 out, float angle, float x, float y, float z );
void Matrix4x4_ConcatScale( matrix4x4 out, float x );
void Matrix4x4_ConcatScale3( matrix4x4 out, float x, float y, float z );
void Matrix4x4_CreateTranslate( matrix4x4 out, float x, float y, float z );
void Matrix4x4_CreateRotate( matrix4x4 out, float angle, float x, float y, float z );
void Matrix4x4_CreateScale( matrix4x4 out, float x );
void Matrix4x4_CreateScale3( matrix4x4 out, float x, float y, float z );
void Matrix4x4_CreateProjection(matrix4x4 out, float xMax, float xMin, float yMax, float yMin, float zNear, float zFar);
void Matrix4x4_CreateOrtho(matrix4x4 m, float xLeft, float xRight, float yBottom, float yTop, float zNear, float zFar);
void Matrix4x4_CreateModelview( matrix4x4 out );

//
// gu_rmisc.c
//
void R_ClearStaticEntities( void );

//
// gu_rsurf.c
//
void R_MarkLeaves( void );
void R_DrawWorld( void );
void R_DrawWaterSurfaces( void );
void R_DrawBrushModel( cl_entity_t *e );
void GL_SubdivideSurface( msurface_t *fa );
void GL_BuildPolygonFromSurface( model_t *mod, msurface_t *fa );
void DrawGLPoly( glpoly_t *p, float xScale, float yScale );
texture_t *R_TextureAnimation( msurface_t *s );
void GL_SetupFogColorForSurfaces( void );
void R_DrawAlphaTextureChains( void );
void GL_RebuildLightmaps( void );
void GL_InitRandomTable( void );
void GL_BuildLightmaps( void );
void GL_ResetFogColor( void );
void R_GenerateVBO( void );
void R_ClearVBO( void );
void R_AddDecalVBO( decal_t *pdecal, msurface_t *surf );

//
// gu_rpart.c
//
void CL_DrawParticlesExternal( const ref_viewpass_t *rvp, qboolean trans_pass, float frametime );
void CL_DrawParticles( double frametime, particle_t *cl_active_particles, float partsize );
void CL_DrawTracers( double frametime, particle_t *cl_active_tracers );


//
// gu_sprite.c
//
void R_SpriteInit( void );
void Mod_LoadSpriteModel( model_t *mod, const void *buffer, qboolean *loaded, uint texFlags );
mspriteframe_t *R_GetSpriteFrame( const model_t *pModel, int frame, float yaw );
void R_DrawSpriteModel( cl_entity_t *e );

//
// gu_studio.c
//
void R_StudioInit( void );
void Mod_LoadStudioModel( model_t *mod, const void *buffer, qboolean *loaded );
void R_StudioLerpMovement( cl_entity_t *e, double time, vec3_t origin, vec3_t angles );
float CL_GetSequenceDuration( cl_entity_t *ent, int sequence );
struct mstudiotex_s *R_StudioGetTexture( cl_entity_t *e );
float CL_GetStudioEstimatedFrame( cl_entity_t *ent );
int R_GetEntityRenderMode( cl_entity_t *ent );
void R_DrawStudioModel( cl_entity_t *e );
player_info_t *pfnPlayerInfo( int index );
void R_GatherPlayerLight( void );
float R_StudioEstimateFrame( cl_entity_t *e, mstudioseqdesc_t *pseqdesc );
void R_StudioLerpMovement( cl_entity_t *e, double time, vec3_t origin, vec3_t angles );
void R_StudioResetPlayerModels( void );
void CL_InitStudioAPI( void );
void Mod_StudioLoadTextures( model_t *mod, void *data );
void Mod_StudioUnloadTextures( void *data );

//
// gu_alias.c
//
void Mod_LoadAliasModel( model_t *mod, const void *buffer, qboolean *loaded );
void R_DrawAliasModel( cl_entity_t *e );
void R_AliasInit( void );

//
// gu_warp.c
//
void R_InitSkyClouds( mip_t *mt, struct texture_s *tx, qboolean custom_palette );
void R_AddSkyBoxSurface( msurface_t *fa );
void R_ClearSkyBox( void );
void R_DrawSkyBox( void );
void R_DrawClouds( void );
void EmitWaterPolys( msurface_t *warp, qboolean reverse );

//
// gu_vgui.c
//
void VGUI_DrawInit( void );
void VGUI_DrawShutdown( void );
void VGUI_SetupDrawingText( int *pColor );
void VGUI_SetupDrawingRect( int *pColor );
void VGUI_SetupDrawingImage( int *pColor );
void VGUI_BindTexture( int id );
void VGUI_EnableTexture( qboolean enable );
void VGUI_CreateTexture( int id, int width, int height );
void VGUI_UploadTexture( int id, const char *buffer, int width, int height );
void VGUI_UploadTextureBlock( int id, int drawX, int drawY, const byte *rgba, int blockWidth, int blockHeight );
void VGUI_DrawQuad( const vpoint_t *ul, const vpoint_t *lr );
void VGUI_GetTextureSizes( int *width, int *height );
int VGUI_GenerateTexture( void );

//#include "vid_common.h"

//
// renderer exports
//
qboolean R_Init( void );
void R_Shutdown( void );
void GL_SetupAttributes( int safegl );
void GL_OnContextCreated( void );
void GL_InitExtensions( void );
void GL_ClearExtensions( void );
void VID_CheckChanges( void );
int GL_LoadTexture( const char *name, const byte *buf, size_t size, int flags );
void GL_FreeImage( const char *name );
qboolean VID_ScreenShot( const char *filename, int shot_type );
qboolean VID_CubemapShot( const char *base, uint size, const float *vieworg, qboolean skyshot );
void R_BeginFrame( qboolean clearScene );
void R_RenderFrame( const struct ref_viewpass_s *vp );
void R_EndFrame( void );
void R_ClearScene( void );
void R_GetTextureParms( int *w, int *h, int texnum );
void R_GetSpriteParms( int *frameWidth, int *frameHeight, int *numFrames, int curFrame, const struct model_s *pSprite );
void R_DrawStretchRaw( float x, float y, float w, float h, int cols, int rows, const byte *data, qboolean dirty );
void R_DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int texnum );
qboolean R_SpeedsMessage( char *out, size_t size );
void R_SetupSky( const char *skyboxname );
qboolean R_CullBox( const vec3_t mins, const vec3_t maxs );
int R_WorldToScreen( const vec3_t point, vec3_t screen );
void R_ScreenToWorld( const vec3_t screen, vec3_t point );
qboolean R_AddEntity( struct cl_entity_s *pRefEntity, int entityType );
void Mod_LoadMapSprite( struct model_s *mod, const void *buffer, size_t size, qboolean *loaded );
void Mod_SpriteUnloadTextures( void *data );
void Mod_UnloadAliasModel( struct model_s *mod );
void Mod_AliasUnloadTextures( void *data );
void GL_SetRenderMode( int mode );
void GL_SetColor4ub( byte r, byte g, byte b, byte a );
void R_RunViewmodelEvents( void );
void R_DrawViewModel( void );
int R_GetSpriteTexture( const struct model_s *m_pSpriteModel, int frame );
void R_DecalShoot( int textureIndex, int entityIndex, int modelIndex, vec3_t pos, int flags, float scale );
void R_RemoveEfrags( struct cl_entity_s *ent );
void R_AddEfrags( struct cl_entity_s *ent );
void R_DecalRemoveAll( int texture );
int R_CreateDecalList( decallist_t *pList );
void R_ClearAllDecals( void );
byte *Mod_GetCurrentVis( void );
void Mod_SetOrthoBounds( const float *mins, const float *maxs );
void R_NewMap( void );
void CL_AddCustomBeam( cl_entity_t *pEnvBeam );

//
// gu_opengl.c
//

//
// gu_triapi.c
//
void TriRenderMode( int mode );
void TriBegin( int mode );
void TriEnd( void );
void TriTexCoord2f( float u, float v );
void TriVertex3fv( const float *v );
void TriVertex3f( float x, float y, float z );
void TriColor4f( float r, float g, float b, float a );
void TriColor4ub( byte r, byte g, byte b, byte a );
void TriBrightness( float brightness );
int TriWorldToScreen( const float *world, float *screen );
int TriSpriteTexture( model_t *pSpriteModel, int frame );
void TriFog( float flFogColor[3], float flStart, float flEnd, int bOn );
void TriGetMatrix( const int pname, float *matrix );
void TriFogParams( float flDensity, int iFogSkybox );
void TriCullFace( TRICULLSTYLE mode );
uint getTriBrightness( float brightness );
int TriBoxInPVS( float *mins, float *maxs );
void TriLightAtPoint( float *pos, float *value );
void TriColor4fRendermode( float r, float g, float b, float a, int rendermode );
int getTriAPI( int version, triangleapi_t *api );

//
// gu_clipping.c
//
#define CLIPPING_DEBUGGING 0
void GU_ClipSetWorldFrustum( const matrix4x4 in );
void GU_ClipRestoreWorldFrustum( void );
void GU_ClipSetModelFrustum( const matrix4x4 in );
void GU_ClipLoadFrustum( const mplane_t *plane ); // Experimental
int GU_ClipIsRequired( gu_vert_t* uv, int uvc );
void GU_Clip( gu_vert_t *uv, int uvc, gu_vert_t **cv, int* cvc );

/*
=======================================================================

 GL STATE MACHINE

=======================================================================
*/
typedef struct
{
	int		max_texture_size;
	qboolean	softwareGammaUpdate;
} glconfig_t;

typedef struct
{

	int		width, height;
	int		activeTMU;
	GLint		currentTexture;
	GLboolean	texIdentityMatrix;
	GLint		isFogEnabled;

	int		faceCull;

	qboolean	stencilEnabled;
	qboolean	in2DMode;

	uint		fogColor;
	float		fogDensity;
	float		fogStart;
	float		fogEnd;
} glstate_t;


typedef struct
{
	qboolean	initialized;	// OpenGL subsystem started
	qboolean	extended;	// extended context allows to GL_Debug
} glwstate_t;

typedef struct
{
	int		screen_width;
	int		screen_height;
	int		buffer_width;
	int		buffer_format;
	int		buffer_bpp;
	void		*draw_buffer;
	void		*disp_buffer;
	void		*depth_buffer;
	void		*context_list;
	size_t		context_list_size;
}gurender_t;

extern glconfig_t	glConfig;
extern glstate_t	glState;
extern gurender_t	guRender;
// move to engine
extern glwstate_t	glw_state;
extern ref_api_t	gEngfuncs;
extern ref_globals_t	*gpGlobals;

#define ENGINE_GET_PARM_ (*gEngfuncs.EngineGetParm)
#define ENGINE_GET_PARM( parm ) ENGINE_GET_PARM_( ( parm ), 0 )

//
// renderer cvars
//
extern cvar_t	*gl_texture_lodfunc;
extern cvar_t	*gl_texture_lodbias;
extern cvar_t	*gl_texture_lodslope;
extern cvar_t	*gl_texture_nearest;
extern cvar_t	*gl_lightmap_nearest;
extern cvar_t	*gl_keeptjunctions;
extern cvar_t	*gl_emboss_scale;
extern cvar_t	*gl_round_down;
extern cvar_t	*gl_detailscale;
extern cvar_t	*gl_wireframe;
extern cvar_t	*gl_depthoffset;
extern cvar_t	*gl_clear;
extern cvar_t	*gl_test;		// cvar to testify new effects
extern cvar_t	*gl_subdivide_size;

extern cvar_t	*r_speeds;
extern cvar_t	*r_fullbright;
extern cvar_t	*r_norefresh;
extern cvar_t	*r_lighting_extended;
extern cvar_t	*r_lighting_modulate;
extern cvar_t	*r_lighting_ambient;
extern cvar_t	*r_studio_lambert;
extern cvar_t	*r_detailtextures;
extern cvar_t	*r_drawentities;
extern cvar_t	*r_decals;
extern cvar_t	*r_novis;
extern cvar_t	*r_nocull;
extern cvar_t	*r_lockpvs;
extern cvar_t	*r_lockfrustum;
extern cvar_t	*r_traceglow;
extern cvar_t	*r_dynamic;
extern cvar_t	*r_lightmap;

extern cvar_t	*vid_brightness;
extern cvar_t	*vid_gamma;

//
// engine shared convars
//
extern cvar_t	*gl_showtextures;
extern cvar_t	*tracerred;
extern cvar_t	*tracergreen;
extern cvar_t	*tracerblue;
extern cvar_t	*traceralpha;
extern cvar_t	*cl_lightstyle_lerping;
extern cvar_t	*r_showhull;
extern cvar_t	*r_fast_particles;

//
// engine callbacks
//
#include "crtlib.h"

#define Mem_Malloc( pool, size ) gEngfuncs._Mem_Alloc( pool, size, false, __FILE__, __LINE__ )
#define Mem_Calloc( pool, size ) gEngfuncs._Mem_Alloc( pool, size, true, __FILE__, __LINE__ )
#define Mem_Realloc( pool, ptr, size ) gEngfuncs._Mem_Realloc( pool, ptr, size, true, __FILE__, __LINE__ )
#define Mem_Free( mem ) gEngfuncs._Mem_Free( mem, __FILE__, __LINE__ )
#define Mem_AllocPool( name ) gEngfuncs._Mem_AllocPool( name, __FILE__, __LINE__ )
#define Mem_FreePool( pool ) gEngfuncs._Mem_FreePool( pool, __FILE__, __LINE__ )
#define Mem_EmptyPool( pool ) gEngfuncs._Mem_EmptyPool( pool, __FILE__, __LINE__ )

#endif // GL_LOCAL_H
