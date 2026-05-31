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
#include "ref_params.h"
#include "mod_local.h"
#include "com_strings.h"
#include "pmove.h"

#define DIRECTDRAW_VERSION 0x0200
#define DIRECT3D_VERSION 0x0500//D3DCOLORMODEL bug
#include <ddraw.h>
#include <d3d.h>

#define MAX_TEXTURES            8192
#define TEXTURES_HASH_SIZE	(MAX_TEXTURES >> 2)

#define MAX_DRAW_STACK	2		// normal view and menu view

#define CULL_VISIBLE	0		// not culled
#define CULL_BACKSIDE	1		// backside of transparent wall
#define CULL_FRUSTUM	2		// culled by frustum
#define CULL_VISFRAME	3		// culled by PVS
#define CULL_OTHER		4		// culled by other reason

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
	ref_viewpass_t rvp;

	cl_entity_t *currententity;
	model_t *currentmodel;
	cl_entity_t *currentbeam;	// same as above but for beams

	gl_frustum_t	frustum;

	mleaf_t *viewleaf;
	mleaf_t *oldviewleaf;
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

	int		skytexturenum; // index into model_t::textures

	// entity lists
	draw_list_t	draw_stack[MAX_DRAW_STACK];
	int		draw_stack_pos;
	draw_list_t *draw_list;

	// matrix states
	qboolean		modelviewIdentity;

	int		visframecount;	// PVS frame
	int		dlightframecount;	// dynamic light frame
	int		realframecount;	// not including viewpasses
	int		framecount;

	qboolean		fCustomRendering;
	qboolean		fResetVis;

	double		frametime;	// special frametime for multipass rendering (will set to 0 on a nextview)
	float		blend;		// global blend value

	// cull info
	vec3_t		modelorg;		// relative to viewpoint

	// get from engine
	model_t *worldmodel;
	world_static_t *world;
	cl_entity_t *entities;
	cl_entity_t *viewent;

	uint max_entities;

	ref_screen_rotation_t rotation;
} dx_globals_t;

struct dx_context_s
{
	HWND window;
	int windowWidth;
	int windowHeight;
	IDirectDraw *pdd;
	IDirectDrawSurface *pddsPrimary;
	IDirectDrawSurface *pddsBack;
	IDirectDrawSurface *pddsZBuffer;
	IDirectDrawClipper *pddClipper;
	IDirectDrawClipper *pddBackClipper;
	IDirect3D *pd3d;
	IDirect3DDevice *pd3dd;
	IDirect3DExecuteBuffer *pd3deb;
	IDirect3DViewport *viewport;
	D3DMATRIXHANDLE mtxWorld;
	D3DMATRIXHANDLE mtxProjection;

	qboolean in2DMode;
	int renderMode;
	vec4_t currentColor;
	D3DTEXTUREHANDLE currentTexture;
	D3DTEXTUREHANDLE lastBoundTexture;
};
typedef struct dx_context_s dx_context_t;

extern ref_instance_t RI;
extern dx_globals_t	tr;
extern dx_context_t dxc;


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
void GL_Bind( int tmu, unsigned int texnum );

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
void R_LoadIdentity( void );
void R_SetupD3D( qboolean set_state );

// r_draw.c
void R_Set2DMode( qboolean enable );
void R_DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int texnum );
void FillRGBA( int rendermode, float x, float y, float w, float h, byte r, byte g, byte b, byte a );

// r_image.c
void R_InitImages( void );
dx_texture_t *R_GetTexture( unsigned int texnum );
void R_ShowTextures( void );
const byte *R_GetTextureOriginalBuffer( unsigned int idx );
int GL_LoadTextureFromBuffer( const char *name, rgbdata_t *pic, texFlags_t flags, qboolean update );
void GL_ProcessTexture( int texnum, float gamma, int topColor, int bottomColor );
void R_GetDetailScaleForTexture( int texture, float *xScale, float *yScale );
void R_SetDetailScaleForTexture( int texture, float xScale, float yScale );
int GL_FindTexture( const char *name );
const char *GL_TextureName( unsigned int texnum );
const byte *GL_TextureData( unsigned int texnum );
int GL_LoadTexture( const char *name, const byte *buf, size_t size, int flags );
int GL_CreateTexture( const char *name, int width, int height, const void *buffer, texFlags_t flags );
void GL_FreeTexture( unsigned int texnum );
void R_OverrideTextureSourceSize( unsigned int textnum, unsigned int srcWidth, unsigned int srcHeight );
void GL_UpdateTexture( int texnum, int cols, int rows, int width, int height, const byte *buffer, pixformat_t fmt );

// r_surf.c
void GL_SubdivideSurface( model_t *mod, msurface_t *fa );
void GL_OrthoBounds( const float *mins, const float *maxs );
byte *Mod_GetCurrentVis( void );
void R_DrawWorld( void );
void R_MarkLeaves( void );
void GL_BuildLightmaps( void );

extern convar_t	r_novis;
extern convar_t	r_nocull;
extern convar_t	r_lockpvs;
extern convar_t	r_lockfrustum;

