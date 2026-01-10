#pragma once

#include "const.h"
#include "cvardef.h"
#include "ref_api.h"
#include "render_api.h"
#include "com_image.h"

#define DIRECTDRAW_VERSION 0x0200
#define DIRECT3D_VERSION 0x0500//D3DCOLORMODEL bug
#include <ddraw.h>
#include <d3d.h>

extern ref_api_t      gEngfuncs;

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
	//gl_frustum_t	frustum;

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
	cl_entity_t *entities;
	uint max_entities;
} gl_globals_t;

extern gl_globals_t	tr;

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

	int renderMode;
	vec4_t currentColor;

};
typedef struct dx_context_s dx_context_t;

extern dx_context_t dxc;

const char *dxResultToStr( HRESULT r );

#define DXCheck(CALL) {\
HRESULT r = CALL;\
if (r != DD_OK)\
	gEngfuncs.Con_Printf( S_ERROR "%s:%d: DirectX error %X(%d 0x%X %d) %s at \"" #CALL "\"\n", __FUNCTION__, __LINE__, r, ( r >> 31 ) & 1, ( r >> 16 ) & 0x7FFF, r & 0xFFFF, dxResultToStr( r ) ); \
}

void GL_SetRenderMode( int mode );

//r_image.c
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
