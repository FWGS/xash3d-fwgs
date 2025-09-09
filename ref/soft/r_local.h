/*
gl_local.h - renderer local declarations
Copyright (C) 2010 Uncle Mike

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
#include "port.h"
#include "xash3d_types.h"
#include "cvardef.h"
#include "const.h"
#include "com_model.h"
#include "cl_entity.h"
#include "render_api.h"
#include "protocol.h"
#include "dlight.h"
#include "ref_api.h"
#include "xash3d_mathlib.h"
#include "ref_params.h"
#include "enginefeatures.h"
#include "com_strings.h"
#include "pm_movevars.h"
#include "cvardef.h"
typedef struct mip_s mip_t;

typedef int fixed8_t;
typedef int fixed16_t;

#define ASSERT( x ) if( !( x )) gEngfuncs.Host_Error( "assert failed at %s:%i\n", __FILE__, __LINE__ )
#define Assert( x ) if( !( x )) gEngfuncs.Host_Error( "assert failed at %s:%i\n", __FILE__, __LINE__ )

#include <stdio.h>

// make mod_ref.h?
#define LM_SAMPLE_SIZE 16

extern poolhandle_t r_temppool;

#define BLOCK_SIZE         tr.block_size        // lightmap blocksize
#define BLOCK_SIZE_DEFAULT 128                  // for keep backward compatibility
#define BLOCK_SIZE_MAX     1024

#define MAX_TEXTURES    8192 // a1ba: increased by users request
#define MAX_DECAL_SURFS 4096

#if XASH_LOW_MEMORY
	#undef MAX_TEXTURES
	#undef MAX_DECAL_SURFS
	#define MAX_TEXTURES    1024
	#define MAX_DECAL_SURFS 256
#endif

#define MAX_DETAIL_TEXTURES 256
#define MAX_LIGHTMAPS       256
#define SUBDIVIDE_SIZE      64
#define MAX_DRAW_STACK      2           // normal view and menu view

#define SHADEDOT_QUANT    16            // precalculated dot products for quantized angles
#define SHADE_LAMBERT     1.4953241
#define DEFAULT_ALPHATEST 0.0f

// refparams
#define RP_NONE        0
#define RP_ENVVIEW     BIT( 0 )                 // used for cubemapshot
#define RP_OLDVIEWLEAF BIT( 1 )
#define RP_CLIPPLANE   BIT( 2 )

#define RP_NONVIEWERREF ( RP_ENVVIEW )
#define R_ModelOpaque( rm )   ( rm == kRenderNormal )
#define R_StaticEntity( ent ) ( VectorIsNull( ent->origin ) && VectorIsNull( ent->angles ))
#define RP_LOCALCLIENT( e )   (( e ) != NULL && ( e )->index == ( gp_cl->playernum + 1 ) && e->player )
#define RP_NORMALPASS()       ( FBitSet( RI.params, RP_NONVIEWERREF ) == 0 )

#define CL_IsViewEntityLocalPlayer() ( gp_cl->viewentity == ( gp_cl->playernum + 1 ))

#define CULL_VISIBLE  0                 // not culled
#define CULL_BACKSIDE 1                 // backside of transparent wall
#define CULL_FRUSTUM  2                 // culled by frustum
#define CULL_VISFRAME 3                 // culled by PVS
#define CULL_OTHER    4                 // culled by other reason

// bit operation helpers
#define MASK( x )           ( BIT( x ) - 1 )
#define GET_BIT( s, b )     (( s & ( 1 << b )) >> b )
#define MOVE_BIT( s, f, t ) ( GET_BIT( s, f ) << t )


/*
  skins will be outline flood filled and mip mapped
  pics and sprites with alpha will be outline flood filled
  pic won't be mip mapped
  model skin
  sprite frame
  wall texture
  pic
*/

typedef enum
{
	it_skin,
	it_sprite,
	it_wall,
	it_pic,
	it_sky
} imagetype_t;


// ===================================================================

typedef unsigned short pixel_t;

typedef struct vrect_s
{
	int            x, y, width, height;
	struct vrect_s *pnext;
} vrect_t;

#define COLOR_WHITE 0xFFFF
// #define SEPARATE_BLIT
typedef struct
{
	pixel_t      *buffer;                           // invisible buffer
	pixel_t      colormap[32 * 8192];               // 8192 * light levels
	// pixel_t                 *alphamap;              // 256 * 256 translucency map
#ifdef SEPARATE_BLIT
	pixel_t      screen_minor[256];
	pixel_t      screen_major[256];
#else
	pixel_t      screen[256 * 256];
	unsigned int screen32[256 * 256];
#endif
	byte         addmap[256 * 256];
	byte         modmap[256 * 256];
	pixel_t      alphamap[3 * 1024 * 256];
	pixel_t      color;
	qboolean     is2d;
	byte         alpha;

	// maybe compute colormask for minor byte?
	int          rendermode;
	int          rowbytes;                                  // may be > width if displayed in a window
	// can be negative for stupid dibs
	int          width;
	int          height;
} viddef_t;

extern viddef_t vid;

typedef struct
{
	int         params;             // rendering parameters

	qboolean    drawWorld;                  // ignore world for drawing PlayerModel
	qboolean    isSkyVisible;               // sky is visible
	qboolean    onlyClientDraw;             // disabled by client request
	qboolean    drawOrtho;                  // draw world as orthogonal projection

	float       fov_x, fov_y;       // current view fov

	cl_entity_t *currententity;
	model_t     *currentmodel;
	cl_entity_t *currentbeam;       // same as above but for beams

	int         viewport[4];
	// gl_frustum_t	frustum;

	mleaf_t     *viewleaf;
	mleaf_t     *oldviewleaf;
	vec3_t      pvsorigin;
	vec3_t      vieworg;                    // locked vieworigin
	vec3_t      viewangles;
	vec3_t      vforward;
	vec3_t      vright;
	vec3_t      vup;
	vec3_t      base_vup;
	vec3_t      base_vpn;
	vec3_t      base_vright;

	vec3_t      cullorigin;
	vec3_t      cull_vforward;
	vec3_t      cull_vright;
	vec3_t      cull_vup;

	int         cached_contents;            // in water
	int         cached_waterlevel;          // was in water
	float       farClip;

	float       skyMins[2][6];
	float       skyMaxs[2][6];

	matrix4x4   objectMatrix;                       // currententity matrix
	matrix4x4   worldviewMatrix;                    // modelview for world
	matrix4x4   modelviewMatrix;                    // worldviewMatrix * objectMatrix

	matrix4x4   projectionMatrix;
	matrix4x4   worldviewProjectionMatrix;           // worldviewMatrix * projectionMatrix
	byte        visbytes[( MAX_MAP_LEAFS + 7 ) / 8]; // actual PVS for current frame

	float       viewplanedist;

	// q2 oldrefdef
	vrect_t     vrect;                              // subwindow in video for refresh
	// FIXME: not need vrect next field here?
	vrect_t     aliasvrect;                         // scaled Alias version
	int         vrectright, vrectbottom;            // right & bottom screen coords
	int         aliasvrectright, aliasvrectbottom;  // scaled Alias versions
	float       vrectrightedge;                     // rightmost right edge we care about,
	//  for use in edge list
	float       fvrectx, fvrecty;             // for floating-point compares
	float       fvrectx_adj, fvrecty_adj;     // left and top edges, for clamping
	int         vrect_x_adj_shift20;          // (vrect.x + 0.5 - epsilon) << 20
	int         vrectright_adj_shift20;       // (vrectright + 0.5 - epsilon) << 20
	float       fvrectright_adj, fvrectbottom_adj;
	// right and bottom edges, for clamping
	float       fvrectright;                        // rightmost edge, for Alias clamping
	float       fvrectbottom;                       // bottommost edge, for Alias clampin


} ref_instance_t;

typedef struct
{
	cl_entity_t *edge_entities[MAX_VISIBLE_PACKET];         // brush edge drawing
	cl_entity_t *solid_entities[MAX_VISIBLE_PACKET];        // opaque moving or alpha brushes
	cl_entity_t *trans_entities[MAX_VISIBLE_PACKET];        // translucent brushes
	cl_entity_t *beam_entities[MAX_VISIBLE_PACKET];
	uint        num_edge_entities;
	uint        num_solid_entities;
	uint        num_trans_entities;
	uint        num_beam_entities;
} draw_list_t;

typedef struct
{
	int          defaultTexture;            // use for bad textures
	int          particleTexture;
	int          whiteTexture;
	int          grayTexture;
	int          blackTexture;
	int          solidskyTexture;           // quake1 solid-sky layer
	int          alphaskyTexture;           // quake1 alpha-sky layer
	int          lightmapTextures[MAX_LIGHTMAPS];
	int          dlightTexture;     // custom dlight texture
	int          skyboxTextures[6]; // skybox sides
	int          cinTexture;        // cinematic texture

	// entity lists
	draw_list_t  draw_stack[MAX_DRAW_STACK];
	int          draw_stack_pos;
	draw_list_t  *draw_list;

	msurface_t   *draw_decals[MAX_DECAL_SURFS];
	int          num_draw_decals;

	// OpenGL matrix states
	qboolean     modelviewIdentity;

	int          visframecount;     // PVS frame
	int          dlightframecount;  // dynamic light frame
	int          realframecount;    // not including viewpasses
	int          framecount;

	qboolean     fCustomRendering;
	qboolean     fResetVis;
	qboolean     fFlipViewModel;

	// tree visualization stuff
	int          recursion_level;
	int          max_recursion;

	byte         visbytes[( MAX_MAP_LEAFS + 7 ) / 8]; // member custom PVS
	int          lightstylevalue[MAX_LIGHTSTYLES];    // value 0 - 65536
	int          block_size;                          // lightmap blocksize

	double       frametime;         // special frametime for multipass rendering (will set to 0 on a nextview)
	float        blend;             // global blend value

	// cull info
	vec3_t       modelorg;                  // relative to viewpoint

	int          sample_size;
	uint         sample_bits;
	qboolean     map_unload;

	// get from engine
	cl_entity_t  *entities;
	movevars_t   *movevars;
	color24      *palette;
	cl_entity_t  *viewent;
	lightstyle_t *lightstyles;
	dlight_t     *dlights;
	dlight_t     *elights;
	byte         *texgammatable;
	uint         *lightgammatable;
	uint         *lineargammatable;
	uint         *screengammatable;

	uint         max_entities;
} gl_globals_t;

typedef struct
{
	uint   c_world_polys;
	uint   c_studio_polys;
	uint   c_sprite_polys;
	uint   c_alias_polys;
	uint   c_world_leafs;

	uint   c_view_beams_count;
	uint   c_active_tents_count;
	uint   c_alias_models_drawn;
	uint   c_studio_models_drawn;
	uint   c_sprite_models_drawn;
	uint   c_particle_count;

	uint   c_client_ents;           // entities that moved to client
	double t_world_node;
	double t_world_draw;
} ref_speeds_t;

extern ref_speeds_t   r_stats;
extern ref_instance_t RI;
extern gl_globals_t   tr;

#define r_numEntities ( tr.draw_list->num_solid_entities + tr.draw_list->num_trans_entities )
#define r_numStatics  ( r_stats.c_client_ents )

typedef struct image_s
{
	char           name[256];       // game path, including extension (can be store image programs)
	word           srcWidth;        // keep unscaled sizes
	word           srcHeight;
	word           width;           // upload width\height
	word           height;
	word           depth;           // texture depth or count of layers for 2D_ARRAY
	byte           numMips;         // mipmap count


	texFlags_t     flags;

	rgba_t         fogParams;       // some water textures
	                                // contain info about underwater fog
	rgbdata_t      *original;       // keep original image

	// debug info
	size_t         size;            // upload size for debug targets

	// detail textures stuff
	float          xscale;
	float          yscale;

	imagetype_t    type;
	pixel_t        *pixels[4];                              // mip levels
	pixel_t        *alpha_pixels;                           // mip levels

	uint           hashValue;
	struct image_s *nextHash;
} image_t;

//
// gl_beams.c
//
void CL_DrawBeams( int fTrans, BEAM *active_beams );
qboolean R_BeamCull( const vec3_t start, const vec3_t end, qboolean pvsOnly );

//
// gl_decals.c
//
void DrawSurfaceDecals( msurface_t *fa, qboolean single, qboolean reverse );
float *R_DecalSetupVerts( decal_t *pDecal, msurface_t *surf, int texture, int *outCount );
// void DrawSingleDecal( decal_t *pDecal, msurface_t *fa );
void R_EntityRemoveDecals( model_t *mod );
// void DrawDecalsBatch( void );
void R_ClearDecals( void );
void R_DecalComputeBasis( msurface_t *surf, int flags, vec3_t textureSpaceBasis[3] );

void GL_Bind( int tmu, unsigned int texnum );

//
// gl_draw.c
//
void R_Set2DMode( qboolean enable );
void R_UploadStretchRaw( int texture, int cols, int rows, int width, int height, const byte *data ); //

// gl_image.c
//
void R_SetTextureParameters( void );
image_t *R_GetTexture( unsigned int texnum );
#define GL_LoadTextureInternal( name, pic, flags )   GL_LoadTextureFromBuffer( name, pic, flags, false )
#define GL_UpdateTextureInternal( name, pic, flags ) GL_LoadTextureFromBuffer( name, pic, flags, true )
int GL_LoadTexture( const char *name, const byte *buf, size_t size, int flags );
int GL_LoadTextureArray( const char **names, int flags );
int GL_LoadTextureFromBuffer( const char *name, rgbdata_t *pic, texFlags_t flags, qboolean update );
byte *GL_ResampleTexture( const byte *source, int in_w, int in_h, int out_w, int out_h, qboolean isNormalMap );
int GL_CreateTexture( const char *name, int width, int height, const void *buffer, texFlags_t flags );
int GL_CreateTextureArray( const char *name, int width, int height, int depth, const void *buffer, texFlags_t flags );
void GL_ProcessTexture( int texnum, float gamma, int topColor, int bottomColor );
void GL_UpdateTexSize( int texnum, int width, int height, int depth );
void GL_ApplyTextureParams( image_t *tex );
int GL_FindTexture( const char *name );
void GL_FreeTexture( unsigned int texnum );
const char *GL_Target( unsigned int target );
void R_InitDlightTexture( void );
void R_TextureList_f( void );
void R_InitImages( void );
void R_ShutdownImages( void );
int R_TexMemory( void );

#if 1
//
// gl_rlight.c
//
void CL_RunLightStyles( lightstyle_t *ls );
void R_PushDlights( void );
void R_GetLightSpot( vec3_t lightspot );
void R_MarkLights( dlight_t *light, int bit, mnode_t *node );
colorVec R_LightVec( const vec3_t start, const vec3_t end, vec3_t lightspot, vec3_t lightvec );
colorVec R_LightPoint( const vec3_t p0 );
#endif
//
// gl_rmain.c
//
void R_ClearScene( void );
void R_LoadIdentity( void );
void R_RenderScene( void );
void R_DrawCubemapView( const vec3_t origin, const vec3_t angles, int size );
void R_SetupRefParams( const struct ref_viewpass_s *rvp );
void R_TranslateForEntity( cl_entity_t *e );
void R_RotateForEntity( cl_entity_t *e );
void R_SetupGL( qboolean set_gl_state );
qboolean R_OpaqueEntity( cl_entity_t *ent );
void R_AllowFog( qboolean allowed );
void R_SetupFrustum( void );
void R_FindViewLeaf( void );
void R_PushScene( void );
void R_PopScene( void );
void R_DrawFog( void );

//
// gl_rmath.c
//
void Matrix4x4_Concat( matrix4x4 out, const matrix4x4 in1, const matrix4x4 in2 );
void Matrix4x4_ConcatTranslate( matrix4x4 out, float x, float y, float z );
void Matrix4x4_ConcatRotate( matrix4x4 out, float angle, float x, float y, float z );
void Matrix4x4_CreateTranslate( matrix4x4 out, float x, float y, float z );
void Matrix4x4_CreateRotate( matrix4x4 out, float angle, float x, float y, float z );
void Matrix4x4_CreateProjection( matrix4x4 out, float xMax, float xMin, float yMax, float yMin, float zNear, float zFar );
void Matrix4x4_CreateOrtho( matrix4x4 m, float xLeft, float xRight, float yBottom, float yTop, float zNear, float zFar );
void Matrix4x4_CreateModelview( matrix4x4 out );

//
// gl_rpart.c
//
void CL_DrawParticlesExternal( const ref_viewpass_t *rvp, qboolean trans_pass, float frametime );
void CL_DrawParticles( double frametime, particle_t *cl_active_particles, float partsize );
void CL_DrawTracers( double frametime, particle_t *cl_active_tracers );


//
// gl_sprite.c
//
void R_SpriteInit( void );
void Mod_LoadSpriteModel( model_t *mod, const void *buffer, qboolean *loaded, uint texFlags );
mspriteframe_t *R_GetSpriteFrame( const model_t *pModel, int frame, float yaw );
void R_DrawSpriteModel( cl_entity_t *e );

//
// gl_studio.c
//
void R_StudioInit( void );
void Mod_LoadStudioModel( model_t *mod, const void *buffer, qboolean *loaded );
void R_StudioLerpMovement( cl_entity_t *e, double time, vec3_t origin, vec3_t angles );
struct mstudiotex_s *R_StudioGetTexture( cl_entity_t *e );
int R_GetEntityRenderMode( cl_entity_t *ent );
void R_DrawStudioModel( cl_entity_t *e );
player_info_t *pfnPlayerInfo( int index );
void R_GatherPlayerLight( void );
float R_StudioEstimateFrame( cl_entity_t *e, mstudioseqdesc_t *pseqdesc, double time );
void R_StudioLerpMovement( cl_entity_t *e, double time, vec3_t origin, vec3_t angles );
void R_StudioResetPlayerModels( void );
void CL_InitStudioAPI( void );
void Mod_StudioLoadTextures( model_t *mod, void *data );
void Mod_StudioUnloadTextures( void *data );

//
// r_polyse.c
//
// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct
{
	void    *pdest;
	short   *pz;
	int     count;
	pixel_t *ptex;
	int     sfrac, tfrac, light, zi;
} spanpackage_t;

extern void (*d_pdrawspans)( spanpackage_t * );
void R_PolysetFillSpans8( spanpackage_t * );
void R_PolysetDrawSpans8_33( spanpackage_t * );
void R_PolysetDrawSpansConstant8_33( spanpackage_t *pspanpackage );
void R_PolysetDrawSpansTextureBlended( spanpackage_t *pspanpackage );
void R_PolysetDrawSpansBlended( spanpackage_t *pspanpackage );
void R_PolysetDrawSpansAdditive( spanpackage_t *pspanpackage );
void R_PolysetDrawSpansGlow( spanpackage_t *pspanpackage );

// #include "vid_common.h"

//
// renderer exports
//
qboolean R_Init( void );
void R_Shutdown( void );
void GL_SetupAttributes( int safegl );
void GL_InitExtensions( void );
void GL_ClearExtensions( void );
void VID_CheckChanges( void );
int GL_LoadTexture( const char *name, const byte *buf, size_t size, int flags );
void GL_FreeImage( const char *name );
qboolean VID_ScreenShot( const char *filename, int shot_type );
qboolean VID_CubemapShot( const char *base, uint size, const float *vieworg, qboolean skyshot );
void R_GammaChanged( qboolean do_reset_gamma );
void R_BeginFrame( qboolean clearScene );
void R_RenderFrame( const struct ref_viewpass_s *vp );
void R_EndFrame( void );
void R_ClearScene( void );
void R_GetTextureParms( int *w, int *h, int texnum );
void R_GetSpriteParms( int *frameWidth, int *frameHeight, int *numFrames, int curFrame, const struct model_s *pSprite );
void R_DrawStretchRaw( float x, float y, float w, float h, int cols, int rows, const byte *data, qboolean dirty );
void R_DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int texnum );
qboolean R_SpeedsMessage( char *out, size_t size );
qboolean R_CullBox( const vec3_t mins, const vec3_t maxs );
int R_WorldToScreen( const vec3_t point, vec3_t screen );
void R_ScreenToWorld( const vec3_t screen, vec3_t point );
qboolean R_AddEntity( struct cl_entity_s *pRefEntity, int entityType );
void Mod_SpriteUnloadTextures( void *data );
void Mod_UnloadAliasModel( struct model_s *mod );
void Mod_AliasUnloadTextures( void *data );
void GL_SetRenderMode( int mode );
void R_RunViewmodelEvents( void );
void R_DrawViewModel( void );
int R_GetSpriteTexture( const struct model_s *m_pSpriteModel, int frame );
void R_DecalShoot( int textureIndex, int entityIndex, int modelIndex, vec3_t pos, int flags, float scale );
void R_DecalRemoveAll( int texture );
int R_CreateDecalList( decallist_t *pList );
void R_ClearAllDecals( void );
byte *Mod_GetCurrentVis( void );
void Mod_SetOrthoBounds( const float *mins, const float *maxs );
void R_NewMap( void );
void CL_AddCustomBeam( cl_entity_t *pEnvBeam );

//
// gl_triapi.c
//
void TriRenderMode( int mode );
void TriBegin( int mode );
void TriEnd( void );
void TriTexCoord2f( float u, float v );
void TriVertex3fv( const float *v );
void TriVertex3f( float x, float y, float z );
void TriColor4f( float r, float g, float b, float a );
void _TriColor4f( float r, float g, float b, float a );
void TriColor4ub( byte r, byte g, byte b, byte a );
void _TriColor4ub( byte r, byte g, byte b, byte a );
int TriWorldToScreen( const float *world, float *screen );
int TriSpriteTexture( model_t *pSpriteModel, int frame );
void TriFog( float flFogColor[3], float flStart, float flEnd, int bOn );
void TriGetMatrix( const int pname, float *matrix );
void TriFogParams( float flDensity, int iFogSkybox );
void TriCullFace( TRICULLSTYLE mode );
void TriBrightness( float brightness );

#define ENGINE_GET_PARM_ ( *gEngfuncs.EngineGetParm )
#define ENGINE_GET_PARM( parm ) ENGINE_GET_PARM_(( parm ), 0 )

extern ref_api_t     gEngfuncs;
extern ref_globals_t *gpGlobals;
extern ref_client_t  *gp_cl;
extern ref_host_t    *gp_host;

DECLARE_ENGINE_SHARED_CVAR_LIST()

//
// helper funcs
//
static inline cl_entity_t *CL_GetEntityByIndex( int index )
{
	if( unlikely( index < 0 || index >= tr.max_entities || !tr.entities ))
		return NULL;

	return &tr.entities[index];
}

static inline model_t *CL_ModelHandle( int index )
{
	if( unlikely( index < 0 || index >= gp_cl->nummodels ))
		return NULL;

	return gp_cl->models[index];
}

static inline byte TextureToGamma( byte b )
{
	return !FBitSet( gp_host->features, ENGINE_LINEAR_GAMMA_SPACE ) ? tr.texgammatable[b] : b;
}

static inline uint LightToTexGamma( uint b )
{
	if( unlikely( b >= 1024 ))
		return 0;

	return !FBitSet( gp_host->features, ENGINE_LINEAR_GAMMA_SPACE ) ? tr.lightgammatable[b] : b;
}

static inline uint ScreenGammaTable( uint b )
{
	if( unlikely( b >= 1024 ))
		return 0;

	return !FBitSet( gp_host->features, ENGINE_LINEAR_GAMMA_SPACE ) ? tr.screengammatable[b] : b;
}

static inline uint LinearGammaTable( uint b )
{
	if( unlikely( b >= 1024 ))
		return 0;

	return !FBitSet( gp_host->features, ENGINE_LINEAR_GAMMA_SPACE ) ? tr.lineargammatable[b] : b;
}

#define WORLDMODEL ( gp_cl->models[1] )

// todo: gl_cull.c
#define R_CullModel( ... ) 0

// softrender defs

#define CACHE_SIZE 32

/*
====================================================
  CONSTANTS
====================================================
*/

#define VID_CBITS  6
#define VID_GRADES ( 1 << VID_CBITS )


// r_shared.h: general refresh-related stuff shared between the refresh and the
// driver


#define MAXVERTS        64               // max points in a surface polygon
#define MAXWORKINGVERTS ( MAXVERTS + 4 ) // max points in an intermediate
//  polygon (while processing)
// !!! if this is changed, it must be changed in d_ifacea.h too !!!
#define MAXHEIGHT 1200
#define MAXWIDTH  1920

#define INFINITE_DISTANCE 0x10000               // distance that's always guaranteed to
//  be farther away than anything in
//  the scene


// d_iface.h: interface header file for rasterization driver modules

#define WARP_WIDTH  320
#define WARP_HEIGHT 240

#define MAX_LBM_HEIGHT 480


#define PARTICLE_Z_CLIP 8.0

// !!! must be kept the same as in quakeasm.h !!!
#define TRANSPARENT_COLOR 0x0349       // 0xFF


// !!! if this is changed, it must be changed in d_ifacea.h too !!!
#define TURB_TEX_SIZE 64                // base turbulent texture size

// !!! if this is changed, it must be changed in d_ifacea.h too !!!
#define CYCLE 128                               // turbulent cycle size

#define SCANBUFFERPAD 0x1000

#define DS_SPAN_LIST_END -128

#define NUMSTACKEDGES    4000
#define MINEDGES         NUMSTACKEDGES
#define NUMSTACKSURFACES 2000
#define MINSURFACES      NUMSTACKSURFACES
#define MAXSPANS         6000

// flags in finalvert_t.flags
#define ALIAS_LEFT_CLIP    0x0001
#define ALIAS_TOP_CLIP     0x0002
#define ALIAS_RIGHT_CLIP   0x0004
#define ALIAS_BOTTOM_CLIP  0x0008
#define ALIAS_Z_CLIP       0x0010
#define ALIAS_XY_CLIP_MASK 0x000F

#define SURFCACHE_SIZE_AT_320X240 1024 * 768

#define BMODEL_FULLY_CLIPPED 0x10    // value returned by R_BmodelCheckBBox ()
//  if bbox is trivially rejected

#define XCENTERING ( 1.0f / 2.0f )
#define YCENTERING ( 1.0f / 2.0f )

#define CLIP_EPSILON 0.001f

// !!! if this is changed, it must be changed in asm_draw.h too !!!
#define NEAR_CLIP 0.01f


// #define MAXALIASVERTS           2000    // TODO: tune this
#define ALIAS_Z_CLIP_PLANE 4

// turbulence stuff

#define AMP   8 * 0x10000
#define AMP2  3
#define SPEED 20


/*
====================================================
TYPES
====================================================
*/

typedef struct
{
	float u, v;
	float s, t;
	float zi;
} emitpoint_t;

/*
** if you change this structure be sure to change the #defines
** listed after it!
*/
#define SMALL_FINALVERT 0

#if SMALL_FINALVERT

typedef struct finalvert_s
{
	short u, v, s, t;
	int   l;
	int   zi;
	int   flags;
	float xyz[3]; // eye space
} finalvert_t;

#define FINALVERT_V0    0
#define FINALVERT_V1    2
#define FINALVERT_V2    4
#define FINALVERT_V3    6
#define FINALVERT_V4    8
#define FINALVERT_V5    12
#define FINALVERT_FLAGS 16
#define FINALVERT_X     20
#define FINALVERT_Y     24
#define FINALVERT_Z     28
#define FINALVERT_SIZE  32

#else

typedef struct finalvert_s
{
	int   u, v, s, t;
	int   l;
	int   zi;
	int   flags;
	float xyz[3]; // eye space
} finalvert_t;

#define FINALVERT_V0    0
#define FINALVERT_V1    4
#define FINALVERT_V2    8
#define FINALVERT_V3    12
#define FINALVERT_V4    16
#define FINALVERT_V5    20
#define FINALVERT_FLAGS 24
#define FINALVERT_X     28
#define FINALVERT_Y     32
#define FINALVERT_Z     36
#define FINALVERT_SIZE  40

#endif


typedef struct
{
	short s;
	short t;
} dstvert_t;

typedef struct
{
	short index_xyz[3];
	short index_st[3];
} dtriangle_t;

typedef struct
{
	byte v[3]; // scaled byte to fit in frame mins/maxs
	byte lightnormalindex;
} dtrivertx_t;

#define DTRIVERTX_V0   0
#define DTRIVERTX_V1   1
#define DTRIVERTX_V2   2
#define DTRIVERTX_LNI  3
#define DTRIVERTX_SIZE 4

typedef struct
{
	void        *pskin;
	int         pskindesc;
	int         skinwidth;
	int         skinheight;
	dtriangle_t *ptriangles;
	finalvert_t *pfinalverts;
	int         numtriangles;
	int         drawtype;
	int         seamfixupX16;
	qboolean    do_vis_thresh;
	int         vis_thresh;
} affinetridesc_t;



typedef struct
{
	pixel_t    *surfdat;            // destination for generated surface
	int        rowbytes;            // destination logical width in bytes
	msurface_t *surf;               // description for surface to generate
	fixed8_t   lightadj[MAXLIGHTMAPS];
	// adjust for lightmap levels for dynamic lighting
	image_t    *image;
	int        surfmip;                     // mipmapped ratio of surface texels / world pixels
	int        surfwidth;                   // in mipmapped texels
	int        surfheight;                  // in mipmapped texels
} drawsurf_t;

// clipped bmodel edges
typedef struct bedge_s
{
	mvertex_t      *v[2];
	struct bedge_s *pnext;
} bedge_t;


// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct clipplane_s
{
	vec3_t                      normal;
	float                       dist;
	struct          clipplane_s *next;
	byte                        leftedge;
	byte                        rightedge;
	byte                        reserved[2];
} clipplane_t;


typedef struct surfcache_s
{
	struct surfcache_s *next;
	struct surfcache_s **owner;                     // NULL is an empty chunk of memory
	int                lightadj[MAXLIGHTMAPS];      // checked for strobe flush
	int                dlight;
	int                size;                                // including header
	unsigned           width;
	unsigned           height;                      // DEBUG only needed for debug
	float              mipscale;
	image_t            *image;
	byte               data[4];                     // width*height elements
} surfcache_t;

// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct espan_s
{
	int            u, v, count;
	struct espan_s *pnext;
} espan_t;


// FIXME: compress, make a union if that will help
// insubmodel is only 1, flags is fewer than 32, spanstate could be a byte
typedef struct surf_s
{
	struct surf_s  *next;                   // active surface stack in r_edge.c
	struct surf_s  *prev;                   // used in r_edge.c for active surf stack
	struct espan_s *spans;                  // pointer to linked list of spans to draw
	int            key;                     // sorting key (BSP order)
	int            last_u;                  // set during tracing
	int            spanstate;               // 0 = not in span
	// 1 = in span
	// -1 = in inverted span (end before
	//  start)
	int         flags;                                      // currentface flags
	msurface_t  *msurf;
	cl_entity_t *entity;
	float       nearzi;                             // nearest 1/z on surface, for mipmapping
	qboolean    insubmodel;
	float       d_ziorigin, d_zistepu, d_zistepv;

	int         pad[2];                                     // to 64 bytes
} surf_t;

// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct edge_s
{
	fixed16_t      u;
	fixed16_t      u_step;
	struct edge_s  *prev, *next;
	unsigned short surfs[2];
	struct edge_s  *nextremove;
	float          nearzi;
	medge16_t      *owner;
} edge_t;


/*
====================================================
VARS
====================================================
*/

//  started
extern float           r_aliasuvscale;  // scale-up factor for screen u and v
//  on Alias vertices passed to driver

extern affinetridesc_t r_affinetridesc;

void D_DrawSurfaces( void );
void R_DrawParticle( void );
void D_ViewChanged( void );

// =======================================================================//

// callbacks to Quake

extern drawsurf_t r_drawsurf;

void R_DrawSurface( void );

// extern int              c_surf;

extern byte r_warpbuffer[WARP_WIDTH * WARP_HEIGHT];




extern float       scale_for_mip;

extern qboolean    d_roverwrapped;
extern surfcache_t *sc_rover;
extern surfcache_t *d_initial_rover;

extern float       d_sdivzstepu, d_tdivzstepu, d_zistepu;
extern float       d_sdivzstepv, d_tdivzstepv, d_zistepv;
extern float       d_sdivzorigin, d_tdivzorigin, d_ziorigin;

extern fixed16_t   sadjust, tadjust;
extern fixed16_t   bbextents, bbextentt;


void D_DrawSpans16( espan_t *pspans );
void D_DrawZSpans( espan_t *pspans );
void Turbulent8( espan_t *pspan );
void NonTurbulent8( espan_t *pspan ); // PGM
void D_BlendSpans16( espan_t *pspan, int alpha );
void D_AlphaSpans16( espan_t *pspan );
void D_AddSpans16( espan_t *pspan );
void TurbulentZ8( espan_t *pspan, int alpha );

surfcache_t     *D_CacheSurface( msurface_t *surface, int miplevel );


extern pixel_t      *d_viewbuffer;
extern short        *d_pzbuffer;
extern unsigned int d_zrowbytes, d_zwidth;
extern short        *zspantable[MAXHEIGHT];
extern int          d_scantable[MAXHEIGHT];

extern int          d_minmip;
extern float        d_scalemip[3];

// ===================================================================

extern int     cachewidth;
extern pixel_t *cacheblock;
extern int     r_screenwidth;


extern int     sintable[1280];
extern int     intsintable[1280];
extern int     blanktable[1280];                        // PGM

extern surf_t  *surfaces, *surface_p, *surf_max;

// surfaces are generated in back to front order by the bsp, so if a surf
// pointer is greater than another one, it should be drawn in front
// surfaces[1] is the background, and is used as the active surface stack.
// surfaces[0] is a dummy, because index 0 is used to indicate no surface
//  attached to an edge_t

// ===================================================================

// extern vec3_t   sxformaxis[4];  // s axis transformed into viewspace
// extern vec3_t   txformaxis[4];  // t axis transformed into viewspac

extern float xcenter, ycenter;
extern float xscale, yscale;
extern float xscaleinv, yscaleinv;
// extern float xscaleshrink, yscaleshrink;


extern edge_t *auxedges;
extern int    r_numallocatededges;
extern edge_t *r_edges, *edge_p, *edge_max;

extern edge_t *newedges[MAXHEIGHT];
extern edge_t *removeedges[MAXHEIGHT];

extern int    r_viewcluster, r_oldviewcluster;

extern int    r_clipflags;
// extern qboolean r_fov_greater_than_90;


extern convar_t sw_clearcolor;
extern convar_t sw_drawflat;
extern convar_t sw_draworder;
extern convar_t sw_maxedges;
extern convar_t sw_mipcap;
extern convar_t sw_mipscale;
extern convar_t sw_surfcacheoverride;
extern convar_t sw_texfilt;
extern convar_t r_traceglow;
extern convar_t sw_noalphabrushes;
extern convar_t r_studio_sort_textures;

extern struct qfrustum_s
{
	mplane_t    screenedge[4];
	clipplane_t view_clipplanes[4];
	int         frustum_indexes[4 * 6];
	int         *pfrustum_indexes[4];
} qfrustum;

#define CACHESPOT( surf ) ((surfcache_t **)surf->info->reserved )
extern int            r_currentkey;
extern int            r_currentbkey;
extern qboolean       insubmodel;

extern vec3_t         r_entorigin;
#if XASH_LOW_MEMORY
extern unsigned short r_leafkeys[MAX_MAP_LEAFS];
#else
extern int            r_leafkeys[MAX_MAP_LEAFS];
#endif
#define LEAF_KEY( pleaf ) r_leafkeys[( pleaf - WORLDMODEL->leafs )]



extern mvertex_t *r_pcurrentvertbase;
// extern int                      r_maxvalidedgeoffset;

typedef struct
{
	finalvert_t *a, *b, *c;
} aliastriangleparms_t;

extern aliastriangleparms_t aliastriangleparms;


extern int   r_aliasblendcolor;

extern float aliasxscale, aliasyscale, aliasxcenter, aliasycenter;
extern float s_ziscale;

void R_DrawTriangle( void );
// void R_DrawTriangle (finalvert_t *index0, finalvert_t *index1, finalvert_t *index2);
void R_AliasClipTriangle( finalvert_t *index0, finalvert_t *index1, finalvert_t *index2 );

//
// r_bsp.c
//
void R_RotateBmodel( void );
void R_DrawSolidClippedSubmodelPolygons( model_t *pmodel, mnode_t *topnode );
void R_DrawSubmodelPolygons( model_t *pmodel, int clipflags, mnode_t *topnode );
void R_DrawBrushModel( cl_entity_t *pent );

//
// r_blitscreen.c
//
void R_InitCaches( void );
void R_BlitScreen( void );
qboolean R_InitBlit( qboolean gl );
qboolean R_SetDisplayTransform( ref_screen_rotation_t rotate, int offset_x, int offset_y, float scale_x, float scale_y );

//
// r_edge.c
//
static inline void R_SurfacePatch( void ) {
}
void R_BeginEdgeFrame( void );
void R_RenderWorld( void );
void R_ScanEdges( void );


//
// r_surf.c
//
void GL_InitRandomTable( void );
void D_FlushCaches( void );

//
// r_draw.c
//
void Draw_Fill( int x, int y, int w, int h );

//
// r_misc.c
//
void R_SetupFrameQ( void );
void R_TransformFrustum( void );
void TransformVector( vec3_t in, vec3_t out );

//
// r_rast.c
//
void R_RenderBmodelFace( bedge_t *pedges, msurface_t *psurf );
void R_RenderFace( msurface_t *fa, int clipflags );

//
// r_main.c
//

void R_RenderTriangle( finalvert_t *fv1, finalvert_t *fv2, finalvert_t *fv3 );
void R_SetupFinalVert( finalvert_t *fv, float x, float y, float z, int light, int s, int t );
void RotatedBBox( vec3_t mins, vec3_t maxs, vec3_t angles, vec3_t tmins, vec3_t tmaxs );
int R_BmodelCheckBBox( float *minmaxs );
int CL_FxBlend( cl_entity_t *e );


void R_SetUpWorldTransform( void );

#define BLEND_ALPHA_LOW( alpha, src, screen ) ( vid.alphamap[(( alpha ) << 18 ) | ((( src ) & 0xff00 ) << 2 ) | (( screen ) >> 6 )] | (( screen ) & 0x3f ))
#define BLEND_ALPHA( alpha, src, dst )        ( alpha ) > 3 ? BLEND_ALPHA_LOW( 7 - 1 - ( alpha ), ( dst ), ( src )) : BLEND_ALPHA_LOW(( alpha ) - 1, ( src ), ( dst ))
#define BLEND_ADD( src, screen )              vid.addmap[(( src ) & 0xff00 ) | (( screen ) >> 8 )] << 8 | (( screen ) & 0xff ) | ((( src ) & 0xff ) >> 0 );
#define BLEND_COLOR( src, color )             vid.modmap[(( src ) & 0xff00 ) | (( color ) >> 8 )] << 8 | (( src ) & ( color ) & 0xff ) | ((( src ) & 0xff ) >> 3 );

#define LM_SAMPLE_SIZE_AUTO( surf ) ( tr.sample_size == -1 ? gEngfuncs.Mod_SampleSizeForFace( surf ) : tr.sample_size )



//
// engine callbacks
//
#include "crtlib.h"
#include "crclib.h"
void _Mem_Free( void *data, const char *filename, int fileline );
void *_Mem_Alloc( poolhandle_t poolptr, size_t size, qboolean clear, const char *filename, int fileline )
ALLOC_CHECK( 2 ) MALLOC_LIKE( _Mem_Free, 1 ) WARN_UNUSED_RESULT;

#define Mem_Malloc( pool, size )       _Mem_Alloc( pool, size, false, __FILE__, __LINE__ )
#define Mem_Calloc( pool, size )       _Mem_Alloc( pool, size, true, __FILE__, __LINE__ )
#define Mem_Realloc( pool, ptr, size ) gEngfuncs._Mem_Realloc( pool, ptr, size, true, __FILE__, __LINE__ )
#define Mem_Free( mem )                _Mem_Free( mem, __FILE__, __LINE__ )
#define Mem_AllocPool( name )          gEngfuncs._Mem_AllocPool( name, __FILE__, __LINE__ )
#define Mem_FreePool( pool )           gEngfuncs._Mem_FreePool( pool, __FILE__, __LINE__ )
#define Mem_EmptyPool( pool )          gEngfuncs._Mem_EmptyPool( pool, __FILE__, __LINE__ )

#endif // GL_LOCAL_H
