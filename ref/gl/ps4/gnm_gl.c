/*
gnm_gl.c - OpenGL ES 2.0 subset over GNM for ref_gl on PS4
Copyright (C) 2026 sergiopoverony

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

// Implements what ref_gl with gl2_shim calls. gl2_shim turns
// fixed function GL into one shader with a few feature flags; programs here only
// recognize those flags in the shader source and use precompiled variants
// (see shaders/ and gnm_shaders.c). Vertex data is copied into per-submit
// memory on every draw and pulled by the vertex shader.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gnm_buffer.h>
#include <gnm_dataformat.h>
#include <gnm_drawcommandbuffer.h>
#include <gnm_sampler.h>
#include <gnm_texture.h>
#include <gpuaddr.h>
#include <platform.h>
#include <pm4/sid.h>

#define GNM_DEV_CLIENT
#include "gnm_dev.h"
#include "gnm_gl.h"
#include "gnm_shaders.h"

const gnm_dev_api_t *gnm_api;

#define GNM_LOG( ... ) gnm_api->log( "GNM: " __VA_ARGS__ )

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef float GLclampf;
typedef double GLdouble;
typedef double GLclampd;
typedef char GLchar;
typedef void GLvoid;
typedef intptr_t GLintptr;
typedef intptr_t GLsizeiptr;

#define GL_NO_ERROR                 0
#define GL_INVALID_ENUM             0x0500
#define GL_INVALID_VALUE            0x0501
#define GL_INVALID_OPERATION        0x0502
#define GL_OUT_OF_MEMORY            0x0505

#define GL_POINTS                   0x0000
#define GL_LINES                    0x0001
#define GL_LINE_LOOP                0x0002
#define GL_LINE_STRIP               0x0003
#define GL_TRIANGLES                0x0004
#define GL_TRIANGLE_STRIP           0x0005
#define GL_TRIANGLE_FAN             0x0006
#define GL_QUADS                    0x0007
#define GL_POLYGON                  0x0009

#define GL_ZERO                     0
#define GL_ONE                      1
#define GL_SRC_COLOR                0x0300
#define GL_ONE_MINUS_SRC_COLOR      0x0301
#define GL_SRC_ALPHA                0x0302
#define GL_ONE_MINUS_SRC_ALPHA      0x0303
#define GL_DST_ALPHA                0x0304
#define GL_ONE_MINUS_DST_ALPHA      0x0305
#define GL_DST_COLOR                0x0306
#define GL_ONE_MINUS_DST_COLOR      0x0307
#define GL_SRC_ALPHA_SATURATE       0x0308

#define GL_NEVER                    0x0200
#define GL_ALWAYS                   0x0207

#define GL_FRONT                    0x0404
#define GL_BACK                     0x0405
#define GL_FRONT_AND_BACK           0x0408
#define GL_CW                       0x0900
#define GL_CCW                      0x0901

#define GL_CULL_FACE                0x0B44
#define GL_DEPTH_TEST               0x0B71
#define GL_STENCIL_TEST             0x0B90
#define GL_BLEND                    0x0BE2
#define GL_SCISSOR_TEST             0x0C11
#define GL_POLYGON_OFFSET_FILL      0x8037
#define GL_VIEWPORT                 0x0BA2

#define GL_DEPTH_BUFFER_BIT         0x00000100
#define GL_COLOR_BUFFER_BIT         0x00004000

#define GL_UNSIGNED_BYTE            0x1401
#define GL_UNSIGNED_SHORT           0x1403
#define GL_UNSIGNED_INT             0x1405
#define GL_FLOAT                    0x1406

#define GL_ALPHA                    0x1906
#define GL_RGB                      0x1907
#define GL_RGBA                     0x1908
#define GL_LUMINANCE                0x1909
#define GL_LUMINANCE_ALPHA          0x190A
#define GL_BGR                      0x80E0
#define GL_BGRA                     0x80E1

#define GL_TEXTURE_2D               0x0DE1
#define GL_TEXTURE_MAG_FILTER       0x2800
#define GL_TEXTURE_MIN_FILTER       0x2801
#define GL_TEXTURE_WRAP_S           0x2802
#define GL_TEXTURE_WRAP_T           0x2803
#define GL_NEAREST                  0x2600
#define GL_NEAREST_MIPMAP_NEAREST   0x2700
#define GL_NEAREST_MIPMAP_LINEAR    0x2702
#define GL_LINEAR_MIPMAP_NEAREST    0x2701
#define GL_LINEAR_MIPMAP_LINEAR     0x2703
#define GL_CLAMP                    0x2900
#define GL_CLAMP_TO_EDGE            0x812F
#define GL_MIRRORED_REPEAT          0x8370
#define GL_TEXTURE0                 0x84C0

#define GL_UNPACK_ALIGNMENT         0x0CF5
#define GL_PACK_ALIGNMENT           0x0D05

#define GL_VENDOR                   0x1F00
#define GL_RENDERER                 0x1F01
#define GL_VERSION                  0x1F02
#define GL_EXTENSIONS               0x1F03
#define GL_TEXTURE_MAX_ANISOTROPY_EXT     0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C

#define GL_MAX_TEXTURE_SIZE         0x0D33
#define GL_MAX_TEXTURE_UNITS        0x84E2
#define GL_MAX_TEXTURE_IMAGE_UNITS  0x8872
#define GL_MAX_TEXTURE_COORDS       0x8871
#define GL_MAX_VERTEX_ATTRIBS       0x8869
#define GL_MAX_VERTEX_UNIFORM_COMPONENTS 0x8B4A
#define GL_MAJOR_VERSION            0x821B
#define GL_MINOR_VERSION            0x821C
#define GL_NUM_EXTENSIONS           0x821D
#define GL_MAX_ELEMENTS_VERTICES    0x80E8
#define GL_MAX_ELEMENTS_INDICES     0x80E9
#define GL_RED_BITS                 0x0D52
#define GL_GREEN_BITS               0x0D53
#define GL_BLUE_BITS                0x0D54
#define GL_ALPHA_BITS               0x0D55
#define GL_DEPTH_BITS               0x0D56
#define GL_STENCIL_BITS             0x0D57

#define GL_ARRAY_BUFFER             0x8892
#define GL_ELEMENT_ARRAY_BUFFER     0x8893

#define GL_FRAGMENT_SHADER          0x8B30
#define GL_VERTEX_SHADER            0x8B31
#define GL_DELETE_STATUS            0x8B80
#define GL_COMPILE_STATUS           0x8B81
#define GL_LINK_STATUS              0x8B82
#define GL_VALIDATE_STATUS          0x8B83
#define GL_INFO_LOG_LENGTH          0x8B84
#define GL_ATTACHED_SHADERS         0x8B85
#define GL_SHADER_TYPE              0x8B4F

#define MAX_TEXTURE_UNITS  2
#define VS_BASE_VERTEX_SGPR 2 // see set_shaders

// descriptor tables: binding N of a shader is at offset N * 64, see psbc_build_set_layout
// in opengnm-psbc; bindings are in shaders/gl2.*.glsl
#define BINDING( n ) (( n ) * 64 )
#define VS_TABLE_SIZE BINDING( 1 + SEM_COUNT ) // uniforms, then one buffer per attribute
#define PS_TABLE_SIZE BINDING( 1 + MAX_TEXTURE_UNITS ) // uniforms, then textures
#define MAX_ATTRIBS        8
#define MAX_VERTS          ( 1024 * 1024 )

// gl2_shim attributes, their order in programs and shader variants
enum { SEM_POS, SEM_COLOR, SEM_TC0, SEM_TC1, SEM_COUNT };

// uniform locations given to gl2_shim
enum { U_MVP, U_COLOR, U_ALPHA, U_FOG, U_TEX0, U_TEX1, U_COUNT };
static const char *uniform_names[U_COUNT] = { "uMVP", "uColor", "uAlphaTest", "uFog", "uTex0", "uTex1" };

/*
==============================================================================

Objects

==============================================================================
*/
#define MAX_TEXTURE_LEVELS 14 // 8192x8192 down to 1x1

typedef struct gl_texture_level_s
{
	uint8_t *shadow;  // RGBA8 copy, kept for partial updates and reuploads
	int width, height;
	uint32_t pitch;   // pixels per row in GPU memory
	uint64_t offset;  // from the texture base address
} gl_texture_level_t;

typedef struct gl_texture_s
{
	bool exists;
	int width, height;    // level 0
	int numlevels;        // levels in GPU memory
	bool dirty;           // shadow copies changed, GPU memory is rebuilt before the next draw
	void *gpu;
	gl_texture_level_t levels[MAX_TEXTURE_LEVELS];
	GnmTexture desc;
	GLenum min_filter, mag_filter, wrap_s, wrap_t;
	float max_anisotropy; // GL_EXT_texture_filter_anisotropic, 0 and 1 - off
	uint32_t used_serial; // submit that reads current GPU memory
} gl_texture_t;

typedef struct gl_buffer_s
{
	bool exists;
	uint8_t *data;
	size_t size;
} gl_buffer_t;

typedef struct gl_object_s
{
	enum { OBJ_FREE, OBJ_SHADER, OBJ_PROGRAM } kind;

	// shader
	GLenum type;
	int flags;     // parsed from the source
	bool compiled;

	// program
	int vsflags, psflags;
	bool linked;
	float uniforms_mvp[16];
	float uniforms_color[4];
	float uniforms_fog[4];
	float uniform_alpha;
} gl_object_t;

typedef struct gl_attrib_s
{
	bool enabled;
	GLint size;
	GLenum type;
	GLsizei stride;
	const void *pointer;
	GLuint buffer;
} gl_attrib_t;

static struct
{
	bool initialized;
	GLenum error;

	gl_texture_t *textures;
	int numtextures;
	GLuint next_texture;
	gl_texture_t white; // bound when nothing usable is

	gl_buffer_t *buffers;
	int numbuffers;
	GLuint next_buffer;
	GLuint array_buffer, element_buffer;

	gl_object_t *objects;
	int numobjects;
	GLuint program;

	gl_attrib_t attribs[MAX_ATTRIBS];

	int active_unit;
	GLuint bound[MAX_TEXTURE_UNITS];

	// render state
	bool blend, depth_test, cull, scissor_test, polygon_offset;
	GLenum blend_src, blend_dst;
	GLenum depth_func;
	bool depth_mask;
	uint32_t color_mask;
	GLenum cull_face, front_face;
	float offset_factor, offset_units;
	int viewport[4], scissor[4];
	float depth_near, depth_far;
	float clear_color[4];
	float clear_depth;
	int unpack_alignment;
	int pack_alignment;

	// what's emitted into the current command buffer
	bool dirty_state;
	const gnm_vs_t *cur_vs;
	const gnm_ps_t *cur_ps;
	int cur_prim;

	gnm_vs_t clear_vs, vs[GNM_GL2_VS_COUNT];
	gnm_ps_t clear_ps, ps[GNM_GL2_PS_COUNT];

	bool flip_front; // front face meaning swapped, needed on the console (-gnmnoflip disables)
	int mipmaps;     // 0 - level 0 only, 1 - power of two textures, 2 - all textures

	// statistics, logged at first frames
	uint32_t draws, verts;
} gl;

static void set_error( GLenum err )
{
	if( gl.error == GL_NO_ERROR )
		gl.error = err;
}

static uint32_t recording_serial( void )
{
	// submit_serial is incremented when the current command buffer is submitted
	return gnm_dev.submit_serial + 1;
}

/*
==============================================================================

Unimplemented functions: every name gets its own stub that reports it once

==============================================================================
*/
#define MAX_STUBS 128
static const char *stub_names[MAX_STUBS];
static bool stub_reported[MAX_STUBS];
static int numstubs;

static void stub_called( int i )
{
	if( !stub_reported[i] )
	{
		GNM_LOG( "unimplemented %s called\n", stub_names[i] ? stub_names[i] : "?" );
		stub_reported[i] = true;
	}
}

#define STUB( n ) static long stub_##n( void ) { stub_called( n ); return 0; }
#define STUB8( n ) STUB( n##0 ) STUB( n##1 ) STUB( n##2 ) STUB( n##3 ) STUB( n##4 ) STUB( n##5 ) STUB( n##6 ) STUB( n##7 )
STUB8( 0 ) STUB8( 1 ) STUB8( 2 ) STUB8( 3 ) STUB8( 4 ) STUB8( 5 ) STUB8( 6 ) STUB8( 7 )
STUB8( 8 ) STUB8( 9 ) STUB8( 10 ) STUB8( 11 ) STUB8( 12 ) STUB8( 13 ) STUB8( 14 ) STUB8( 15 )
#undef STUB
#undef STUB8

#define STUBREF8( n ) stub_##n##0, stub_##n##1, stub_##n##2, stub_##n##3, stub_##n##4, stub_##n##5, stub_##n##6, stub_##n##7
static long (*const stubs[MAX_STUBS])( void ) =
{
	STUBREF8( 0 ), STUBREF8( 1 ), STUBREF8( 2 ), STUBREF8( 3 ), STUBREF8( 4 ), STUBREF8( 5 ), STUBREF8( 6 ), STUBREF8( 7 ),
	STUBREF8( 8 ), STUBREF8( 9 ), STUBREF8( 10 ), STUBREF8( 11 ), STUBREF8( 12 ), STUBREF8( 13 ), STUBREF8( 14 ), STUBREF8( 15 ),
};
#undef STUBREF8

static void *get_stub( const char *name )
{
	int i;

	for( i = 0; i < numstubs; i++ )
	{
		if( !strcmp( stub_names[i], name ))
			return (void *)stubs[i];
	}

	if( numstubs >= MAX_STUBS )
		return NULL;

	stub_names[numstubs] = strdup( name );
	return (void *)stubs[numstubs++];
}

/*
==============================================================================

Errors, strings, queries

==============================================================================
*/
static GLenum gl_GetError( void )
{
	GLenum err = gl.error;
	gl.error = GL_NO_ERROR;
	return err;
}

static const char *gl_GetString( GLenum name )
{
	switch( name )
	{
	case GL_VENDOR: return "Xash3D FWGS";
	case GL_RENDERER: return "PS4 GNM (opengnm)";
	case GL_VERSION: return "OpenGL ES 2.0 xash-gnm";
	case GL_SHADING_LANGUAGE_VERSION: return "OpenGL ES GLSL ES 1.00";
	case GL_EXTENSIONS: return "GL_OES_texture_npot GL_EXT_draw_range_elements GL_OES_draw_elements_base_vertex GL_EXT_texture_filter_anisotropic";
	}

	set_error( GL_INVALID_ENUM );
	return "";
}

static void gl_GetIntegerv( GLenum pname, GLint *v )
{
	static bool reported[16];
	static GLenum reported_names[16];
	int i;

	switch( pname )
	{
	case GL_MAJOR_VERSION: *v = 2; return;
	case GL_MINOR_VERSION: *v = 0; return;
	case GL_NUM_EXTENSIONS: *v = 0; return;
	case GL_MAX_TEXTURE_SIZE: *v = 4096; return;
	case GL_MAX_TEXTURE_UNITS:
	case GL_MAX_TEXTURE_IMAGE_UNITS:
	case GL_MAX_TEXTURE_COORDS: *v = MAX_TEXTURE_UNITS; return;
	case GL_MAX_VERTEX_ATTRIBS: *v = MAX_ATTRIBS; return;
	case GL_MAX_VERTEX_UNIFORM_COMPONENTS: *v = 1024; return;
	case GL_MAX_ELEMENTS_VERTICES:
	case GL_MAX_ELEMENTS_INDICES: *v = MAX_VERTS; return;
	case GL_RED_BITS: case GL_GREEN_BITS: case GL_BLUE_BITS: case GL_ALPHA_BITS: *v = 8; return;
	case GL_DEPTH_BITS: *v = 24; return;
	case GL_STENCIL_BITS: *v = 0; return;
	case GL_VIEWPORT:
		for( i = 0; i < 4; i++ )
			v[i] = gl.viewport[i];
		return;
	}

	*v = 0;

	for( i = 0; i < 16 && reported[i]; i++ )
	{
		if( reported_names[i] == pname )
			return;
	}

	if( i < 16 )
	{
		reported[i] = true;
		reported_names[i] = pname;
		GNM_LOG( "glGetIntegerv: unknown 0x%x\n", pname );
	}
}

static void gl_GetFloatv( GLenum pname, GLfloat *v )
{
	GLint i[4] = { 0 };
	int n = pname == GL_VIEWPORT ? 4 : 1, k;

	if( pname == GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT )
	{
		*v = 16.0f;
		return;
	}

	gl_GetIntegerv( pname, i );
	for( k = 0; k < n; k++ )
		v[k] = (float)i[k];
}

static void gl_GetBooleanv( GLenum pname, GLboolean *v )
{
	GLint i = 0;
	gl_GetIntegerv( pname, &i );
	*v = i != 0;
}

static void gl_Finish( void )
{
	// everything recorded so far has to be done
	gnm_dev_flush( "glFinish" );
}

static void gl_Flush( void )
{
}

static void gl_Hint( GLenum target, GLenum mode )
{
}

static void gl_PixelStorei( GLenum pname, GLint param )
{
	if( param != 1 && param != 2 && param != 4 && param != 8 )
		return;

	if( pname == GL_UNPACK_ALIGNMENT )
		gl.unpack_alignment = param;
	else if( pname == GL_PACK_ALIGNMENT )
		gl.pack_alignment = param;
}

static void gl_PixelStoref( GLenum pname, GLfloat param )
{
	gl_PixelStorei( pname, (GLint)param );
}

/*
==============================================================================

Render state

==============================================================================
*/
static bool *cap_ptr( GLenum cap )
{
	switch( cap )
	{
	case GL_BLEND: return &gl.blend;
	case GL_DEPTH_TEST: return &gl.depth_test;
	case GL_CULL_FACE: return &gl.cull;
	case GL_SCISSOR_TEST: return &gl.scissor_test;
	case GL_POLYGON_OFFSET_FILL: return &gl.polygon_offset;
	}
	return NULL;
}

static void gl_Enable( GLenum cap )
{
	bool *p = cap_ptr( cap );

	if( p && !*p )
	{
		*p = true;
		gl.dirty_state = true;
	}
}

static void gl_Disable( GLenum cap )
{
	bool *p = cap_ptr( cap );

	if( p && *p )
	{
		*p = false;
		gl.dirty_state = true;
	}
}

static GLboolean gl_IsEnabled( GLenum cap )
{
	bool *p = cap_ptr( cap );
	return p ? *p : false;
}

static void gl_BlendFunc( GLenum src, GLenum dst )
{
	gl.blend_src = src;
	gl.blend_dst = dst;
	gl.dirty_state = true;
}

static void gl_DepthFunc( GLenum func )
{
	gl.depth_func = func;
	gl.dirty_state = true;
}

static void gl_DepthMask( GLboolean flag )
{
	gl.depth_mask = flag;
	gl.dirty_state = true;
}

static void gl_DepthRangef( GLfloat n, GLfloat f )
{
	gl.depth_near = n;
	gl.depth_far = f;
	gl.dirty_state = true;
}

static void gl_DepthRange( GLclampd n, GLclampd f )
{
	gl_DepthRangef( (float)n, (float)f );
}

static void gl_ColorMask( GLboolean r, GLboolean g, GLboolean b, GLboolean a )
{
	gl.color_mask = ( r ? 1 : 0 ) | ( g ? 2 : 0 ) | ( b ? 4 : 0 ) | ( a ? 8 : 0 );
	gl.dirty_state = true;
}

static void gl_CullFace( GLenum mode )
{
	gl.cull_face = mode;
	gl.dirty_state = true;
}

static void gl_FrontFace( GLenum mode )
{
	gl.front_face = mode;
	gl.dirty_state = true;
}

static void gl_PolygonOffset( GLfloat factor, GLfloat units )
{
	gl.offset_factor = factor;
	gl.offset_units = units;
	gl.dirty_state = true;
}

static void gl_Viewport( GLint x, GLint y, GLsizei w, GLsizei h )
{
	gl.viewport[0] = x;
	gl.viewport[1] = y;
	gl.viewport[2] = w;
	gl.viewport[3] = h;
	gl.dirty_state = true;
}

static void gl_Scissor( GLint x, GLint y, GLsizei w, GLsizei h )
{
	gl.scissor[0] = x;
	gl.scissor[1] = y;
	gl.scissor[2] = w;
	gl.scissor[3] = h;
	gl.dirty_state = true;
}

static void gl_ClearColor( GLclampf r, GLclampf g, GLclampf b, GLclampf a )
{
	gl.clear_color[0] = r;
	gl.clear_color[1] = g;
	gl.clear_color[2] = b;
	gl.clear_color[3] = a;
}

static void gl_ClearDepthf( GLclampf d )
{
	gl.clear_depth = d;
}

static void gl_ClearDepth( GLclampd d )
{
	gl.clear_depth = (float)d;
}

static void gl_noop_GLenum( GLenum a )
{
}

static void gl_noop_GLfloat( GLfloat a )
{
}

static void gl_noop_GLenum_GLenum( GLenum a, GLenum b )
{
}

static void gl_StencilFunc( GLenum func, GLint ref, GLuint mask )
{
}

static void gl_StencilOp( GLenum fail, GLenum zfail, GLenum zpass )
{
}

static void gl_StencilMask( GLuint mask )
{
}

static void gl_ClearStencil( GLint s )
{
}

static GnmBlendOp blend_factor( GLenum f )
{
	switch( f )
	{
	case GL_ZERO: return GNM_BLEND_ZERO;
	case GL_ONE: return GNM_BLEND_ONE;
	case GL_SRC_COLOR: return GNM_BLEND_SRC_COLOR;
	case GL_ONE_MINUS_SRC_COLOR: return GNM_BLEND_ONE_MINUS_SRC_COLOR;
	case GL_SRC_ALPHA: return GNM_BLEND_SRC_ALPHA;
	case GL_ONE_MINUS_SRC_ALPHA: return GNM_BLEND_ONE_MINUS_SRC_ALPHA;
	case GL_DST_ALPHA: return GNM_BLEND_DEST_ALPHA;
	case GL_ONE_MINUS_DST_ALPHA: return GNM_BLEND_ONE_MINUS_DEST_ALPHA;
	case GL_DST_COLOR: return GNM_BLEND_DEST_COLOR;
	case GL_ONE_MINUS_DST_COLOR: return GNM_BLEND_ONE_MINUS_DEST_COLOR;
	case GL_SRC_ALPHA_SATURATE: return GNM_BLEND_SRC_ALPHA_SATURATE;
	}
	return GNM_BLEND_ONE;
}

static GnmDepthCompare depth_compare( GLenum f )
{
	// GL_NEVER..GL_ALWAYS are in the same order as GNM values
	if( f >= GL_NEVER && f <= GL_ALWAYS )
		return (GnmDepthCompare)( f - GL_NEVER );
	return GNM_DEPTH_COMPARE_LESSEQUAL;
}

static inline uint32_t float_bits( float f )
{
	uint32_t u;
	memcpy( &u, &f, sizeof( u ));
	return u;
}

static int clampi( int v, int lo, int hi )
{
	return v < lo ? lo : v > hi ? hi : v;
}

// viewport transform like freegnm-examples setupviewport(), with OpenGL conventions:
// origin at the bottom left, depth range maps 0..1 from the vertex shader
static void emit_viewport( GnmCommandBuffer *cmd )
{
	const int H = gnm_dev.height, W = gnm_dev.width;
	const float w = (float)gl.viewport[2], h = (float)gl.viewport[3];
	const float left = (float)gl.viewport[0], top = (float)( H - gl.viewport[1] - gl.viewport[3] );
	GnmSetViewportInfo vp;
	GnmViewportTransformControl vtc;
	int sx0, sy0, sx1, sy1;
	int hwx, hwy;
	float hwmin, hwmax, gbx, gby;

	memset( &vp, 0, sizeof( vp ));
	vp.dmin = 0.0f;
	vp.dmax = 1.0f;
	vp.scale[0] = w * 0.5f;
	vp.scale[1] = -h * 0.5f;
	vp.scale[2] = gl.depth_far - gl.depth_near;
	vp.offset[0] = left + w * 0.5f;
	vp.offset[1] = top + h * 0.5f;
	vp.offset[2] = gl.depth_near;
	sceGnmDrawCmdSetViewport( cmd, 0, &vp );

	memset( &vtc, 0, sizeof( vtc ));
	vtc.scalex = vtc.offsetx = vtc.scaley = vtc.offsety = vtc.scalez = vtc.offsetz = true;
	vtc.invertw = true;
	sceGnmDrawCmdSetViewportTransformControl( cmd, &vtc );

	// guard band lets primitives go past the viewport, screen scissor cuts them
	hwx = ( clampi( (int)floorf( vp.offset[0] / 16.0f + 0.5f ), -1000, 508 )) & ~0x3;
	hwy = ( clampi( (int)floorf( vp.offset[1] / 16.0f + 0.5f ), -1000, 508 )) & ~0x3;
	if( hwx < 0 ) hwx = 0;
	if( hwy < 0 ) hwy = 0;
	sceGnmDrawCmdSetHwScreenOffset( cmd, hwx, hwy );

	hwmin = -(float)( 1 << 23 ) / (float)( 1 << 8 );
	hwmax = (float)(( 1 << 23 ) - 1 ) / (float)( 1 << 8 );
	gbx = fminf( hwmax - fabsf( vp.scale[0] ) - vp.offset[0] + hwx * 16, -fabsf( vp.scale[0] ) + vp.offset[0] - hwx * 16 - hwmin );
	gby = fminf( hwmax - fabsf( vp.scale[1] ) - vp.offset[1] + hwy * 16, -fabsf( vp.scale[1] ) + vp.offset[1] - hwy * 16 - hwmin );
	if( fabsf( vp.scale[0] ) > 0.0f && fabsf( vp.scale[1] ) > 0.0f )
		sceGnmDrawCmdSetGuardBands( cmd, gbx / fabsf( vp.scale[0] ), gby / fabsf( vp.scale[1] ), 1.0f, 1.0f );

	// viewport, then scissor box, in top-left based screen coordinates
	sx0 = gl.viewport[0];
	sy0 = H - gl.viewport[1] - gl.viewport[3];
	sx1 = sx0 + gl.viewport[2];
	sy1 = sy0 + gl.viewport[3];

	if( gl.scissor_test )
	{
		int cx0 = gl.scissor[0], cy0 = H - gl.scissor[1] - gl.scissor[3];
		int cx1 = cx0 + gl.scissor[2], cy1 = cy0 + gl.scissor[3];

		if( cx0 > sx0 ) sx0 = cx0;
		if( cy0 > sy0 ) sy0 = cy0;
		if( cx1 < sx1 ) sx1 = cx1;
		if( cy1 < sy1 ) sy1 = cy1;
	}

	sx0 = clampi( sx0, 0, W );
	sx1 = clampi( sx1, 0, W );
	sy0 = clampi( sy0, 0, H );
	sy1 = clampi( sy1, 0, H );
	if( sx1 < sx0 ) sx1 = sx0;
	if( sy1 < sy0 ) sy1 = sy0;
	sceGnmDrawCmdSetScreenScissor( cmd, sx0, sy0, sx1, sy1 );
}

#define R_028B78_PA_SU_POLY_OFFSET_DB_FMT_CNTL 0x028B78
#define R_028B80_PA_SU_POLY_OFFSET_FRONT_SCALE 0x028B80
#define R_028B84_PA_SU_POLY_OFFSET_FRONT_OFFSET 0x028B84
#define R_028B88_PA_SU_POLY_OFFSET_BACK_SCALE 0x028B88
#define R_028B8C_PA_SU_POLY_OFFSET_BACK_OFFSET 0x028B8C

static void emit_state( GnmCommandBuffer *cmd )
{
	GnmBlendControl blend;
	GnmDepthStencilControl ds;
	GnmDbRenderControl db;
	GnmPrimitiveSetup ps;
	bool front_cw;

	memset( &blend, 0, sizeof( blend ));
	blend.blendenabled = gl.blend;
	blend.colorfunc = GNM_COMB_DST_PLUS_SRC;
	blend.colorsrcmult = blend_factor( gl.blend_src );
	blend.colordstmult = blend_factor( gl.blend_dst );
	blend.alphafunc = GNM_COMB_DST_PLUS_SRC;
	blend.alphasrcmult = blend.colorsrcmult;
	blend.alphadstmult = blend.colordstmult;
	sceGnmDrawCmdSetBlendControl( cmd, 0, &blend );

	memset( &db, 0, sizeof( db ));
	sceGnmDrawCmdSetDbRenderControl( cmd, &db );

	// depth writes need the depth test in OpenGL
	memset( &ds, 0, sizeof( ds ));
	ds.depthenable = gl.depth_test;
	ds.zwrite = gl.depth_test && gl.depth_mask;
	ds.zfunc = gl.depth_test ? depth_compare( gl.depth_func ) : GNM_DEPTH_COMPARE_ALWAYS;
	ds.stencilfunc = GNM_DEPTH_COMPARE_ALWAYS;
	ds.stencilbackfunc = GNM_DEPTH_COMPARE_ALWAYS;
	sceGnmDrawCmdSetDepthStencilControl( cmd, &ds );

	// the viewport flips Y, so winding seen by the GPU is the opposite of OpenGL's
	front_cw = ( gl.front_face == GL_CCW );
	if( gl.flip_front )
		front_cw = !front_cw;

	memset( &ps, 0, sizeof( ps ));
	ps.cullmode = !gl.cull ? GNM_CULL_NONE :
		gl.cull_face == GL_FRONT ? GNM_CULL_FRONT :
		gl.cull_face == GL_BACK ? GNM_CULL_BACK : GNM_CULL_FRONTBACK;
	ps.frontface = front_cw ? GNM_FACE_CW : GNM_FACE_CCW;
	ps.frontmode = GNM_FILL_SOLID;
	ps.backmode = GNM_FILL_SOLID;
	ps.frontoffsetmode = gl.polygon_offset;
	ps.backoffsetmode = gl.polygon_offset;
	ps.provokemode = GNM_PROVOKINGVTX_LAST;
	sceGnmDrawCmdSetPrimitiveSetup( cmd, &ps );

	// 32-bit float depth: offset units are relative to the exponent of the depth value
	gnm_dev_set_context_reg( cmd, R_028B78_PA_SU_POLY_OFFSET_DB_FMT_CNTL, ( -23 & 0xff ) | ( 1 << 8 ));
	gnm_dev_set_context_reg( cmd, R_028B80_PA_SU_POLY_OFFSET_FRONT_SCALE, float_bits( gl.offset_factor * 16.0f ));
	gnm_dev_set_context_reg( cmd, R_028B84_PA_SU_POLY_OFFSET_FRONT_OFFSET, float_bits( gl.offset_units ));
	gnm_dev_set_context_reg( cmd, R_028B88_PA_SU_POLY_OFFSET_BACK_SCALE, float_bits( gl.offset_factor * 16.0f ));
	gnm_dev_set_context_reg( cmd, R_028B8C_PA_SU_POLY_OFFSET_BACK_OFFSET, float_bits( gl.offset_units ));

	sceGnmDrawCmdSetRenderTargetMask( cmd, gl.color_mask );
	emit_viewport( cmd );

	gl.dirty_state = false;
}

// command buffer was started again, nothing of our state is there
static void restart_cb( void )
{
	gl.dirty_state = true;
	gl.cur_vs = NULL;
	gl.cur_ps = NULL;
	gl.cur_prim = -1;
}

static void set_shaders( GnmCommandBuffer *cmd, const gnm_vs_t *vs, const gnm_ps_t *ps )
{
	if( gl.cur_vs == vs && gl.cur_ps == ps )
		return;

	if( gl.cur_vs != vs )
	{
		sceGnmDrawCmdSetVsShader( cmd, &vs->shader->registers, 0 );
		// psbc adds user SGPR 2 (base vertex) to gl_VertexIndex in every vertex shader,
		// user data registers keep old values, so it must be written explicitly
		gnm_dev_set_user_sgpr( cmd, false, VS_BASE_VERTEX_SGPR, 0 );
	}
	if( gl.cur_ps != ps )
		sceGnmDrawCmdSetPsShader( cmd, &ps->shader->registers );

	sceGnmDrawCmdSetPsInputUsage( cmd, sceGnmVsShaderExportSemanticTable( vs->shader ), vs->shader->numexportsemantics,
		sceGnmPsShaderInputSemanticTable( ps->shader ), ps->shader->numinputsemantics );

	gl.cur_vs = vs;
	gl.cur_ps = ps;
}

static void set_primitive( GnmCommandBuffer *cmd, int prim )
{
	if( gl.cur_prim == prim )
		return;

	sceGnmDrawCmdSetPrimitiveType( cmd, (GnmPrimitiveType)prim );
	gl.cur_prim = prim;
}

/*
==============================================================================

Textures

==============================================================================
*/
static gl_texture_t *get_texture( GLuint name, bool create )
{
	if( name == 0 )
		return NULL;

	if( (int)name >= gl.numtextures )
	{
		int num;

		if( !create )
			return NULL;

		num = ( name + 256 ) & ~255;
		gl.textures = realloc( gl.textures, num * sizeof( gl_texture_t ));
		memset( gl.textures + gl.numtextures, 0, ( num - gl.numtextures ) * sizeof( gl_texture_t ));
		gl.numtextures = num;
	}

	if( !gl.textures[name].exists )
	{
		gl_texture_t *t;

		if( !create )
			return NULL;

		t = &gl.textures[name];
		memset( t, 0, sizeof( *t ));
		t->exists = true;
		t->min_filter = GL_NEAREST_MIPMAP_LINEAR;
		t->mag_filter = 0x2601; // GL_LINEAR
		t->wrap_s = t->wrap_t = 0x2901; // GL_REPEAT
	}

	return &gl.textures[name];
}

static gl_texture_t *bound_texture( void )
{
	return get_texture( gl.bound[gl.active_unit], true );
}

static void gl_GenTextures( GLsizei n, GLuint *textures )
{
	int i;

	for( i = 0; i < n; i++ )
	{
		textures[i] = gl.next_texture++;
		get_texture( textures[i], true );
	}
}

static void free_texture_level( gl_texture_level_t *l )
{
	free( l->shadow );
	memset( l, 0, sizeof( *l ));
}

static void free_texture_storage( gl_texture_t *t )
{
	int i;

	gnm_dev_heap_free( t->gpu );
	t->gpu = NULL;
	for( i = 0; i < MAX_TEXTURE_LEVELS; i++ )
		free_texture_level( &t->levels[i] );
	t->width = t->height = 0;
	t->numlevels = 0;
	t->dirty = false;
}

static void gl_DeleteTextures( GLsizei n, const GLuint *textures )
{
	int i, u;

	for( i = 0; i < n; i++ )
	{
		gl_texture_t *t = get_texture( textures[i], false );

		if( !t )
			continue;

		free_texture_storage( t );
		t->exists = false;

		for( u = 0; u < MAX_TEXTURE_UNITS; u++ )
		{
			if( gl.bound[u] == textures[i] )
				gl.bound[u] = 0;
		}
	}
}

static GLboolean gl_IsTexture( GLuint texture )
{
	return get_texture( texture, false ) != NULL;
}

static void gl_BindTexture( GLenum target, GLuint texture )
{
	if( target != GL_TEXTURE_2D )
		return;

	gl.bound[gl.active_unit] = texture;
	get_texture( texture, true );
}

static void gl_ActiveTexture( GLenum unit )
{
	int u = (int)unit - GL_TEXTURE0;

	if( u >= 0 && u < MAX_TEXTURE_UNITS )
		gl.active_unit = u;
}

static void gl_TexParameteri( GLenum target, GLenum pname, GLint param )
{
	gl_texture_t *t = bound_texture();

	if( !t || target != GL_TEXTURE_2D )
		return;

	switch( pname )
	{
	case GL_TEXTURE_MIN_FILTER: t->min_filter = param; break;
	case GL_TEXTURE_MAG_FILTER: t->mag_filter = param; break;
	case GL_TEXTURE_WRAP_S: t->wrap_s = param; break;
	case GL_TEXTURE_WRAP_T: t->wrap_t = param; break;
	case GL_TEXTURE_MAX_ANISOTROPY_EXT: t->max_anisotropy = param; break;
	}
}

static void gl_TexParameterf( GLenum target, GLenum pname, GLfloat param )
{
	gl_TexParameteri( target, pname, (GLint)param );
}

static void gl_TexParameterfv( GLenum target, GLenum pname, const GLfloat *params )
{
	gl_TexParameteri( target, pname, (GLint)params[0] );
}

// converts a rectangle of client pixels to RGBA8
static bool convert_pixels( uint8_t *dst, int dststride, const uint8_t *src, int w, int h, GLenum format, GLenum type )
{
	int comps, srcstride, x, y;

	if( type != GL_UNSIGNED_BYTE )
		return false;

	switch( format )
	{
	case GL_RGBA: case GL_BGRA: comps = 4; break;
	case GL_RGB: case GL_BGR: comps = 3; break;
	case GL_LUMINANCE_ALPHA: comps = 2; break;
	case GL_ALPHA: case GL_LUMINANCE: comps = 1; break;
	default: return false;
	}

	srcstride = w * comps;
	srcstride = ( srcstride + gl.unpack_alignment - 1 ) & ~( gl.unpack_alignment - 1 );

	for( y = 0; y < h; y++ )
	{
		const uint8_t *s = src + y * srcstride;
		uint8_t *d = dst + y * dststride;

		if( format == GL_RGBA )
		{
			memcpy( d, s, w * 4 );
			continue;
		}

		for( x = 0; x < w; x++, s += comps, d += 4 )
		{
			switch( format )
			{
			case GL_BGRA: d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3]; break;
			case GL_RGB: d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255; break;
			case GL_BGR: d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = 255; break;
			case GL_LUMINANCE_ALPHA: d[0] = d[1] = d[2] = s[0]; d[3] = s[1]; break;
			case GL_LUMINANCE: d[0] = d[1] = d[2] = s[0]; d[3] = 255; break;
			case GL_ALPHA: d[0] = d[1] = d[2] = 255; d[3] = s[0]; break;
			}
		}
	}

	return true;
}

static uint32_t next_pow2( uint32_t v )
{
	uint32_t p = 1;

	while( p < v )
		p <<= 1;
	return p;
}

// levels uploaded with the expected sizes, mipmaps are used only when the chain
// reaches 1x1 without holes, like a complete texture in OpenGL
static int count_texture_levels( const gl_texture_t *t )
{
	int i, w = t->width, h = t->height;

	if( !gl.mipmaps )
		return 1;

	if( gl.mipmaps == 1 && ( next_pow2( w ) != (uint32_t)w || next_pow2( h ) != (uint32_t)h ))
		return 1;

	for( i = 1; i < MAX_TEXTURE_LEVELS; i++ )
	{
		if( w == 1 && h == 1 )
			break;

		w = w > 1 ? w >> 1 : 1;
		h = h > 1 ? h >> 1 : 1;

		if( !t->levels[i].shadow || t->levels[i].width != w || t->levels[i].height != h )
			return 1;
	}

	return i;
}

/*
creates GPU memory for the texture and fills it from the shadow copies

Linear aligned layout. Single level textures get rows aligned to 64 texels:
opengnm's 16 gave slanted lines in the 460x80 menu logo. Mipmapped textures
use opengnm's layout (pow2 padded levels, AddrLib rules), a texelFetch test on the
console showed the texture unit reads a 256x256 texture exactly like that,
my own guess with 64 texel rows on small levels didn't match. Other sizes
were checked by looking at maps and the menu.
*/
static bool upload_texture( gl_texture_t *t )
{
	GnmTextureCreateInfo ci;
	uint64_t size = 0;
	GnmError err;
	uint8_t *gpu;
	int i, y;
	int numlevels = count_texture_levels( t );

	memset( &ci, 0, sizeof( ci ));
	ci.format = GNM_FMT_R8G8B8A8_UNORM;
	ci.texturetype = GNM_TEXTURE_2D;
	ci.width = t->width;
	ci.height = t->height;
	ci.depth = 1;
	ci.nummiplevels = numlevels;
	ci.numslices = 1;
	ci.numfragments = 1;
	ci.tilemodehint = GNM_TM_DISPLAY_LINEAR_ALIGNED;
	ci.mingpumode = GNM_GPU_BASE;

	err = sceGnmCreateTexture( &t->desc, &ci );
	if( err != GNM_ERROR_OK )
	{
		GNM_LOG( "texture %dx%d, %d levels: %s\n", t->width, t->height, numlevels, sceGnmStrError( err ));
		return false;
	}

	if( numlevels == 1 )
	{
		t->levels[0].pitch = ( t->width + 63 ) & ~63;
		t->levels[0].offset = 0;
		sceGnmTexSetPitch( &t->desc, t->levels[0].pitch );
		size = (uint64_t)t->levels[0].pitch * t->height * 4;
	}
	else
	{
		const GpaTextureInfo info = sceGnmTexBuildInfo( &t->desc );

		for( i = 0; i < numlevels; i++ )
		{
			GpaTilingParams tp;
			GpaSurfaceInfo si;
			uint64_t lsize = 0, loffset = 0;

			memset( &si, 0, sizeof( si ));
			if( sceGpaTpInit( &tp, &info, i, 0 ) != GPA_ERR_OK || sceGpaComputeSurfaceInfo( &si, &tp ) != GPA_ERR_OK
				|| sceGpaComputeSurfaceSizeOffset( &lsize, &loffset, &info, i, 0 ) != GPA_ERR_OK || si.pitch < (uint32_t)t->levels[i].width )
			{
				GNM_LOG( "texture %dx%d: no layout for level %d\n", t->width, t->height, i );
				return false;
			}

			t->levels[i].pitch = si.pitch;
			t->levels[i].offset = loffset;
			if( loffset + lsize > size )
				size = loffset + lsize;
		}
	}

	gpu = gnm_dev_heap_alloc( size, 256 );
	if( !gpu )
		return false;

	for( i = 0; i < numlevels; i++ )
	{
		const gl_texture_level_t *l = &t->levels[i];

		for( y = 0; y < l->height; y++ )
			memcpy( gpu + l->offset + (size_t)y * l->pitch * 4, l->shadow + (size_t)y * l->width * 4, l->width * 4 );
	}

	gnm_dev_heap_free( t->gpu );
	t->gpu = gpu;
	t->numlevels = numlevels;
	t->dirty = false;
	sceGnmTexSetBaseAddress( &t->desc, gpu );
	t->used_serial = 0;
	return true;
}

static void gl_TexImage2D( GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels )
{
	gl_texture_t *t = bound_texture();
	gl_texture_level_t *l;

	if( !t || target != GL_TEXTURE_2D )
		return;

	if( level < 0 || level >= MAX_TEXTURE_LEVELS || width <= 0 || height <= 0 || width > 8192 || height > 8192 )
	{
		set_error( GL_INVALID_VALUE );
		return;
	}

	// level 0 defines the texture, other levels are checked against it when uploading
	if( level == 0 )
	{
		free_texture_storage( t );
		t->width = width;
		t->height = height;
	}

	l = &t->levels[level];
	free_texture_level( l );
	l->width = width;
	l->height = height;
	l->shadow = calloc( 1, (size_t)width * height * 4 );
	t->dirty = true;

	if( pixels && !convert_pixels( l->shadow, width * 4, pixels, width, height, format, type ))
	{
		static bool reported;
		if( !reported )
		{
			GNM_LOG( "glTexImage2D: unsupported format 0x%x type 0x%x\n", format, type );
			reported = true;
		}
	}

	if( gnm_dev.debug && level == 0 )
		GNM_LOG( "glTexImage2D: texture %u %dx%d, format 0x%x type 0x%x align %d, %s\n",
			gl.bound[gl.active_unit], width, height, format, type, gl.unpack_alignment,
			pixels ? "data" : "no data" );
}

static void gl_TexSubImage2D( GLenum target, GLint level, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels )
{
	gl_texture_t *t = bound_texture();
	gl_texture_level_t *l;
	int row;

	if( !t || target != GL_TEXTURE_2D || level < 0 || level >= MAX_TEXTURE_LEVELS || !pixels )
		return;

	l = &t->levels[level];
	if( !l->shadow )
		return;

	if( x < 0 || y < 0 || x + width > l->width || y + height > l->height )
	{
		set_error( GL_INVALID_VALUE );
		return;
	}

	if( gnm_dev.debug )
	{
		static int logged;
		if( logged < 64 || gnm_dev.debug >= 2 )
		{
			GNM_LOG( "glTexSubImage2D: texture %u level %d (%dx%d) rect %d,%d %dx%d, format 0x%x type 0x%x align %d, %s\n",
				gl.bound[gl.active_unit], level, l->width, l->height, x, y, width, height, format, type, gl.unpack_alignment,
				t->dirty || !t->gpu ? "pending upload" : t->used_serial == recording_serial() ? "in use, new memory" : "in place" );
			logged++;
		}
	}

	if( !convert_pixels( l->shadow + ((size_t)y * l->width + x ) * 4, l->width * 4, pixels, width, height, format, type ))
	{
		GNM_LOG( "glTexSubImage2D: unsupported format 0x%x type 0x%x\n", format, type );
		return;
	}

	// GPU still has to read the old contents in this submit, new memory is made
	// before the next draw that uses the texture
	if( t->dirty || !t->gpu || level >= t->numlevels || t->used_serial == recording_serial( ))
	{
		t->dirty = true;
		return;
	}

	for( row = 0; row < height; row++ )
	{
		memcpy( (uint8_t *)t->gpu + l->offset + ((size_t)( y + row ) * l->pitch + x ) * 4,
			l->shadow + ((size_t)( y + row ) * l->width + x ) * 4, width * 4 );
	}
}

static GnmSampler make_sampler( const gl_texture_t *t )
{
	GnmSampler s;
	bool min_linear = t->min_filter != GL_NEAREST && t->min_filter != GL_NEAREST_MIPMAP_NEAREST && t->min_filter != GL_NEAREST_MIPMAP_LINEAR;
	bool mag_linear = t->mag_filter != GL_NEAREST;

	memset( &s, 0, sizeof( s ));
	s.clampx = t->wrap_s == GL_CLAMP_TO_EDGE || t->wrap_s == GL_CLAMP ? GNM_TEX_CLAMP_CLAMP_LAST_TEXEL :
		t->wrap_s == GL_MIRRORED_REPEAT ? GNM_TEX_CLAMP_MIRROR : GNM_TEX_CLAMP_WRAP;
	s.clampy = t->wrap_t == GL_CLAMP_TO_EDGE || t->wrap_t == GL_CLAMP ? GNM_TEX_CLAMP_CLAMP_LAST_TEXEL :
		t->wrap_t == GL_MIRRORED_REPEAT ? GNM_TEX_CLAMP_MIRROR : GNM_TEX_CLAMP_WRAP;
	s.clampz = GNM_TEX_CLAMP_CLAMP_LAST_TEXEL;
	s.xyminfilter = min_linear ? GNM_FILTER_BILINEAR : GNM_FILTER_POINT;
	s.xymagfilter = mag_linear ? GNM_FILTER_BILINEAR : GNM_FILTER_POINT;
	switch( t->numlevels > 1 ? t->min_filter : 0 )
	{
	case GL_NEAREST_MIPMAP_NEAREST:
	case GL_LINEAR_MIPMAP_NEAREST:
		s.mipfilter = GNM_MIPFILTER_POINT;
		break;
	case GL_NEAREST_MIPMAP_LINEAR:
	case GL_LINEAR_MIPMAP_LINEAR:
		s.mipfilter = GNM_MIPFILTER_LINEAR;
		break;
	default:
		s.mipfilter = GNM_MIPFILTER_NONE; // level 0 only
		break;
	}
	s.zfilter = GNM_ZFILTER_NONE;
	s.maxlod = 0xfff;

	// anisotropic filtering works with linear filtering, ratio is a power of two up to 16
	if( t->max_anisotropy >= 2.0f && min_linear && mag_linear )
	{
		s.xyminfilter = s.xymagfilter = GNM_FILTER_ANISO_BILINEAR;
		s.maxanisoratio = t->max_anisotropy >= 16.0f ? 4 : t->max_anisotropy >= 8.0f ? 3 : t->max_anisotropy >= 4.0f ? 2 : 1;
	}

	return s;
}

/*
==============================================================================

Buffers

==============================================================================
*/
static gl_buffer_t *get_buffer( GLuint name, bool create )
{
	if( name == 0 )
		return NULL;

	if( (int)name >= gl.numbuffers )
	{
		int num;

		if( !create )
			return NULL;

		num = ( name + 64 ) & ~63;
		gl.buffers = realloc( gl.buffers, num * sizeof( gl_buffer_t ));
		memset( gl.buffers + gl.numbuffers, 0, ( num - gl.numbuffers ) * sizeof( gl_buffer_t ));
		gl.numbuffers = num;
	}

	if( !gl.buffers[name].exists && !create )
		return NULL;

	gl.buffers[name].exists = true;
	return &gl.buffers[name];
}

static GLuint *buffer_binding( GLenum target )
{
	switch( target )
	{
	case GL_ARRAY_BUFFER: return &gl.array_buffer;
	case GL_ELEMENT_ARRAY_BUFFER: return &gl.element_buffer;
	}
	return NULL;
}

static void gl_GenBuffers( GLsizei n, GLuint *buffers )
{
	int i;

	for( i = 0; i < n; i++ )
	{
		buffers[i] = gl.next_buffer++;
		get_buffer( buffers[i], true );
	}
}

static void gl_DeleteBuffers( GLsizei n, const GLuint *buffers )
{
	int i;

	for( i = 0; i < n; i++ )
	{
		gl_buffer_t *b = get_buffer( buffers[i], false );
		int a;

		if( !b )
			continue;

		free( b->data );
		memset( b, 0, sizeof( *b ));

		if( gl.array_buffer == buffers[i] ) gl.array_buffer = 0;
		if( gl.element_buffer == buffers[i] ) gl.element_buffer = 0;
		for( a = 0; a < MAX_ATTRIBS; a++ )
		{
			if( gl.attribs[a].buffer == buffers[i] )
				gl.attribs[a].buffer = 0;
		}
	}
}

static GLboolean gl_IsBuffer( GLuint buffer )
{
	return get_buffer( buffer, false ) != NULL;
}

static void gl_BindBuffer( GLenum target, GLuint buffer )
{
	GLuint *b = buffer_binding( target );

	if( b )
	{
		*b = buffer;
		get_buffer( buffer, true );
	}
}

// buffer contents live in CPU memory, draws copy what they use
static void gl_BufferData( GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage )
{
	GLuint *bind = buffer_binding( target );
	gl_buffer_t *b = bind ? get_buffer( *bind, false ) : NULL;

	if( !b || size < 0 )
		return;

	free( b->data );
	b->data = malloc( size ? size : 1 );
	b->size = size;
	if( data )
		memcpy( b->data, data, size );
}

static void gl_BufferSubData( GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid *data )
{
	GLuint *bind = buffer_binding( target );
	gl_buffer_t *b = bind ? get_buffer( *bind, false ) : NULL;

	if( !b || !b->data || offset < 0 || offset + size > (GLintptr)b->size )
		return;

	memcpy( b->data + offset, data, size );
}

/*
==============================================================================

Shaders and programs

==============================================================================
*/
static gl_object_t *get_object( GLuint name, int kind )
{
	if( name == 0 || (int)name >= gl.numobjects || (int)gl.objects[name].kind != kind )
		return NULL;
	return &gl.objects[name];
}

static GLuint new_object( int kind )
{
	int i;

	for( i = 1; i < gl.numobjects; i++ )
	{
		if( gl.objects[i].kind == OBJ_FREE )
			break;
	}

	if( i >= gl.numobjects )
	{
		int num = gl.numobjects ? gl.numobjects * 2 : 64;
		gl.objects = realloc( gl.objects, num * sizeof( gl_object_t ));
		memset( gl.objects + gl.numobjects, 0, ( num - gl.numobjects ) * sizeof( gl_object_t ));
		if( !gl.numobjects )
			i = 1;
		gl.numobjects = num;
	}

	memset( &gl.objects[i], 0, sizeof( gl.objects[i] ));
	gl.objects[i].kind = kind;
	return i;
}

static GLuint gl_CreateShader( GLenum type )
{
	GLuint id = new_object( OBJ_SHADER );
	gl.objects[id].type = type;
	return id;
}

static int define_value( const char *src, const char *name )
{
	char key[64];
	const char *p;

	snprintf( key, sizeof( key ), "#define %s ", name );
	p = strstr( src, key );
	return p ? atoi( p + strlen( key )) : 0;
}

static void gl_ShaderSource( GLuint shader, GLsizei count, const GLchar *const *strings, const GLint *lengths )
{
	gl_object_t *o = get_object( shader, OBJ_SHADER );
	size_t total = 0;
	char *src;
	int i;

	if( !o )
		return;

	for( i = 0; i < count; i++ )
		total += lengths && lengths[i] >= 0 ? (size_t)lengths[i] : strlen( strings[i] );

	src = malloc( total + 1 );
	total = 0;
	for( i = 0; i < count; i++ )
	{
		size_t len = lengths && lengths[i] >= 0 ? (size_t)lengths[i] : strlen( strings[i] );
		memcpy( src + total, strings[i], len );
		total += len;
	}
	src[total] = 0;

	// only gl2_shim shaders are known: features come from its defines
	o->flags = 0;
	if( define_value( src, "ATTR_COLOR" )) o->flags |= GNM_GL2_COLOR;
	if( define_value( src, "ATTR_TEXCOORD0" )) o->flags |= GNM_GL2_TEXCOORD0;
	if( define_value( src, "ATTR_TEXCOORD1" )) o->flags |= GNM_GL2_TEXCOORD1;
	if( define_value( src, "FEAT_ALPHA_TEST" )) o->flags |= GNM_GL2_ALPHA_TEST;
	if( define_value( src, "FEAT_FOG" )) o->flags |= GNM_GL2_FOG;
	o->compiled = strstr( src, "#define ATTR_POSITION" ) != NULL;

	if( !o->compiled )
		GNM_LOG( "glShaderSource: not a gl2_shim shader\n" );

	free( src );
}

static void gl_CompileShader( GLuint shader )
{
}

static GLuint gl_CreateProgram( void )
{
	GLuint id = new_object( OBJ_PROGRAM );
	gl_object_t *p = &gl.objects[id];

	p->vsflags = p->psflags = -1;
	p->uniforms_mvp[0] = p->uniforms_mvp[5] = p->uniforms_mvp[10] = p->uniforms_mvp[15] = 1.0f;
	p->uniforms_color[0] = p->uniforms_color[1] = p->uniforms_color[2] = p->uniforms_color[3] = 1.0f;
	return id;
}

static void gl_AttachShader( GLuint program, GLuint shader )
{
	gl_object_t *p = get_object( program, OBJ_PROGRAM );
	gl_object_t *s = get_object( shader, OBJ_SHADER );

	if( !p || !s || !s->compiled )
		return;

	if( s->type == GL_VERTEX_SHADER )
		p->vsflags = s->flags;
	else if( s->type == GL_FRAGMENT_SHADER )
		p->psflags = s->flags;
}

static void gl_DetachShader( GLuint program, GLuint shader )
{
}

static void gl_LinkProgram( GLuint program )
{
	gl_object_t *p = get_object( program, OBJ_PROGRAM );

	if( !p )
		return;

	p->linked = p->vsflags >= 0 && p->psflags >= 0;
	if( !p->linked )
		GNM_LOG( "glLinkProgram: program %u has no gl2_shim shaders\n", program );
}

static void gl_DeleteObject( GLuint obj )
{
	if( obj && (int)obj < gl.numobjects )
	{
		if( gl.program == obj )
			gl.program = 0;
		gl.objects[obj].kind = OBJ_FREE;
	}
}

static void gl_UseProgram( GLuint program )
{
	gl.program = program;
}

static void gl_BindAttribLocation( GLuint program, GLuint index, const GLchar *name )
{
	// attributes are always numbered in gl2_shim order
}

static void gl_ValidateProgram( GLuint program )
{
}

static void gl_GetObjectParameteriv( GLuint obj, GLenum pname, GLint *params )
{
	gl_object_t *o = obj && (int)obj < gl.numobjects ? &gl.objects[obj] : NULL;

	if( !o || o->kind == OBJ_FREE )
	{
		*params = 0;
		return;
	}

	switch( pname )
	{
	case GL_COMPILE_STATUS: *params = o->kind == OBJ_SHADER && o->compiled; break;
	case GL_LINK_STATUS: *params = o->kind == OBJ_PROGRAM && o->linked; break;
	case GL_VALIDATE_STATUS: *params = 1; break;
	case GL_INFO_LOG_LENGTH: *params = 1; break;
	case GL_SHADER_TYPE: *params = o->type; break;
	default: *params = 0; break;
	}
}

static void gl_GetInfoLog( GLuint obj, GLsizei maxlength, GLsizei *length, GLchar *log )
{
	if( maxlength > 0 )
		log[0] = 0;
	if( length )
		*length = 0;
}

static GLint gl_GetUniformLocation( GLuint program, const GLchar *name )
{
	int i;

	for( i = 0; i < U_COUNT; i++ )
	{
		if( !strcmp( name, uniform_names[i] ))
			return i;
	}

	return -1;
}

static GLint gl_GetAttribLocation( GLuint program, const GLchar *name )
{
	return -1;
}

static gl_object_t *current_program( void )
{
	return get_object( gl.program, OBJ_PROGRAM );
}

static void gl_Uniform1i( GLint location, GLint v0 )
{
	// samplers: uTex0 is always unit 0, uTex1 is unit 1
}

static void gl_Uniform1f( GLint location, GLfloat v0 )
{
	gl_object_t *p = current_program();

	if( p && location == U_ALPHA )
		p->uniform_alpha = v0;
}

static void gl_Uniform4fv( GLint location, GLsizei count, const GLfloat *v )
{
	gl_object_t *p = current_program();

	if( !p || count < 1 )
		return;

	if( location == U_COLOR )
		memcpy( p->uniforms_color, v, sizeof( p->uniforms_color ));
	else if( location == U_FOG )
		memcpy( p->uniforms_fog, v, sizeof( p->uniforms_fog ));
}

static void gl_Uniform4f( GLint location, GLfloat x, GLfloat y, GLfloat z, GLfloat w )
{
	const GLfloat v[4] = { x, y, z, w };
	gl_Uniform4fv( location, 1, v );
}

static void gl_UniformMatrix4fv( GLint location, GLsizei count, GLboolean transpose, const GLfloat *v )
{
	gl_object_t *p = current_program();
	int r, c;

	if( !p || count < 1 || location != U_MVP )
		return;

	if( !transpose )
	{
		memcpy( p->uniforms_mvp, v, sizeof( p->uniforms_mvp ));
		return;
	}

	for( r = 0; r < 4; r++ )
		for( c = 0; c < 4; c++ )
			p->uniforms_mvp[c * 4 + r] = v[r * 4 + c];
}

/*
==============================================================================

Vertex attributes

==============================================================================
*/
static void gl_EnableVertexAttribArray( GLuint index )
{
	if( index < MAX_ATTRIBS )
		gl.attribs[index].enabled = true;
}

static void gl_DisableVertexAttribArray( GLuint index )
{
	if( index < MAX_ATTRIBS )
		gl.attribs[index].enabled = false;
}

static void gl_VertexAttribPointer( GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid *pointer )
{
	gl_attrib_t *a;

	if( index >= MAX_ATTRIBS )
		return;

	a = &gl.attribs[index];
	a->size = size;
	a->type = type;
	a->stride = stride;
	a->pointer = pointer;
	a->buffer = gl.array_buffer;
}

/*
==============================================================================

Drawing

==============================================================================
*/
typedef struct
{
	const uint8_t *src; // element of the first vertex used by the draw
	int stride;         // bytes between vertices, 0 for a constant
	int elemsize;
	int comps;
	int type;           // 0 - float, 1 - normalized unsigned bytes
	uint32_t bytes;     // to copy
} attr_source_t;

static bool attrib_source( int index, int vmin, int numverts, attr_source_t *out )
{
	static const float default_attrib[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	const gl_attrib_t *a = &gl.attribs[index];
	const uint8_t *base;
	int typesize;

	if( !a->enabled )
	{
		// shouldn't happen with gl2_shim, keep the draw going with white
		out->src = (const uint8_t *)default_attrib;
		out->stride = 0;
		out->comps = 4;
		out->type = 0;
		out->elemsize = 16;
		out->bytes = 16;
		return true;
	}

	switch( a->type )
	{
	case GL_FLOAT: typesize = 4; out->type = 0; break;
	case GL_UNSIGNED_BYTE: typesize = 1; out->type = 1; break;
	default:
	{
		static bool reported;
		if( !reported )
		{
			GNM_LOG( "vertex attribute type 0x%x isn't supported\n", a->type );
			reported = true;
		}
		return false;
	}
	}

	if( a->buffer )
	{
		const gl_buffer_t *b = get_buffer( a->buffer, false );
		if( !b || !b->data )
			return false;
		base = b->data + (uintptr_t)a->pointer;
	}
	else base = a->pointer;

	if( !base )
		return false;

	out->comps = a->size;
	out->elemsize = a->size * typesize;
	out->stride = a->stride ? a->stride : out->elemsize;
	out->src = base + (size_t)vmin * out->stride;
	out->bytes = (uint32_t)(( numverts - 1 ) * out->stride + out->elemsize );
	return true;
}

static int primitive_type( GLenum mode )
{
	switch( mode )
	{
	case GL_POINTS: return GNM_PT_POINTLIST;
	case GL_LINES: return GNM_PT_LINELIST;
	case GL_LINE_LOOP: return GNM_PT_LINELOOP;
	case GL_LINE_STRIP: return GNM_PT_LINESTRIP;
	case GL_TRIANGLES: return GNM_PT_TRILIST;
	case GL_TRIANGLE_STRIP: return GNM_PT_TRISTRIP;
	case GL_TRIANGLE_FAN: return GNM_PT_TRIFAN;
	case GL_QUADS: return GNM_PT_QUADLIST;
	case GL_POLYGON: return GNM_PT_TRIFAN;
	}
	return -1;
}

static const gl_texture_t *usable_texture( int unit )
{
	gl_texture_t *t = get_texture( gl.bound[unit], false );

	if( t && t->dirty && t->levels[0].shadow && !upload_texture( t ))
		t->dirty = false; // out of memory, keep the old contents
	if( !t || !t->gpu )
		return &gl.white;

	t->used_serial = recording_serial();
	return t;
}

// DRAW_INDEX_2 like Mesa radeonsi and opengnm's generic backend: opengnm's
// sceGnmDrawCmdDrawIndex calls the firmware helper, the only draw that does,
// and the first indexed draw of a map hung the GPU on FW 13.02
static void emit_draw_index( GnmCommandBuffer *cmd, uint32_t count, const void *indices )
{
	const uint64_t addr = (uint64_t)(uintptr_t)indices;

	if( cmd->cmdptr + 6 > cmd->endptr )
		return;

	cmd->cmdptr[0] = PKT3( PKT3_DRAW_INDEX_2, 4, 0 );
	cmd->cmdptr[1] = count; // max index count in the buffer
	cmd->cmdptr[2] = (uint32_t)addr & ~1u;
	cmd->cmdptr[3] = (uint32_t)( addr >> 32 );
	cmd->cmdptr[4] = count;
	cmd->cmdptr[5] = 0; // VGT_DRAW_INITIATOR: source select DMA
	cmd->cmdptr += 6;
}

static void draw_dropped( const char *reason )
{
	static int s_count;

	if( s_count < 16 )
	{
		GNM_LOG( "draw dropped: %s\n", reason );
		s_count++;
	}
}

// indexed: glDrawElements family, indices are a pointer or an offset into the element buffer;
// otherwise glDrawArrays and first is the first vertex
static void draw( GLenum mode, GLint first, GLsizei count, bool indexed, GLenum itype, const GLvoid *indices, GLint basevertex )
{
	gl_object_t *p = current_program();
	attr_source_t attrs[SEM_COUNT];
	const uint8_t *isrc = NULL;
	int vmin, vmax, numverts, prim, vsbits, psbits, sem, k, i;
	int semantics[SEM_COUNT], numsems = 0;
	uint32_t transient, tables, ibytes = 0;
	const gnm_vs_t *vs;
	const gnm_ps_t *ps;
	GnmCommandBuffer *cmd;
	bool idx32 = false;

	if( count <= 0 )
		return;

	if( !p || !p->linked )
	{
		static bool reported;
		if( !reported )
		{
			GNM_LOG( "draw without gl2_shim program\n" );
			reported = true;
		}
		return;
	}

	prim = primitive_type( mode );
	if( prim < 0 )
	{
		draw_dropped( "unsupported primitive" );
		return;
	}

	// vertex range used by the draw
	if( indexed )
	{
		const gl_buffer_t *eb = get_buffer( gl.element_buffer, false );
		int isize = itype == GL_UNSIGNED_INT ? 4 : itype == GL_UNSIGNED_SHORT ? 2 : 1;

		if( eb )
		{
			if( !eb->data || (uintptr_t)indices + (size_t)count * isize > eb->size )
			{
				draw_dropped( "indices are out of element buffer" );
				return;
			}
			isrc = eb->data + (uintptr_t)indices;
		}
		else isrc = indices;

		if( !isrc )
		{
			draw_dropped( "no indices" );
			return;
		}

		vmin = 0x7fffffff;
		vmax = -1;
		for( i = 0; i < count; i++ )
		{
			int v = isize == 4 ? (int)((const uint32_t *)isrc)[i] : isize == 2 ? ((const uint16_t *)isrc)[i] : isrc[i];
			if( v < vmin ) vmin = v;
			if( v > vmax ) vmax = v;
		}

		vmin += basevertex;
		vmax += basevertex;
		ibytes = count * 4;
	}
	else
	{
		vmin = first;
		vmax = first + count - 1;
	}

	numverts = vmax - vmin + 1;
	if( vmin < 0 || numverts <= 0 || numverts > MAX_VERTS )
	{
		draw_dropped( "vertex range is out of limits" );
		return;
	}

	idx32 = numverts > 65535;

	// attributes the program reads, in gl2_shim order: attribute k is the k-th of them
	vsbits = p->vsflags & ( GNM_GL2_COLOR | GNM_GL2_TEXCOORD0 | GNM_GL2_TEXCOORD1 );
	psbits = p->psflags & ( GNM_GL2_COLOR | GNM_GL2_TEXCOORD0 | GNM_GL2_TEXCOORD1 | GNM_GL2_ALPHA_TEST | GNM_GL2_FOG );
	semantics[numsems++] = SEM_POS;
	if( vsbits & GNM_GL2_COLOR ) semantics[numsems++] = SEM_COLOR;
	if( vsbits & GNM_GL2_TEXCOORD0 ) semantics[numsems++] = SEM_TC0;
	if( vsbits & GNM_GL2_TEXCOORD1 ) semantics[numsems++] = SEM_TC1;

	transient = 144 + 48 + ibytes;
	for( k = 0; k < numsems; k++ )
	{
		if( !attrib_source( k, vmin, numverts, &attrs[k] ))
		{
			draw_dropped( "vertex attribute can't be read" );
			return;
		}
		transient += (( attrs[k].bytes + 15 ) & ~15u ) + 16;
	}
	tables = VS_TABLE_SIZE + PS_TABLE_SIZE + 32;

	cmd = gnm_dev_reserve( 1024, tables, transient );

	if( gnm_dev.debug )
	{
		const gl_texture_t *t0 = get_texture( gl.bound[0], false ), *t1 = get_texture( gl.bound[1], false );

		snprintf( gnm_dev.last_draw, sizeof( gnm_dev.last_draw ),
			"draw %u: mode 0x%x count %d %s verts %d..%d, vs %d ps %d, attribs %d, tex0 %u (%dx%d) tex1 %u (%dx%d), "
			"blend %d depth %d/%d cull %d",
			gnm_dev.submit_draws, mode, count, indexed ? ( idx32 ? "idx32" : "idx16" ) : "arrays", vmin, vmax, vsbits, psbits, numsems,
			gl.bound[0], t0 ? t0->width : 0, t0 ? t0->height : 0, gl.bound[1], t1 ? t1->width : 0, t1 ? t1->height : 0,
			gl.blend, gl.depth_test, gl.depth_mask, gl.cull );
		if( gnm_dev.debug == 2 )
			GNM_LOG( "%s\n", gnm_dev.last_draw );
	}
	gnm_dev.submit_draws++;

	if( gl.dirty_state )
		emit_state( cmd );

	vs = &gl.vs[vsbits];
	ps = &gl.ps[psbits];
	set_shaders( cmd, vs, ps );

	// vertex shader: uniforms and one buffer per attribute
	{
		uint8_t *vsu = gnm_dev_alloc_transient( 144, 16 );
		uint8_t *table = gnm_dev_alloc_table( VS_TABLE_SIZE );
		int32_t *ai = (int32_t *)( vsu + 64 );
		GnmBuffer b;

		memcpy( vsu, p->uniforms_mvp, 64 );
		memset( vsu + 64, 0, 80 );

		for( k = 0; k < numsems; k++ )
		{
			uint8_t *data = gnm_dev_alloc_transient( attrs[k].bytes + 16, 16 );

			memcpy( data, attrs[k].src, attrs[k].bytes );
			memset( data + attrs[k].bytes, 0, 16 ); // unaligned byte reads go a bit past the end

			sem = semantics[k];
			ai[sem * 4 + 0] = 0;
			ai[sem * 4 + 1] = attrs[k].stride;
			ai[sem * 4 + 2] = attrs[k].comps;
			ai[sem * 4 + 3] = attrs[k].type;

			b = sceGnmCreateConstBuffer( data, attrs[k].bytes + 16 );
			b.stride = 0;
			b.numrecords = attrs[k].bytes + 16;
			memcpy( table + BINDING( 1 + sem ), &b, 16 );
		}
		// misc.x - base vertex, data starts at vmin and indices are rebased

		b = sceGnmCreateConstBuffer( vsu, 144 );
		b.stride = 0;
		b.numrecords = 144;
		memcpy( table, &b, 16 );
		gnm_dev_set_user_sgpr( cmd, false, vs->table_reg, (uint32_t)(uintptr_t)table );
	}

	// pixel shader: uniforms and textures
	{
		float *psu = gnm_dev_alloc_transient( 48, 16 );
		uint8_t *table = gnm_dev_alloc_table( PS_TABLE_SIZE );
		GnmBuffer b;
		int u;

		memcpy( psu, p->uniforms_color, 16 );
		memcpy( psu + 4, p->uniforms_fog, 16 );
		psu[8] = p->uniform_alpha;
		psu[9] = psu[10] = psu[11] = 0.0f;

		b = sceGnmCreateConstBuffer( psu, 48 );
		b.stride = 0;
		b.numrecords = 48;
		memcpy( table, &b, 16 );

		for( u = 0; u < MAX_TEXTURE_UNITS; u++ )
		{
			const gl_texture_t *t;
			GnmSampler s;

			if( !( psbits & ( u ? GNM_GL2_TEXCOORD1 : GNM_GL2_TEXCOORD0 )))
				continue;

			t = usable_texture( u );
			s = make_sampler( t );
			// combined image sampler: T# and S# at +32
			memcpy( table + BINDING( 1 + u ), &t->desc, 32 );
			memcpy( table + BINDING( 1 + u ) + 32, &s, 16 );
		}

		gnm_dev_set_user_sgpr( cmd, true, ps->table_reg, (uint32_t)(uintptr_t)table );
	}

	set_primitive( cmd, prim );

	if( isrc )
	{
		int isize = itype == GL_UNSIGNED_INT ? 4 : itype == GL_UNSIGNED_SHORT ? 2 : 1;
		void *idata = gnm_dev_alloc_transient( idx32 ? count * 4 : count * 2, 16 );
		const int rebase = basevertex - vmin;

		for( i = 0; i < count; i++ )
		{
			int v = isize == 4 ? (int)((const uint32_t *)isrc)[i] : isize == 2 ? ((const uint16_t *)isrc)[i] : isrc[i];
			v += rebase;
			if( idx32 ) ((uint32_t *)idata)[i] = v;
			else ((uint16_t *)idata)[i] = (uint16_t)v;
		}

		sceGnmDrawCmdSetIndexSize( cmd, idx32 ? GNM_INDEX_32 : GNM_INDEX_16, GNM_POLICY_LRU );
		emit_draw_index( cmd, count, idata );
	}
	else
	{
		sceGnmDrawCmdDrawIndexAuto( cmd, count );
	}

	gl.draws++;
	gl.verts += count;

	// -gnmdebug 3: every draw is its own submit, a hang report names the exact draw
	if( gnm_dev.debug >= 3 )
		gnm_dev_flush( "debug" );
}

static void gl_DrawArrays( GLenum mode, GLint first, GLsizei count )
{
	draw( mode, first, count, false, 0, NULL, 0 );
}

static void gl_DrawElements( GLenum mode, GLsizei count, GLenum type, const GLvoid *indices )
{
	draw( mode, 0, count, true, type, indices, 0 );
}

static void gl_DrawRangeElements( GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid *indices )
{
	draw( mode, 0, count, true, type, indices, 0 );
}

static void gl_DrawRangeElementsBaseVertex( GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid *indices, GLint basevertex )
{
	draw( mode, 0, count, true, type, indices, basevertex );
}

static void gl_DrawElementsBaseVertex( GLenum mode, GLsizei count, GLenum type, const GLvoid *indices, GLint basevertex )
{
	draw( mode, 0, count, true, type, indices, basevertex );
}

// fullscreen triangle, clipped by the scissor box like in OpenGL
static void gl_Clear( GLbitfield mask )
{
	const bool color = ( mask & GL_COLOR_BUFFER_BIT ) && gl.color_mask;
	const bool depth = ( mask & GL_DEPTH_BUFFER_BIT ) && gl.depth_mask;
	const int saved_viewport[4] = { gl.viewport[0], gl.viewport[1], gl.viewport[2], gl.viewport[3] };
	GnmCommandBuffer *cmd;
	GnmBlendControl blend;
	GnmDepthStencilControl ds;
	GnmDbRenderControl db;
	GnmPrimitiveSetup ps;
	float *colordata;
	uint8_t *table;
	GnmBuffer b;

	if( !color && !depth )
		return;

	cmd = gnm_dev_reserve( 1024, 64, 64 );

	if( gnm_dev.debug )
	{
		snprintf( gnm_dev.last_draw, sizeof( gnm_dev.last_draw ), "clear %u: color %d depth %d (%.2f), scissor %d",
			gnm_dev.submit_draws, color, depth, gl.clear_depth, gl.scissor_test );
		if( gnm_dev.debug == 2 )
			GNM_LOG( "%s\n", gnm_dev.last_draw );
	}
	gnm_dev.submit_draws++;

	// state for the clear, then everything goes back on the next draw
	gl.viewport[0] = gl.viewport[1] = 0;
	gl.viewport[2] = gnm_dev.width;
	gl.viewport[3] = gnm_dev.height;
	emit_viewport( cmd );
	memcpy( gl.viewport, saved_viewport, sizeof( gl.viewport ));
	gl.dirty_state = true;

	memset( &blend, 0, sizeof( blend ));
	blend.colorsrcmult = blend.alphasrcmult = GNM_BLEND_ONE;
	blend.colordstmult = blend.alphadstmult = GNM_BLEND_ZERO;
	sceGnmDrawCmdSetBlendControl( cmd, 0, &blend );

	memset( &ps, 0, sizeof( ps ));
	ps.cullmode = GNM_CULL_NONE;
	ps.frontmode = ps.backmode = GNM_FILL_SOLID;
	sceGnmDrawCmdSetPrimitiveSetup( cmd, &ps );

	// depth block writes the clear value for every covered pixel
	memset( &db, 0, sizeof( db ));
	db.depthclearenable = depth;
	sceGnmDrawCmdSetDbRenderControl( cmd, &db );
	sceGnmDrawCmdSetDepthClearValue( cmd, gl.clear_depth );

	memset( &ds, 0, sizeof( ds ));
	ds.depthenable = depth;
	ds.zwrite = depth;
	ds.zfunc = GNM_DEPTH_COMPARE_ALWAYS;
	ds.stencilfunc = ds.stencilbackfunc = GNM_DEPTH_COMPARE_ALWAYS;
	sceGnmDrawCmdSetDepthStencilControl( cmd, &ds );

	sceGnmDrawCmdSetRenderTargetMask( cmd, color ? gl.color_mask : 0 );

	set_shaders( cmd, &gl.clear_vs, &gl.clear_ps );

	colordata = gnm_dev_alloc_transient( 16, 16 );
	memcpy( colordata, gl.clear_color, 16 );
	table = gnm_dev_alloc_table( 16 );
	b = sceGnmCreateConstBuffer( colordata, 16 );
	b.stride = 0;
	b.numrecords = 16;
	memcpy( table, &b, 16 );
	gnm_dev_set_user_sgpr( cmd, true, gl.clear_ps.table_reg, (uint32_t)(uintptr_t)table );

	set_primitive( cmd, GNM_PT_TRILIST );
	sceGnmDrawCmdDrawIndexAuto( cmd, 3 );

	// following draws must not see stale depth or color
	sceGnmDrawCmdWaitGraphicsWrite( cmd, GNM_ACQUIRE_TARGET_CB0 | GNM_ACQUIRE_TARGET_DB );
	sceGnmDrawCmdSetDbRenderControl( cmd, &(GnmDbRenderControl){ 0 } );

	if( gnm_dev.debug >= 3 )
		gnm_dev_flush( "debug" );
}

/*
reads the color target being rendered: everything recorded so far is submitted
and the tiled surface is read on the CPU. Inside an 8x8 micro tile 32-bit pixels
always have the same layout (checked with opengnm for the whole display target),
so the tiling math runs once per micro tile.
*/
static void gl_ReadPixels( GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels )
{
	static GpaSurfaceContext ctx;
	static const GnmRenderTarget *ctx_rt;
	static uint32_t pattern[64];
	const GnmRenderTarget *rt = &gnm_dev.color[gnm_dev.back];
	const uint8_t *mem = sceGnmRtGetBaseAddr( rt );
	const int comps = format == GL_RGB ? 3 : 4;
	size_t stride;
	int row, col;

	if( !pixels || width <= 0 || height <= 0 )
		return;

	if( type != GL_UNSIGNED_BYTE || ( format != GL_RGB && format != GL_RGBA ))
	{
		set_error( GL_INVALID_ENUM );
		return;
	}

	stride = ( (size_t)width * comps + gl.pack_alignment - 1 ) & ~(size_t)( gl.pack_alignment - 1 );
	memset( pixels, 0, stride * height );

	if( gnm_dev.hung )
		return;

	gnm_dev_flush( "glReadPixels" );

	if( ctx_rt != rt )
	{
		const GpaTextureInfo info = sceGnmRtBuildInfo( rt );
		GpaTilingParams tp;
		uint64_t size = 0, base = 0, off;
		uint32_t align = 0;
		int i;

		if( sceGnmRtCalcByteSize( &size, &align, rt ) != GNM_ERROR_OK || sceGpaTpInit( &tp, &info, 0, 0 ) != GPA_ERR_OK )
		{
			GNM_LOG( "glReadPixels: no tiling info for the color target\n" );
			return;
		}

		tp.surfaceflags.display = 1;
		if( sceGpaInitSurfaceContext( &ctx, size, &tp ) != GPA_ERR_OK )
		{
			GNM_LOG( "glReadPixels: no surface context for the color target\n" );
			return;
		}

		sceGpaComputeSurfaceCoord( &base, NULL, &ctx, 0, 0, 0, 0 );
		for( i = 0; i < 64; i++ )
		{
			sceGpaComputeSurfaceCoord( &off, NULL, &ctx, i & 7, i >> 3, 0, 0 );
			pattern[i] = (uint32_t)( off - base );
		}

		ctx_rt = rt;
	}

	for( row = 0; row < height; row++ )
	{
		// OpenGL rows go from the bottom
		const int sy = gnm_dev.height - 1 - ( y + row );
		uint8_t *dst = (uint8_t *)pixels + stride * row;
		uint64_t tile = 0;
		int tile_x = -1;

		if( sy < 0 || sy >= gnm_dev.height )
			continue;

		for( col = 0; col < width; col++, dst += comps )
		{
			const int sx = x + col;
			const uint8_t *src;

			if( sx < 0 || sx >= gnm_dev.width )
				continue;

			if(( sx & ~7 ) != tile_x )
			{
				tile_x = sx & ~7;
				sceGpaComputeSurfaceCoord( &tile, NULL, &ctx, tile_x, sy & ~7, 0, 0 );
			}

			src = mem + tile + pattern[( sy & 7 ) * 8 + ( sx & 7 )];
			memcpy( dst, src, comps );
		}
	}
}

/*
==============================================================================

Function table

==============================================================================
*/
typedef struct
{
	const char *name;
	void *func;
} gl_proc_t;

#define P( name, func ) { name, (void *)func }
static const gl_proc_t procs[] =
{
	P( "glGetError", gl_GetError ),
	P( "glGetString", gl_GetString ),
	P( "glGetIntegerv", gl_GetIntegerv ),
	P( "glGetFloatv", gl_GetFloatv ),
	P( "glGetBooleanv", gl_GetBooleanv ),
	P( "glFinish", gl_Finish ),
	P( "glFlush", gl_Flush ),
	P( "glHint", gl_Hint ),
	P( "glPixelStorei", gl_PixelStorei ),
	P( "glPixelStoref", gl_PixelStoref ),

	P( "glEnable", gl_Enable ),
	P( "glDisable", gl_Disable ),
	P( "glIsEnabled", gl_IsEnabled ),
	P( "glBlendFunc", gl_BlendFunc ),
	P( "glDepthFunc", gl_DepthFunc ),
	P( "glDepthMask", gl_DepthMask ),
	P( "glDepthRange", gl_DepthRange ),
	P( "glDepthRangef", gl_DepthRangef ),
	P( "glColorMask", gl_ColorMask ),
	P( "glCullFace", gl_CullFace ),
	P( "glFrontFace", gl_FrontFace ),
	P( "glPolygonOffset", gl_PolygonOffset ),
	P( "glViewport", gl_Viewport ),
	P( "glScissor", gl_Scissor ),
	P( "glClearColor", gl_ClearColor ),
	P( "glClearDepth", gl_ClearDepth ),
	P( "glClearDepthf", gl_ClearDepthf ),
	P( "glClear", gl_Clear ),
	P( "glStencilFunc", gl_StencilFunc ),
	P( "glStencilOp", gl_StencilOp ),
	P( "glStencilMask", gl_StencilMask ),
	P( "glClearStencil", gl_ClearStencil ),
	P( "glDrawBuffer", gl_noop_GLenum ),
	P( "glReadBuffer", gl_noop_GLenum ),
	P( "glShadeModel", gl_noop_GLenum ),
	P( "glLineWidth", gl_noop_GLfloat ),
	P( "glPointSize", gl_noop_GLfloat ),
	P( "glPolygonMode", gl_noop_GLenum_GLenum ),
	P( "glReadPixels", gl_ReadPixels ),

	P( "glGenTextures", gl_GenTextures ),
	P( "glDeleteTextures", gl_DeleteTextures ),
	P( "glIsTexture", gl_IsTexture ),
	P( "glBindTexture", gl_BindTexture ),
	P( "glActiveTexture", gl_ActiveTexture ),
	P( "glActiveTextureARB", gl_ActiveTexture ),
	P( "glTexParameteri", gl_TexParameteri ),
	P( "glTexParameterf", gl_TexParameterf ),
	P( "glTexParameterfv", gl_TexParameterfv ),
	P( "glTexImage2D", gl_TexImage2D ),
	P( "glTexSubImage2D", gl_TexSubImage2D ),

	P( "glGenBuffers", gl_GenBuffers ),
	P( "glGenBuffersARB", gl_GenBuffers ),
	P( "glDeleteBuffers", gl_DeleteBuffers ),
	P( "glDeleteBuffersARB", gl_DeleteBuffers ),
	P( "glIsBuffer", gl_IsBuffer ),
	P( "glIsBufferARB", gl_IsBuffer ),
	P( "glBindBuffer", gl_BindBuffer ),
	P( "glBindBufferARB", gl_BindBuffer ),
	P( "glBufferData", gl_BufferData ),
	P( "glBufferDataARB", gl_BufferData ),
	P( "glBufferSubData", gl_BufferSubData ),
	P( "glBufferSubDataARB", gl_BufferSubData ),

	P( "glCreateShader", gl_CreateShader ),
	P( "glCreateShaderObjectARB", gl_CreateShader ),
	P( "glShaderSource", gl_ShaderSource ),
	P( "glShaderSourceARB", gl_ShaderSource ),
	P( "glCompileShader", gl_CompileShader ),
	P( "glCompileShaderARB", gl_CompileShader ),
	P( "glCreateProgram", gl_CreateProgram ),
	P( "glCreateProgramObjectARB", gl_CreateProgram ),
	P( "glAttachShader", gl_AttachShader ),
	P( "glAttachObjectARB", gl_AttachShader ),
	P( "glDetachShader", gl_DetachShader ),
	P( "glDetachObjectARB", gl_DetachShader ),
	P( "glLinkProgram", gl_LinkProgram ),
	P( "glLinkProgramARB", gl_LinkProgram ),
	P( "glDeleteShader", gl_DeleteObject ),
	P( "glDeleteProgram", gl_DeleteObject ),
	P( "glDeleteObjectARB", gl_DeleteObject ),
	P( "glUseProgram", gl_UseProgram ),
	P( "glUseProgramObjectARB", gl_UseProgram ),
	P( "glValidateProgram", gl_ValidateProgram ),
	P( "glValidateProgramARB", gl_ValidateProgram ),
	P( "glBindAttribLocation", gl_BindAttribLocation ),
	P( "glBindAttribLocationARB", gl_BindAttribLocation ),
	P( "glGetShaderiv", gl_GetObjectParameteriv ),
	P( "glGetProgramiv", gl_GetObjectParameteriv ),
	P( "glGetObjectParameterivARB", gl_GetObjectParameteriv ),
	P( "glGetShaderInfoLog", gl_GetInfoLog ),
	P( "glGetProgramInfoLog", gl_GetInfoLog ),
	P( "glGetInfoLogARB", gl_GetInfoLog ),
	P( "glGetUniformLocation", gl_GetUniformLocation ),
	P( "glGetUniformLocationARB", gl_GetUniformLocation ),
	P( "glGetAttribLocation", gl_GetAttribLocation ),
	P( "glGetAttribLocationARB", gl_GetAttribLocation ),
	P( "glUniform1i", gl_Uniform1i ),
	P( "glUniform1iARB", gl_Uniform1i ),
	P( "glUniform1f", gl_Uniform1f ),
	P( "glUniform1fARB", gl_Uniform1f ),
	P( "glUniform4f", gl_Uniform4f ),
	P( "glUniform4fARB", gl_Uniform4f ),
	P( "glUniform4fv", gl_Uniform4fv ),
	P( "glUniform4fvARB", gl_Uniform4fv ),
	P( "glUniformMatrix4fv", gl_UniformMatrix4fv ),
	P( "glUniformMatrix4fvARB", gl_UniformMatrix4fv ),

	P( "glEnableVertexAttribArray", gl_EnableVertexAttribArray ),
	P( "glEnableVertexAttribArrayARB", gl_EnableVertexAttribArray ),
	P( "glDisableVertexAttribArray", gl_DisableVertexAttribArray ),
	P( "glDisableVertexAttribArrayARB", gl_DisableVertexAttribArray ),
	P( "glVertexAttribPointer", gl_VertexAttribPointer ),
	P( "glVertexAttribPointerARB", gl_VertexAttribPointer ),

	P( "glDrawArrays", gl_DrawArrays ),
	P( "glDrawElements", gl_DrawElements ),
	P( "glDrawRangeElements", gl_DrawRangeElements ),
	P( "glDrawRangeElementsEXT", gl_DrawRangeElements ),
	P( "glDrawRangeElementsBaseVertex", gl_DrawRangeElementsBaseVertex ),
	P( "glDrawRangeElementsBaseVertexOES", gl_DrawRangeElementsBaseVertex ),
	P( "glDrawElementsBaseVertex", gl_DrawElementsBaseVertex ),
	P( "glDrawElementsBaseVertexOES", gl_DrawElementsBaseVertex ),
};
#undef P

// functions gl2_shim replaces and never calls through, and functions ref_gl
// checks for optional features: unknown names there must stay NULL
static const char *const missing_on_purpose[] =
{
	"glMapBufferRange", "glMapBufferRangeEXT", "glFlushMappedBufferRange", "glFlushMappedBufferRangeEXT",
	"glBufferStorage", "glBufferStorageEXT", "glBindVertexArray", "glBindVertexArrayOES",
	"glGenVertexArrays", "glGenVertexArraysOES", "glDeleteVertexArrays", "glDeleteVertexArraysOES",
	"glMapBuffer", "glMapBufferARB", "glUnmapBuffer", "glUnmapBufferARB",
	"glGetStringi", "glDebugMessageControl", "glDebugMessageControlARB", "glDebugMessageCallback",
	"glDebugMessageCallbackARB", "glDebugMessageInsert", "glDebugMessageInsertARB", "glGetDebugMessageLog",
	"glGetDebugMessageLogARB",
};

void *PS4_GL_GetProcAddress( const char *name )
{
	size_t i;

	for( i = 0; i < sizeof( procs ) / sizeof( procs[0] ); i++ )
	{
		if( !strcmp( procs[i].name, name ))
			return procs[i].func;
	}

	for( i = 0; i < sizeof( missing_on_purpose ) / sizeof( missing_on_purpose[0] ); i++ )
	{
		if( !strcmp( missing_on_purpose[i], name ))
			return NULL;
	}

	// everything else: legacy GL 1.x functions gl2_shim replaces, or something
	// we don't do yet; a stub that reports the name once is safer than NULL
	return get_stub( name );
}

/*
==============================================================================

Context

==============================================================================
*/
static bool load_shaders( void )
{
	int i;

	if( !gnm_dev_load_vs( &gl.clear_vs, gnm_shader_clear_vs.data, gnm_shader_clear_vs.size )
		|| !gnm_dev_load_ps( &gl.clear_ps, gnm_shader_clear_ps.data, gnm_shader_clear_ps.size ))
		return false;

	for( i = 0; i < GNM_GL2_VS_COUNT; i++ )
	{
		if( !gnm_dev_load_vs( &gl.vs[i], gnm_shader_gl2_vs[i].data, gnm_shader_gl2_vs[i].size ))
			return false;
	}

	for( i = 0; i < GNM_GL2_PS_COUNT; i++ )
	{
		if( !gnm_dev_load_ps( &gl.ps[i], gnm_shader_gl2_ps[i].data, gnm_shader_gl2_ps[i].size ))
			return false;
	}

	return true;
}

static bool init_white_texture( void )
{
	gl_texture_t *t = &gl.white;

	memset( t, 0, sizeof( *t ));
	t->exists = true;
	t->width = t->height = 1;
	t->levels[0].width = t->levels[0].height = 1;
	t->levels[0].shadow = malloc( 4 );
	memset( t->levels[0].shadow, 255, 4 );
	t->min_filter = t->mag_filter = GL_NEAREST;
	t->wrap_s = t->wrap_t = GL_CLAMP_TO_EDGE;
	return upload_texture( t );
}

static void reset_state( void )
{
	gl.error = GL_NO_ERROR;
	gl.next_texture = 1;
	gl.next_buffer = 1;
	gl.blend_src = GL_ONE;
	gl.blend_dst = GL_ZERO;
	gl.depth_func = 0x0201; // GL_LESS
	gl.depth_mask = true;
	gl.color_mask = 0xf;
	gl.cull_face = GL_BACK;
	gl.front_face = GL_CCW;
	gl.depth_near = 0.0f;
	gl.depth_far = 1.0f;
	gl.clear_depth = 1.0f;
	gl.unpack_alignment = 4;
	gl.pack_alignment = 4;
	gl.viewport[0] = gl.viewport[1] = 0;
	gl.viewport[2] = gnm_dev.width;
	gl.viewport[3] = gnm_dev.height;
	memcpy( gl.scissor, gl.viewport, sizeof( gl.scissor ));
	restart_cb();
}

static void gnm_message( GnmMessageSeverity severity, const char *message, void *userdata )
{
	GNM_LOG( "opengnm %s: %s\n", severity == GNM_MSGSEV_ERR ? "error" : "warning", message );
}

int PS4_GL_Init( const gnm_dev_api_t *api )
{
	if( gl.initialized )
		return true;

	gnm_api = api;
	sceGnmSetMessageHandler( gnm_message, NULL );
	gnm_dev_set_restart_callback( restart_cb );

	if( !load_shaders( ) || !init_white_texture( ))
	{
		GNM_LOG( "shader or texture init failed\n" );
		return false;
	}

	gl.flip_front = gnm_dev.flip_front;
	gl.mipmaps = gnm_dev.mipmaps;
	reset_state();

	gl.initialized = true;
	GNM_LOG( "OpenGL layer ready, debug level %d\n", gnm_dev.debug );
	return true;
}

// the engine presents the frame after this
void PS4_GL_EndFrame( void )
{
	if( !gl.initialized )
		return;

	if( gnm_dev.frame < 3 || gnm_dev.frame == 60 || gnm_dev.frame == 600 )
		GNM_LOG( "frame %u: %u draws, %u vertices\n", gnm_dev.frame, gl.draws, gl.verts );

	gl.draws = gl.verts = 0;
}

// the device is still open, the engine closes it after this
void PS4_GL_Shutdown( void )
{
	int i;

	if( !gl.initialized )
		return;

	gnm_dev_set_restart_callback( NULL );

	for( i = 0; i < gl.numtextures; i++ )
	{
		if( gl.textures[i].exists )
			free_texture_storage( &gl.textures[i] );
	}
	free( gl.textures );
	for( i = 0; i < gl.numbuffers; i++ )
		free( gl.buffers[i].data );
	free( gl.buffers );
	free( gl.objects );

	gl.textures = NULL;
	gl.numtextures = 0;
	gl.buffers = NULL;
	gl.numbuffers = 0;
	gl.objects = NULL;
	gl.numobjects = 0;
	gl.program = 0;
	gl.initialized = false;
	// shaders and the white texture stay in GPU memory, they're loaded again on next init
}
