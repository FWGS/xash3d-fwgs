/*
r_local.h - renderer local declarations
Copyright (C) 2010 Uncle Mike
Copyright (C) 2025-2026 lewa_j

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

#include "ref_common.h"
#include "ref_api.h"
#include "r_frustum.h"
#include "mod_local.h"
#include "com_strings.h"

#define DIRECTDRAW_VERSION 0x0200
#define DIRECT3D_VERSION 0x0500//D3DCOLORMODEL bug
#include <ddraw.h>
#include <d3d.h>

#define Assert(x) if(!( x )) gEngfuncs.Host_Error( "assert failed at %s:%i\n", __FILE__, __LINE__ )


extern poolhandle_t r_temppool;


#define MAX_TEXTURES            8192
#define TEXTURES_HASH_SIZE	(MAX_TEXTURES >> 2)

// refparams
#define RP_NONE		0
#define RP_ENVVIEW		BIT( 0 )	// used for cubemapshot
#define RP_OLDVIEWLEAF	BIT( 1 )
#define RP_CLIPPLANE	BIT( 2 )

#define MAX_DRAW_STACK	2		// normal view and menu view

typedef struct dx_texture_s
{
	char		name[256];	// game path, including extension (can be store image programs)
	word		srcWidth;		// keep unscaled sizes
	word		srcHeight;
	word		width;		// upload width\height
	word		height;

	byte		numMips;		// mipmap count

	int used;
	texFlags_t	flags;
	rgba_t		fogParams;	// some water textures
	// contain info about underwater fog
	rgbdata_t *original;	// keep original image
#if 0
	word		depth;		// texture depth or count of layers for 2D_ARRAY

	GLuint		target;		// glTarget
	GLuint		texnum;		// gl texture binding
	GLint		format;		// uploaded format

	// debug info
	size_t		size;		// upload size for debug targets

	// detail textures stuff
	float		xscale;
	float		yscale;

#endif
	IDirectDrawSurface *dds;
	IDirect3DTexture *d3dTex;
	D3DTEXTUREHANDLE d3dHandle;
	uint		hashValue;
	struct dx_texture_s *nextHash;
} dx_texture_t;

typedef struct d3dEBContext_s
{
	IDirect3DExecuteBuffer *pd3deb;
	DWORD bufferSize;
	void *data;
	DWORD vertCount;
	DWORD insOffset;
	DWORD insLength;
} d3dEBContext_t;

typedef struct
{
	int		params;		// rendering parameters

	qboolean		drawWorld;	// ignore world for drawing PlayerModel
	qboolean		isSkyVisible;	// sky is visible
	qboolean		onlyClientDraw;	// disabled by client request
	qboolean		drawOrtho;	// draw world as orthogonal projection

	float		fov_x, fov_y;	// current view fov

	cl_entity_t *currententity;
	model_t *currentmodel;
	cl_entity_t *currentbeam;	// same as above but for beams

	int		viewport[4];
	gl_frustum_t	frustum;

	mleaf_t *viewleaf;
	mleaf_t *oldviewleaf;
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

	float		skyMins[2][SKYBOX_MAX_SIDES];
	float		skyMaxs[2][SKYBOX_MAX_SIDES];

	matrix4x4		objectMatrix;		// currententity matrix
	matrix4x4		worldviewMatrix;		// modelview for world
	matrix4x4		modelviewMatrix;		// worldviewMatrix * objectMatrix

	matrix4x4		projectionMatrix;
	matrix4x4		worldviewProjectionMatrix;	// worldviewMatrix * projectionMatrix
	byte		visbytes[( MAX_MAP_LEAFS + 7 ) / 8];// actual PVS for current frame

	float		viewplanedist;
	mplane_t		clipPlane;
} ref_instance_t;

typedef struct
{
	cl_entity_t *solid_entities[MAX_VISIBLE_PACKET];	// opaque moving or alpha brushes
	cl_entity_t *trans_entities[MAX_VISIBLE_PACKET];	// translucent brushes
	cl_entity_t *beam_entities[MAX_VISIBLE_PACKET];
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
	int		cinTexture;      	// cinematic texture
	// entity lists
	draw_list_t	draw_stack[MAX_DRAW_STACK];
	int		draw_stack_pos;
	draw_list_t *draw_list;

	int		realframecount;	// not including viewpasses
	int		framecount;

	qboolean		fCustomRendering;
	qboolean		fResetVis;

	double		frametime;	// special frametime for multipass rendering (will set to 0 on a nextview)

	// get from engine
	world_static_t *world;
	cl_entity_t *entities;
	int max_entities;

} dx_globals_t;

struct dx_context_s
{
	HWND window;
	int windowWidth;
	int windowHeight;
	IDirectDraw *pdd;
	IDirectDrawSurface *pddsPrimary;
	IDirectDrawSurface *pddsBack;
	IDirectDrawClipper *pddClipper;
	IDirectDrawClipper *pddBackClipper;
	IDirect3D *pd3d;
	IDirect3DDevice *pd3dd;
	IDirect3DExecuteBuffer *pd3deb;
	IDirect3DViewport *viewport;
	D3DMATRIXHANDLE mtxWorld;
	D3DMATRIXHANDLE mtxProjection;

	int renderMode;
	vec4_t currentColor;

};
typedef struct dx_context_s dx_context_t;

extern ref_instance_t RI;
extern dx_globals_t	tr;
extern dx_context_t dxc;

extern ref_api_t      gEngfuncs;
extern ref_client_t *gp_cl;

const char *dxResultToStr( HRESULT r );

#define DXCheckRet(DST, CALL) \
(DST) = (CALL);\
if ((DST) != DD_OK)\
	gEngfuncs.Con_Printf( S_ERROR "%s():%s:%d: DirectX error %X(%d 0x%X %d) %s at \"" #CALL "\"\n", __FUNCTION__, __FILE__, __LINE__, (DST), ( (DST) >> 31 ) & 1, ( (DST) >> 16 ) & 0x7FFF, (DST) & 0xFFFF, dxResultToStr( r ));

#define DXCheck(CALL) \
{\
	HRESULT r = DD_OK;\
	DXCheckRet(r, CALL)\
}

void GL_SetRenderMode( int mode );

// r_d3d.c
qboolean R_Init( void );
void R_Shutdown( void );
void GL_SetupAttributes( int safegl );
void GL_InitExtensions( void );
void GL_ClearExtensions( void );
void D3D_Resize( int width, int height );
qboolean D3D_StartExecuteBuffer( d3dEBContext_t *ctx );
qboolean D3D_EndExecuteBuffer( d3dEBContext_t *ctx );
void D3D_ReleaseExecuteBuffer( d3dEBContext_t *ctx );
void D3D_SetVertTL( D3DTLVERTEX *v, float x, float y, float z, float w, float tu, float tv );
void D3D_SetTri( D3DTRIANGLE *t, int v1, int v2, int v3 );
void D3D_PutInstruction( void **dst, BYTE opcode, BYTE size, WORD count );
void D3D_PutProcessVertices( void **dst, DWORD flags, WORD start, WORD count );
void D3D_PutRenderState( void **dst, D3DRENDERSTATETYPE type, DWORD arg );
void D3D_PutTransformState( void **dst, D3DTRANSFORMSTATETYPE type, DWORD arg );

// r_main.c
void R_GammaChanged( qboolean do_reset_gamma );
void R_BeginFrame( qboolean clearScene );
void R_RenderScene( void );
void R_EndFrame( void );
void R_AllowFog( qboolean allowed );
void R_RenderFrame( const struct ref_viewpass_s *rvp );
qboolean R_AddEntity( struct cl_entity_s *clent, int type );
void R_ClearScene( void );
int WorldToScreen( const vec3_t world, vec3_t screen );
void ScreenToWorld( const float *screen, float *world );

// r_draw.c
void R_Set2DMode( qboolean enable );
void R_DrawStretchRaw( float x, float y, float w, float h, int cols, int rows, const byte *data, qboolean dirty );
void R_DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int texnum );
void FillRGBA( int rendermode, float x, float y, float w, float h, byte r, byte g, byte b, byte a );
void R_GetSpriteParms( int *frameWidth, int *frameHeight, int *numFrames, int currentFrame, const model_t *pSprite );
int R_GetSpriteTexture( const model_t *m_pSpriteModel, int frame );
void AVI_UploadRawFrame( int texture, int cols, int rows, int width, int height, const byte *data );

// r_image.c
void R_InitImages( void );
dx_texture_t *R_GetTexture( unsigned int texnum );
void R_ShowTextures( void );
const byte *R_GetTextureOriginalBuffer( unsigned int idx );
int GL_LoadTextureFromBuffer( const char *name, rgbdata_t *pic, texFlags_t flags, qboolean update );
void GL_ProcessTexture( int texnum, float gamma, int topColor, int bottomColor );
void GetDetailScaleForTexture( int texture, float *xScale, float *yScale );
void GetExtraParmsForTexture( int texture, byte *red, byte *green, byte *blue, byte *alpha );
int GL_FindTexture( const char *name );
const char *GL_TextureName( unsigned int texnum );
const byte *GL_TextureData( unsigned int texnum );
int GL_LoadTexture( const char *name, const byte *buf, size_t size, int flags );
int GL_CreateTexture( const char *name, int width, int height, const void *buffer, texFlags_t flags );
int GL_LoadTextureArray( const char **names, int flags );
int GL_CreateTextureArray( const char *name, int width, int height, int depth, const void *buffer, texFlags_t flags );
void GL_FreeTexture( unsigned int texnum );
void R_OverrideTextureSourceSize( unsigned int textnum, unsigned int srcWidth, unsigned int srcHeight );
void GL_UpdateTexSize( int texnum, int width, int height, int depth );

// r_math.c
void Matrix4x4_ToArrayFloatGL( const matrix4x4 in, float out[16] );
void Matrix4x4_Concat( matrix4x4 out, const matrix4x4 in1, const matrix4x4 in2 );
void Matrix4x4_ConcatTranslate( matrix4x4 out, float x, float y, float z );
void Matrix4x4_ConcatRotate( matrix4x4 out, float angle, float x, float y, float z );
void Matrix4x4_CreateProjection( matrix4x4 out, float xMax, float xMin, float yMax, float yMin, float zNear, float zFar );
void Matrix4x4_CreateOrtho( matrix4x4 m, float xLeft, float xRight, float yBottom, float yTop, float zNear, float zFar );
void Matrix4x4_CreateModelview( matrix4x4 out );


// r_surf.c
void GL_SubdivideSurface( model_t *mod, msurface_t *fa );
void GL_OrthoBounds( const float *mins, const float *maxs );
byte *Mod_GetCurrentVis( void );


//
// engine shared convars
//
DECLARE_ENGINE_SHARED_CVAR_LIST( )

#define Mem_Malloc( pool, size ) _Mem_Alloc( pool, size, false, __FILE__, __LINE__ )
#define Mem_Calloc( pool, size ) _Mem_Alloc( pool, size, true, __FILE__, __LINE__ )
#define Mem_Realloc( pool, ptr, size ) gEngfuncs._Mem_Realloc( pool, ptr, size, true, __FILE__, __LINE__ )
#define Mem_Free( mem ) _Mem_Free( mem, __FILE__, __LINE__ )
#define Mem_AllocPool( name ) gEngfuncs._Mem_AllocPool( name, __FILE__, __LINE__ )
#define Mem_FreePool( pool ) gEngfuncs._Mem_FreePool( pool, __FILE__, __LINE__ )
#define Mem_EmptyPool( pool ) gEngfuncs._Mem_EmptyPool( pool, __FILE__, __LINE__ )
