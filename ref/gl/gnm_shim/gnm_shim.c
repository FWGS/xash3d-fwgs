/*
gnm_shim.c - fixed function OpenGL over GNM for ref_gl on PS4
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

// Implements what ref_gl calls, like vgl_shim does on top of vitaGL, but
// there's no driver below: state, textures and draws go to GNM directly.
// Fixed function state selects one of precompiled shader variants (see
// shaders/ and gnm_shaders.h). Vertex data is copied into per-submit memory
// on every draw and pulled by the vertex shader.

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
#include "xash3d_types.h"
#include "gl_export.h"
#include "gnm_shim.h"
#include "gnm_shaders.h"

#define GL_MIRRORED_REPEAT 0x8370

const gnm_dev_api_t *gnm_api;

#define GNM_LOG( ... ) gnm_api->log( "GNM: " __VA_ARGS__ )

#define MAX_TEXTURE_UNITS   2
#define MAX_TEXTURE_LEVELS  14 // 8192x8192 down to 1x1
#define MAX_VERTS           ( 1024 * 1024 )
#define VS_BASE_VERTEX_SGPR 2 // see set_shaders

// vertex attributes, in the order of shader variant bits
enum { SEM_POS, SEM_COLOR, SEM_TC0, SEM_TC1, SEM_COUNT };

// descriptor tables: binding N of a shader is at offset N * 64, see psbc_build_set_layout
// in opengnm-psbc; bindings are in shaders/ff.*.glsl
#define BINDING( n ) (( n ) * 64 )
#define VS_TABLE_SIZE BINDING( 1 + SEM_COUNT ) // uniforms, then one buffer per attribute
#define PS_TABLE_SIZE BINDING( 1 + MAX_TEXTURE_UNITS ) // uniforms, then textures

// texture matrices are kept for glMatrixMode, but the shaders don't apply them
enum { MAT_MODELVIEW, MAT_PROJECTION, MAT_TEXTURE, MAT_COUNT };

/*
==============================================================================

Objects

==============================================================================
*/
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
	GLint internalformat; // channels of the client data that are used, see apply_internalformat
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

// client array, buffer is GL_ARRAY_BUFFER bound when the pointer was set
typedef struct gl_array_s
{
	bool enabled;
	GLint size;
	GLenum type;
	GLsizei stride;
	const void *pointer;
	GLuint buffer;
} gl_array_t;

// vertex attribute of a draw, in client memory, buffer contents or the current state
typedef struct draw_attrib_s
{
	const uint8_t *data; // NULL - not used by the draw
	size_t limit;        // bytes that can be read from data
	GLint size;
	GLenum type;
	GLsizei stride;      // 0 - tightly packed
	bool constant;       // one value for all vertices
} draw_attrib_t;

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

	int active_unit, client_unit;
	GLuint bound[MAX_TEXTURE_UNITS];

	// fixed function state
	bool texture_1d[MAX_TEXTURE_UNITS], texture_2d[MAX_TEXTURE_UNITS];
	bool alpha_test, fog;
	float alpha_ref;
	float fog_color[4];
	float fog_density;
	float matrix[MAT_COUNT][16];
	int matrix_mode;
	float color[4];
	float texcoord[MAX_TEXTURE_UNITS][2];

	// client arrays: vertices, colors, texture coordinates of every unit
	gl_array_t arrays[SEM_COUNT];

	// vertices between glBegin and glEnd
	bool begun;
	GLenum prim;
	int numverts, maxverts;
	float *imm_pos;
	float *imm_color;
	float *imm_tc[MAX_TEXTURE_UNITS];

	// GL_QUADS drawn as triangles
	uint32_t *quad_indices;
	int maxquadindices;

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

	gnm_vs_t clear_vs, vs[GNM_FF_VS_COUNT];
	gnm_ps_t clear_ps, ps[GNM_FF_PS_COUNT];

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

Errors, strings, queries

==============================================================================
*/
static GLenum gl_GetError( void )
{
	GLenum err = gl.error;
	gl.error = GL_NO_ERROR;
	return err;
}

static const GLubyte *gl_GetString( GLenum name )
{
	switch( name )
	{
	case GL_VENDOR: return (const GLubyte *)"Xash3D FWGS";
	case GL_RENDERER: return (const GLubyte *)"PS4 GNM (opengnm)";
	case GL_VERSION: return (const GLubyte *)"1.1 xash-gnm";
	case GL_EXTENSIONS: return (const GLubyte *)"GL_ARB_multitexture GL_ARB_texture_non_power_of_two GL_EXT_texture_edge_clamp "
		"GL_EXT_texture_filter_anisotropic GL_ARB_vertex_buffer_object";
	}

	set_error( GL_INVALID_ENUM );
	return (const GLubyte *)"";
}

static void gl_GetIntegerv( GLenum pname, GLint *v )
{
	static GLenum s_reported[16];
	int i;

	switch( pname )
	{
	case GL_MAX_TEXTURE_SIZE:
		*v = 4096;
		return;
	case GL_MAX_TEXTURE_UNITS_ARB:
	case GL_MAX_TEXTURE_IMAGE_UNITS_ARB:
	case GL_MAX_TEXTURE_COORDS_ARB:
		*v = MAX_TEXTURE_UNITS;
		return;
	case GL_MAJOR_VERSION:
	case GL_MINOR_VERSION:
	case GL_NUM_EXTENSIONS:
		// not in OpenGL 1.1, ref_gl takes the version from GL_VERSION
		*v = 0;
		return;
	}

	*v = 0;

	for( i = 0; i < 16 && s_reported[i]; i++ )
	{
		if( s_reported[i] == pname )
			return;
	}

	if( i < 16 )
	{
		s_reported[i] = pname;
		GNM_LOG( "glGetIntegerv: unknown 0x%x\n", pname );
	}
}

static void gl_GetFloatv( GLenum pname, GLfloat *v )
{
	GLint i = 0;

	switch( pname )
	{
	case GL_MODELVIEW_MATRIX:
		memcpy( v, gl.matrix[MAT_MODELVIEW], sizeof( gl.matrix[0] ));
		return;
	case GL_PROJECTION_MATRIX:
		memcpy( v, gl.matrix[MAT_PROJECTION], sizeof( gl.matrix[0] ));
		return;
	case GL_FOG_COLOR:
		memcpy( v, gl.fog_color, sizeof( gl.fog_color ));
		return;
	case GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT:
		*v = 16.0f;
		return;
	}

	gl_GetIntegerv( pname, &i );
	*v = (float)i;
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
// gpu: the state is emitted into the command buffer, others select shader variants
static bool *cap_ptr( GLenum cap, bool *gpu )
{
	*gpu = true;

	switch( cap )
	{
	case GL_BLEND: return &gl.blend;
	case GL_DEPTH_TEST: return &gl.depth_test;
	case GL_CULL_FACE: return &gl.cull;
	case GL_SCISSOR_TEST: return &gl.scissor_test;
	case GL_POLYGON_OFFSET_FILL: return &gl.polygon_offset;
	}

	*gpu = false;

	switch( cap )
	{
	case GL_TEXTURE_1D: return &gl.texture_1d[gl.active_unit];
	case GL_TEXTURE_2D: return &gl.texture_2d[gl.active_unit];
	case GL_ALPHA_TEST: return &gl.alpha_test;
	case GL_FOG: return &gl.fog;
	}

	return NULL;
}

static void set_cap( GLenum cap, bool enable )
{
	bool gpu;
	bool *p = cap_ptr( cap, &gpu );

	if( !p || *p == enable )
		return;

	*p = enable;
	if( gpu )
		gl.dirty_state = true;
}

static void gl_Enable( GLenum cap )
{
	set_cap( cap, true );
}

static void gl_Disable( GLenum cap )
{
	set_cap( cap, false );
}

static GLboolean gl_IsEnabled( GLenum cap )
{
	bool gpu;
	bool *p = cap_ptr( cap, &gpu );
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

static void gl_DepthRange( GLclampd n, GLclampd f )
{
	gl.depth_near = (float)n;
	gl.depth_far = (float)f;
	gl.dirty_state = true;
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

static void gl_ClearDepth( GLclampd d )
{
	gl.clear_depth = (float)d;
}

// the shaders only compare with GL_GREATER
static void gl_AlphaFunc( GLenum func, GLclampf ref )
{
	gl.alpha_ref = ref;
}

// the shaders only do GL_EXP fog
static void gl_Fogf( GLenum pname, GLfloat param )
{
	if( pname == GL_FOG_DENSITY )
		gl.fog_density = param;
}

static void gl_Fogi( GLenum pname, GLint param )
{
	gl_Fogf( pname, (GLfloat)param );
}

static void gl_Fogfv( GLenum pname, const GLfloat *params )
{
	if( pname == GL_FOG_COLOR )
		memcpy( gl.fog_color, params, sizeof( gl.fog_color ));
	else
		gl_Fogf( pname, params[0] );
}

// the shaders always modulate
static void gl_TexEnvi( GLenum target, GLenum pname, GLint param )
{
}

static void gl_TexEnvf( GLenum target, GLenum pname, GLfloat param )
{
}

static void gl_TexEnvfv( GLenum target, GLenum pname, const GLfloat *params )
{
}

static void gl_TexGeni( GLenum coord, GLenum pname, GLint param )
{
}

static void gl_Hint( GLenum target, GLenum mode )
{
}

static void gl_ShadeModel( GLenum mode )
{
}

static void gl_PolygonMode( GLenum face, GLenum mode )
{
}

static void gl_LineWidth( GLfloat width )
{
}

static void gl_PointSize( GLfloat size )
{
}

static void gl_DrawBuffer( GLenum mode )
{
}

static void gl_ReadBuffer( GLenum mode )
{
}

// there's no stencil buffer
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

Matrices

==============================================================================
*/
static void identity( float *m )
{
	memset( m, 0, sizeof( float ) * 16 );
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

// column-major, out = a * b
static void mat_mul( const float *a, const float *b, float *out )
{
	for( int c = 0; c < 4; c++ )
	{
		for( int r = 0; r < 4; r++ )
			out[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] + a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
	}
}

static void gl_MatrixMode( GLenum mode )
{
	switch( mode )
	{
	case GL_MODELVIEW: gl.matrix_mode = MAT_MODELVIEW; break;
	case GL_PROJECTION: gl.matrix_mode = MAT_PROJECTION; break;
	case GL_TEXTURE: gl.matrix_mode = MAT_TEXTURE; break;
	default: set_error( GL_INVALID_ENUM ); break;
	}
}

static void gl_LoadIdentity( void )
{
	identity( gl.matrix[gl.matrix_mode] );
}

static void gl_LoadMatrixf( const GLfloat *m )
{
	memcpy( gl.matrix[gl.matrix_mode], m, sizeof( gl.matrix[0] ));
}

static void gl_Translatef( GLfloat x, GLfloat y, GLfloat z )
{
	float *m = gl.matrix[gl.matrix_mode];

	for( int r = 0; r < 4; r++ )
		m[12 + r] += x * m[r] + y * m[4 + r] + z * m[8 + r];
}

static void gl_Scalef( GLfloat x, GLfloat y, GLfloat z )
{
	float *m = gl.matrix[gl.matrix_mode];

	for( int r = 0; r < 4; r++ )
	{
		m[r] *= x;
		m[4 + r] *= y;
		m[8 + r] *= z;
	}
}

/*
==============================================================================

Textures

==============================================================================
*/
// 1D textures are 2D textures one texel high, a texture unit has one binding for both
static bool texture_target( GLenum target )
{
	return target == GL_TEXTURE_2D || target == GL_TEXTURE_1D;
}

static bool texture_enabled( int unit )
{
	return gl.texture_1d[unit] || gl.texture_2d[unit];
}

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
		t->internalformat = GL_RGBA;
		t->min_filter = GL_NEAREST_MIPMAP_LINEAR;
		t->mag_filter = GL_LINEAR;
		t->wrap_s = t->wrap_t = GL_REPEAT;
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
	if( !texture_target( target ))
		return;

	gl.bound[gl.active_unit] = texture;
	get_texture( texture, true );
}

static void gl_ActiveTexture( GLenum unit )
{
	int u = (int)unit - GL_TEXTURE0_ARB;

	if( u >= 0 && u < MAX_TEXTURE_UNITS )
		gl.active_unit = u;
}

static void gl_ClientActiveTexture( GLenum unit )
{
	int u = (int)unit - GL_TEXTURE0_ARB;

	if( u >= 0 && u < MAX_TEXTURE_UNITS )
		gl.client_unit = u;
}

static void gl_TexParameteri( GLenum target, GLenum pname, GLint param )
{
	gl_texture_t *t = bound_texture();

	if( !t || !texture_target( target ))
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

		for( x = 0; x < w; x++ )
		{
			const uint8_t *sp = s + x * comps;
			uint8_t *dp = d + x * 4;

			switch( format )
			{
			case GL_BGRA: dp[0] = sp[2]; dp[1] = sp[1]; dp[2] = sp[0]; dp[3] = sp[3]; break;
			case GL_RGB: dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = 255; break;
			case GL_BGR: dp[0] = sp[2]; dp[1] = sp[1]; dp[2] = sp[0]; dp[3] = 255; break;
			case GL_LUMINANCE_ALPHA: dp[0] = dp[1] = dp[2] = sp[0]; dp[3] = sp[1]; break;
			case GL_LUMINANCE: dp[0] = dp[1] = dp[2] = sp[0]; dp[3] = 255; break;
			case GL_ALPHA: dp[0] = dp[1] = dp[2] = 255; dp[3] = sp[0]; break;
			}
		}
	}

	return true;
}

// what the internal format keeps of RGBA8: no alpha in color and luminance formats,
// red in every channel of intensity formats
static void apply_internalformat( uint8_t *dst, int dststride, int w, int h, GLint internalformat )
{
	int x, y;

	switch( internalformat )
	{
	case GL_RGB: case GL_RGB5: case GL_RGB8: case GL_LUMINANCE: case GL_LUMINANCE4: case GL_LUMINANCE8:
		for( y = 0; y < h; y++ )
		{
			for( x = 0; x < w; x++ )
				dst[y * dststride + x * 4 + 3] = 255;
		}
		break;
	case GL_INTENSITY: case GL_INTENSITY8:
		for( y = 0; y < h; y++ )
		{
			for( x = 0; x < w; x++ )
			{
				uint8_t *p = dst + y * dststride + x * 4;
				p[1] = p[2] = p[3] = p[0];
			}
		}
		break;
	}
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
		t->internalformat = internalformat;
	}

	l = &t->levels[level];
	free_texture_level( l );
	l->width = width;
	l->height = height;
	l->shadow = calloc( 1, (size_t)width * height * 4 );
	t->dirty = true;

	if( pixels )
	{
		if( convert_pixels( l->shadow, width * 4, pixels, width, height, format, type ))
			apply_internalformat( l->shadow, width * 4, width, height, t->internalformat );
		else
		{
			static bool s_reported;
			if( !s_reported )
			{
				GNM_LOG( "glTexImage2D: unsupported format 0x%x type 0x%x\n", format, type );
				s_reported = true;
			}
		}
	}

	if( gnm_dev.debug && level == 0 )
		GNM_LOG( "glTexImage2D: texture %u %dx%d, internal format 0x%x, format 0x%x type 0x%x align %d, %s\n",
			gl.bound[gl.active_unit], width, height, internalformat, format, type, gl.unpack_alignment,
			pixels ? "data" : "no data" );
}

static void gl_TexSubImage2D( GLenum target, GLint level, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels )
{
	gl_texture_t *t = bound_texture();
	gl_texture_level_t *l;
	uint8_t *dst;
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
		static int s_logged;
		if( s_logged < 64 || gnm_dev.debug >= 2 )
		{
			GNM_LOG( "glTexSubImage2D: texture %u level %d (%dx%d) rect %d,%d %dx%d, format 0x%x type 0x%x align %d, %s\n",
				gl.bound[gl.active_unit], level, l->width, l->height, x, y, width, height, format, type, gl.unpack_alignment,
				t->dirty || !t->gpu ? "pending upload" : t->used_serial == recording_serial() ? "in use, new memory" : "in place" );
			s_logged++;
		}
	}

	dst = l->shadow + ((size_t)y * l->width + x ) * 4;
	if( !convert_pixels( dst, l->width * 4, pixels, width, height, format, type ))
	{
		GNM_LOG( "glTexSubImage2D: unsupported format 0x%x type 0x%x\n", format, type );
		return;
	}
	apply_internalformat( dst, l->width * 4, width, height, t->internalformat );

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

static void gl_TexImage1D( GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid *pixels )
{
	if( target == GL_TEXTURE_1D )
		gl_TexImage2D( GL_TEXTURE_2D, level, internalformat, width, 1, border, format, type, pixels );
}

static void gl_TexSubImage1D( GLenum target, GLint level, GLint x, GLsizei width, GLenum format, GLenum type, const GLvoid *pixels )
{
	if( target == GL_TEXTURE_1D )
		gl_TexSubImage2D( GL_TEXTURE_2D, level, x, 0, width, 1, format, type, pixels );
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
	case GL_ARRAY_BUFFER_ARB: return &gl.array_buffer;
	case GL_ELEMENT_ARRAY_BUFFER_ARB: return &gl.element_buffer;
	}
	return NULL;
}

static gl_buffer_t *bound_buffer( GLenum target )
{
	GLuint *bind = buffer_binding( target );
	return bind ? get_buffer( *bind, false ) : NULL;
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
	int i, a;

	for( i = 0; i < n; i++ )
	{
		gl_buffer_t *b = get_buffer( buffers[i], false );

		if( !b )
			continue;

		free( b->data );
		memset( b, 0, sizeof( *b ));

		if( gl.array_buffer == buffers[i] )
			gl.array_buffer = 0;
		if( gl.element_buffer == buffers[i] )
			gl.element_buffer = 0;
		for( a = 0; a < SEM_COUNT; a++ )
		{
			if( gl.arrays[a].buffer == buffers[i] )
				gl.arrays[a].buffer = 0;
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
static void gl_BufferData( GLenum target, GLsizeiptrARB size, const GLvoid *data, GLenum usage )
{
	gl_buffer_t *b = bound_buffer( target );

	if( !b || size < 0 )
		return;

	free( b->data );
	b->data = malloc( size ? size : 1 );
	b->size = size;
	if( data )
		memcpy( b->data, data, size );
}

static void gl_BufferSubData( GLenum target, GLintptrARB offset, GLsizeiptrARB size, const GLvoid *data )
{
	gl_buffer_t *b = bound_buffer( target );

	if( !b || !b->data || offset < 0 || size < 0 || (size_t)offset + size > b->size )
		return;

	memcpy( b->data + offset, data, size );
}

static GLvoid *gl_MapBuffer( GLenum target, GLenum access )
{
	gl_buffer_t *b = bound_buffer( target );
	return b ? b->data : NULL;
}

static GLboolean gl_UnmapBuffer( GLenum target )
{
	return bound_buffer( target ) != NULL;
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

static void draw_dropped( const char *reason )
{
	static int s_count;

	if( s_count < 16 )
	{
		GNM_LOG( "draw dropped: %s\n", reason );
		s_count++;
	}
}

static bool attrib_source( const draw_attrib_t *a, int vmin, int numverts, attr_source_t *out )
{
	int typesize;

	switch( a->type )
	{
	case GL_FLOAT: typesize = 4; out->type = 0; break;
	case GL_UNSIGNED_BYTE: typesize = 1; out->type = 1; break;
	default:
	{
		static bool s_reported;
		if( !s_reported )
		{
			GNM_LOG( "vertex attribute type 0x%x isn't supported\n", a->type );
			s_reported = true;
		}
		return false;
	}
	}

	if( a->size < 1 || a->size > 4 )
		return false;

	out->comps = a->size;
	out->elemsize = a->size * typesize;

	if( a->constant )
	{
		out->stride = 0;
		out->src = a->data;
		out->bytes = out->elemsize;
	}
	else
	{
		out->stride = a->stride ? a->stride : out->elemsize;
		out->src = a->data + (size_t)vmin * out->stride;
		out->bytes = (uint32_t)(( numverts - 1 ) * out->stride + out->elemsize );
	}

	if( (size_t)( out->src - a->data ) + out->bytes > a->limit )
		return false;

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
	case GL_QUAD_STRIP: return GNM_PT_TRISTRIP;
	case GL_POLYGON: return GNM_PT_TRIFAN;
	}
	return -1;
}

static int index_size( GLenum type )
{
	switch( type )
	{
	case GL_UNSIGNED_BYTE: return 1;
	case GL_UNSIGNED_SHORT: return 2;
	case GL_UNSIGNED_INT: return 4;
	}
	return 0;
}

static uint32_t read_index( const void *indices, GLenum type, int i )
{
	switch( type )
	{
	case GL_UNSIGNED_BYTE: return ((const uint8_t *)indices)[i];
	case GL_UNSIGNED_SHORT: return ((const uint16_t *)indices)[i];
	}
	return ((const uint32_t *)indices)[i];
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

// indices: client memory with count indices of itype, otherwise first is the first vertex;
// absent color is the uniform one, texture units without texture coordinates aren't sampled
static void draw( GLenum mode, GLint first, GLsizei count, GLenum itype, const void *indices, const draw_attrib_t *attribs, const float *color )
{
	attr_source_t attrs[SEM_COUNT];
	int vmin, vmax, numverts, prim, vsbits, psbits, sem, k, i;
	int semantics[SEM_COUNT], numsems = 0;
	uint32_t transient, tables, ibytes = 0;
	const gnm_vs_t *vs;
	const gnm_ps_t *ps;
	GnmCommandBuffer *cmd;
	float mvp[16];
	bool idx32 = false;

	if( count <= 0 )
		return;

	prim = primitive_type( mode );
	if( prim < 0 )
	{
		draw_dropped( "unsupported primitive" );
		return;
	}

	// vertex range used by the draw
	if( indices )
	{
		vmin = 0x7fffffff;
		vmax = -1;
		for( i = 0; i < count; i++ )
		{
			int v = (int)read_index( indices, itype, i );
			if( v < vmin ) vmin = v;
			if( v > vmax ) vmax = v;
		}
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

	// attributes the variant reads, in this order: attribute k is the k-th of them
	vsbits = 0;
	semantics[numsems++] = SEM_POS;
	if( attribs[SEM_COLOR].data )
	{
		vsbits |= GNM_FF_COLOR;
		semantics[numsems++] = SEM_COLOR;
	}
	if( attribs[SEM_TC0].data )
	{
		vsbits |= GNM_FF_TEXCOORD0;
		semantics[numsems++] = SEM_TC0;
	}
	if( attribs[SEM_TC1].data )
	{
		vsbits |= GNM_FF_TEXCOORD1;
		semantics[numsems++] = SEM_TC1;
	}

	psbits = vsbits;
	if( gl.alpha_test )
		psbits |= GNM_FF_ALPHA_TEST;
	if( gl.fog )
		psbits |= GNM_FF_FOG;

	transient = 144 + 48 + ibytes;
	for( k = 0; k < numsems; k++ )
	{
		if( !attrib_source( &attribs[semantics[k]], vmin, numverts, &attrs[k] ))
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
			gnm_dev.submit_draws, mode, count, indices ? ( idx32 ? "idx32" : "idx16" ) : "arrays", vmin, vmax, vsbits, psbits, numsems,
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
		GnmBuffer b;

		if( !vsu || !table )
		{
			draw_dropped( "out of GPU memory" );
			return;
		}

		int32_t *ai = (int32_t *)( vsu + 64 );

		mat_mul( gl.matrix[MAT_PROJECTION], gl.matrix[MAT_MODELVIEW], mvp );
		memcpy( vsu, mvp, 64 );
		memset( vsu + 64, 0, 80 );

		for( k = 0; k < numsems; k++ )
		{
			uint8_t *data = gnm_dev_alloc_transient( attrs[k].bytes + 16, 16 );

			if( !data )
			{
				draw_dropped( "out of GPU memory" );
				return;
			}

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

		if( !psu || !table )
		{
			draw_dropped( "out of GPU memory" );
			return;
		}

		memcpy( psu, color, 16 );
		psu[4] = gl.fog_color[0];
		psu[5] = gl.fog_color[1];
		psu[6] = gl.fog_color[2];
		psu[7] = gl.fog_density;
		psu[8] = gl.alpha_ref;
		psu[9] = psu[10] = psu[11] = 0.0f;

		b = sceGnmCreateConstBuffer( psu, 48 );
		b.stride = 0;
		b.numrecords = 48;
		memcpy( table, &b, 16 );

		for( u = 0; u < MAX_TEXTURE_UNITS; u++ )
		{
			const gl_texture_t *t;
			GnmSampler s;

			if( !( psbits & ( u ? GNM_FF_TEXCOORD1 : GNM_FF_TEXCOORD0 )))
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

	if( indices )
	{
		void *idata = gnm_dev_alloc_transient( idx32 ? count * 4 : count * 2, 16 );

		if( !idata )
		{
			draw_dropped( "out of GPU memory" );
			return;
		}

		for( i = 0; i < count; i++ )
		{
			uint32_t v = read_index( indices, itype, i ) - vmin;

			if( idx32 )
				((uint32_t *)idata)[i] = v;
			else
				((uint16_t *)idata)[i] = (uint16_t)v;
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

// GL_QUADS as triangles, one quad is a fan
static void draw_prims( GLenum mode, GLint first, GLsizei count, GLenum itype, const void *indices, const draw_attrib_t *attribs, const float *color )
{
	int numquads, i, q;

	if( mode != GL_QUADS )
	{
		draw( mode, first, count, itype, indices, attribs, color );
		return;
	}

	if( count == 4 && !indices )
	{
		draw( GL_TRIANGLE_FAN, first, count, 0, NULL, attribs, color );
		return;
	}

	numquads = count / 4;
	if( numquads * 6 > gl.maxquadindices )
	{
		uint32_t *p = realloc( gl.quad_indices, numquads * 6 * sizeof( uint32_t ));

		if( !p )
		{
			draw_dropped( "out of memory for quads" );
			return;
		}

		gl.quad_indices = p;
		gl.maxquadindices = numquads * 6;
	}

	for( q = 0; q < numquads; q++ )
	{
		uint32_t v[4];

		for( i = 0; i < 4; i++ )
			v[i] = indices ? read_index( indices, itype, q * 4 + i ) : (uint32_t)( first + q * 4 + i );

		gl.quad_indices[q * 6 + 0] = v[0];
		gl.quad_indices[q * 6 + 1] = v[1];
		gl.quad_indices[q * 6 + 2] = v[2];
		gl.quad_indices[q * 6 + 3] = v[0];
		gl.quad_indices[q * 6 + 4] = v[2];
		gl.quad_indices[q * 6 + 5] = v[3];
	}

	draw( GL_TRIANGLES, 0, numquads * 6, GL_UNSIGNED_INT, gl.quad_indices, attribs, color );
}

/*
==============================================================================

glBegin and glEnd

==============================================================================
*/
static bool imm_grow( void )
{
	int num = gl.maxverts ? gl.maxverts * 2 : 1024;
	float *p;
	int u;

	if( num > MAX_VERTS )
		return false;

	if(( p = realloc( gl.imm_pos, num * 3 * sizeof( float ))))
		gl.imm_pos = p;
	else
		return false;

	if(( p = realloc( gl.imm_color, num * 4 * sizeof( float ))))
		gl.imm_color = p;
	else
		return false;

	for( u = 0; u < MAX_TEXTURE_UNITS; u++ )
	{
		if(( p = realloc( gl.imm_tc[u], num * 2 * sizeof( float ))))
			gl.imm_tc[u] = p;
		else
			return false;
	}

	gl.maxverts = num;
	return true;
}

static void gl_Begin( GLenum mode )
{
	gl.prim = mode;
	gl.numverts = 0;
	gl.begun = true;
}

static void gl_Vertex3f( GLfloat x, GLfloat y, GLfloat z )
{
	const int i = gl.numverts;
	int u;

	if( !gl.begun )
		return;

	if( i >= gl.maxverts && !imm_grow( ))
	{
		draw_dropped( "too many vertices between glBegin and glEnd" );
		return;
	}

	gl.imm_pos[i * 3 + 0] = x;
	gl.imm_pos[i * 3 + 1] = y;
	gl.imm_pos[i * 3 + 2] = z;
	memcpy( &gl.imm_color[i * 4], gl.color, sizeof( gl.color ));
	for( u = 0; u < MAX_TEXTURE_UNITS; u++ )
		memcpy( &gl.imm_tc[u][i * 2], gl.texcoord[u], sizeof( gl.texcoord[u] ));
	gl.numverts++;
}

static void gl_Vertex2f( GLfloat x, GLfloat y )
{
	gl_Vertex3f( x, y, 0.0f );
}

static void gl_Vertex3fv( const GLfloat *v )
{
	gl_Vertex3f( v[0], v[1], v[2] );
}

static void imm_attrib( float *data, int size, draw_attrib_t *out )
{
	out->data = (const uint8_t *)data;
	out->limit = (size_t)gl.numverts * size * sizeof( float );
	out->size = size;
	out->type = GL_FLOAT;
	out->stride = 0;
	out->constant = false;
}

static void gl_End( void )
{
	draw_attrib_t attribs[SEM_COUNT];
	bool varying_color = false;
	int i, u;

	if( !gl.begun )
		return;

	gl.begun = false;
	if( !gl.numverts )
		return;

	memset( attribs, 0, sizeof( attribs ));
	imm_attrib( gl.imm_pos, 3, &attribs[SEM_POS] );

	// color of all vertices goes as the uniform when it's the same
	for( i = 1; i < gl.numverts && !varying_color; i++ )
		varying_color = memcmp( &gl.imm_color[i * 4], gl.imm_color, sizeof( float ) * 4 ) != 0;
	if( varying_color )
		imm_attrib( gl.imm_color, 4, &attribs[SEM_COLOR] );

	for( u = 0; u < MAX_TEXTURE_UNITS; u++ )
	{
		if( texture_enabled( u ))
			imm_attrib( gl.imm_tc[u], 2, &attribs[SEM_TC0 + u] );
	}

	draw_prims( gl.prim, 0, gl.numverts, 0, NULL, attribs, gl.imm_color );
}

static void gl_Color4f( GLfloat r, GLfloat g, GLfloat b, GLfloat a )
{
	gl.color[0] = r;
	gl.color[1] = g;
	gl.color[2] = b;
	gl.color[3] = a;
}

static void gl_Color3f( GLfloat r, GLfloat g, GLfloat b )
{
	gl_Color4f( r, g, b, 1.0f );
}

static void gl_Color4ub( GLubyte r, GLubyte g, GLubyte b, GLubyte a )
{
	gl_Color4f( r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f );
}

static void gl_Color4ubv( const GLubyte *v )
{
	gl_Color4ub( v[0], v[1], v[2], v[3] );
}

static void gl_MultiTexCoord2f( GLenum target, GLfloat s, GLfloat t )
{
	int u = (int)target - GL_TEXTURE0_ARB;

	if( u < 0 || u >= MAX_TEXTURE_UNITS )
		return;

	gl.texcoord[u][0] = s;
	gl.texcoord[u][1] = t;
}

static void gl_MultiTexCoord1f( GLenum target, GLfloat s )
{
	gl_MultiTexCoord2f( target, s, 0.0f );
}

static void gl_MultiTexCoord3f( GLenum target, GLfloat s, GLfloat t, GLfloat r )
{
	gl_MultiTexCoord2f( target, s, t );
}

static void gl_MultiTexCoord4f( GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q )
{
	gl_MultiTexCoord2f( target, s, t );
}

static void gl_TexCoord2f( GLfloat s, GLfloat t )
{
	gl_MultiTexCoord2f( GL_TEXTURE0_ARB, s, t );
}

static void gl_TexCoord2fv( const GLfloat *v )
{
	gl_MultiTexCoord2f( GL_TEXTURE0_ARB, v[0], v[1] );
}

static void gl_TexCoord3f( GLfloat s, GLfloat t, GLfloat r )
{
	gl_MultiTexCoord2f( GL_TEXTURE0_ARB, s, t );
}

// there's no lighting
static void gl_Normal3fv( const GLfloat *v )
{
}

/*
==============================================================================

Client arrays

==============================================================================
*/
static gl_array_t *client_array( GLenum array )
{
	switch( array )
	{
	case GL_VERTEX_ARRAY: return &gl.arrays[SEM_POS];
	case GL_COLOR_ARRAY: return &gl.arrays[SEM_COLOR];
	case GL_TEXTURE_COORD_ARRAY: return &gl.arrays[SEM_TC0 + gl.client_unit];
	}
	return NULL;
}

static void set_pointer( gl_array_t *a, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer )
{
	a->size = size;
	a->type = type;
	a->stride = stride;
	a->pointer = pointer;
	a->buffer = gl.array_buffer;
}

static void gl_VertexPointer( GLint size, GLenum type, GLsizei stride, const GLvoid *pointer )
{
	set_pointer( &gl.arrays[SEM_POS], size, type, stride, pointer );
}

static void gl_ColorPointer( GLint size, GLenum type, GLsizei stride, const GLvoid *pointer )
{
	set_pointer( &gl.arrays[SEM_COLOR], size, type, stride, pointer );
}

static void gl_TexCoordPointer( GLint size, GLenum type, GLsizei stride, const GLvoid *pointer )
{
	set_pointer( &gl.arrays[SEM_TC0 + gl.client_unit], size, type, stride, pointer );
}

static void gl_EnableClientState( GLenum array )
{
	gl_array_t *a = client_array( array );

	if( a )
		a->enabled = true;
}

static void gl_DisableClientState( GLenum array )
{
	gl_array_t *a = client_array( array );

	if( a )
		a->enabled = false;
}

// buffer contents or client memory of an array
static bool array_attrib( const gl_array_t *a, draw_attrib_t *out )
{
	if( a->buffer )
	{
		const gl_buffer_t *b = get_buffer( a->buffer, false );
		const uintptr_t offset = (uintptr_t)a->pointer;

		if( !b || !b->data || offset >= b->size )
			return false;

		out->data = b->data + offset;
		out->limit = b->size - offset;
	}
	else
	{
		if( !a->pointer )
			return false;

		out->data = a->pointer;
		out->limit = SIZE_MAX;
	}

	out->size = a->size;
	out->type = a->type;
	out->stride = a->stride;
	out->constant = false;
	return true;
}

// texture units without an array use the current texture coordinates
static bool array_attribs( draw_attrib_t *attribs )
{
	int u;

	memset( attribs, 0, sizeof( *attribs ) * SEM_COUNT );

	if( !gl.arrays[SEM_POS].enabled || !array_attrib( &gl.arrays[SEM_POS], &attribs[SEM_POS] ))
		return false;

	if( gl.arrays[SEM_COLOR].enabled && !array_attrib( &gl.arrays[SEM_COLOR], &attribs[SEM_COLOR] ))
		return false;

	for( u = 0; u < MAX_TEXTURE_UNITS; u++ )
	{
		draw_attrib_t *a = &attribs[SEM_TC0 + u];

		if( !texture_enabled( u ))
			continue;

		if( gl.arrays[SEM_TC0 + u].enabled )
		{
			if( !array_attrib( &gl.arrays[SEM_TC0 + u], a ))
				return false;
		}
		else
		{
			a->data = (const uint8_t *)gl.texcoord[u];
			a->limit = sizeof( gl.texcoord[u] );
			a->size = 2;
			a->type = GL_FLOAT;
			a->constant = true;
		}
	}

	return true;
}

static void gl_DrawArrays( GLenum mode, GLint first, GLsizei count )
{
	draw_attrib_t attribs[SEM_COUNT];

	if( !array_attribs( attribs ))
	{
		draw_dropped( "vertex arrays can't be read" );
		return;
	}

	draw_prims( mode, first, count, 0, NULL, attribs, gl.color );
}

static void gl_DrawElements( GLenum mode, GLsizei count, GLenum type, const GLvoid *indices )
{
	draw_attrib_t attribs[SEM_COUNT];
	const int isize = index_size( type );
	const uint8_t *isrc = indices;

	if( !isize )
	{
		set_error( GL_INVALID_ENUM );
		return;
	}

	if( gl.element_buffer )
	{
		const gl_buffer_t *eb = get_buffer( gl.element_buffer, false );

		if( !eb || !eb->data || (uintptr_t)indices + (size_t)count * isize > eb->size )
		{
			draw_dropped( "indices are out of element buffer" );
			return;
		}
		isrc = eb->data + (uintptr_t)indices;
	}

	if( !isrc )
	{
		draw_dropped( "no indices" );
		return;
	}

	if( !array_attribs( attribs ))
	{
		draw_dropped( "vertex arrays can't be read" );
		return;
	}

	draw_prims( mode, 0, count, type, isrc, attribs, gl.color );
}

static void gl_DrawRangeElements( GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid *indices )
{
	gl_DrawElements( mode, count, type, indices );
}

/*
==============================================================================

Clear and read back

==============================================================================
*/
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
	colordata = gnm_dev_alloc_transient( 16, 16 );
	table = gnm_dev_alloc_table( 16 );

	if( !colordata || !table )
	{
		draw_dropped( "clear: out of GPU memory" );
		return;
	}

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

	memcpy( colordata, gl.clear_color, 16 );
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
	static GpaSurfaceContext s_ctx;
	static const GnmRenderTarget *s_ctx_rt;
	static uint32_t s_pattern[64];
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

	if( s_ctx_rt != rt )
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
		if( sceGpaInitSurfaceContext( &s_ctx, size, &tp ) != GPA_ERR_OK )
		{
			GNM_LOG( "glReadPixels: no surface context for the color target\n" );
			return;
		}

		sceGpaComputeSurfaceCoord( &base, NULL, &s_ctx, 0, 0, 0, 0 );
		for( i = 0; i < 64; i++ )
		{
			sceGpaComputeSurfaceCoord( &off, NULL, &s_ctx, i & 7, i >> 3, 0, 0 );
			s_pattern[i] = (uint32_t)( off - base );
		}

		s_ctx_rt = rt;
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

		for( col = 0; col < width; col++ )
		{
			const int sx = x + col;
			const uint8_t *src;

			if( sx < 0 || sx >= gnm_dev.width )
				continue;

			if(( sx & ~7 ) != tile_x )
			{
				tile_x = sx & ~7;
				sceGpaComputeSurfaceCoord( &tile, NULL, &s_ctx, tile_x, sy & ~7, 0, 0 );
			}

			src = mem + tile + s_pattern[( sy & 7 ) * 8 + ( sx & 7 )];
			memcpy( dst + col * comps, src, comps );
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
	P( "glPixelStorei", gl_PixelStorei ),
	P( "glPixelStoref", gl_PixelStoref ),

	P( "glEnable", gl_Enable ),
	P( "glDisable", gl_Disable ),
	P( "glIsEnabled", gl_IsEnabled ),
	P( "glBlendFunc", gl_BlendFunc ),
	P( "glDepthFunc", gl_DepthFunc ),
	P( "glDepthMask", gl_DepthMask ),
	P( "glDepthRange", gl_DepthRange ),
	P( "glColorMask", gl_ColorMask ),
	P( "glCullFace", gl_CullFace ),
	P( "glFrontFace", gl_FrontFace ),
	P( "glPolygonOffset", gl_PolygonOffset ),
	P( "glViewport", gl_Viewport ),
	P( "glScissor", gl_Scissor ),
	P( "glClearColor", gl_ClearColor ),
	P( "glClearDepth", gl_ClearDepth ),
	P( "glClear", gl_Clear ),
	P( "glAlphaFunc", gl_AlphaFunc ),
	P( "glFogf", gl_Fogf ),
	P( "glFogi", gl_Fogi ),
	P( "glFogfv", gl_Fogfv ),
	P( "glTexEnvi", gl_TexEnvi ),
	P( "glTexEnvf", gl_TexEnvf ),
	P( "glTexEnvfv", gl_TexEnvfv ),
	P( "glTexGeni", gl_TexGeni ),
	P( "glHint", gl_Hint ),
	P( "glShadeModel", gl_ShadeModel ),
	P( "glPolygonMode", gl_PolygonMode ),
	P( "glLineWidth", gl_LineWidth ),
	P( "glPointSize", gl_PointSize ),
	P( "glDrawBuffer", gl_DrawBuffer ),
	P( "glReadBuffer", gl_ReadBuffer ),
	P( "glStencilFunc", gl_StencilFunc ),
	P( "glStencilOp", gl_StencilOp ),
	P( "glStencilMask", gl_StencilMask ),
	P( "glClearStencil", gl_ClearStencil ),
	P( "glReadPixels", gl_ReadPixels ),

	P( "glMatrixMode", gl_MatrixMode ),
	P( "glLoadIdentity", gl_LoadIdentity ),
	P( "glLoadMatrixf", gl_LoadMatrixf ),
	P( "glTranslatef", gl_Translatef ),
	P( "glScalef", gl_Scalef ),

	P( "glGenTextures", gl_GenTextures ),
	P( "glDeleteTextures", gl_DeleteTextures ),
	P( "glIsTexture", gl_IsTexture ),
	P( "glBindTexture", gl_BindTexture ),
	P( "glActiveTexture", gl_ActiveTexture ),
	P( "glActiveTextureARB", gl_ActiveTexture ),
	P( "glClientActiveTexture", gl_ClientActiveTexture ),
	P( "glClientActiveTextureARB", gl_ClientActiveTexture ),
	P( "glTexParameteri", gl_TexParameteri ),
	P( "glTexParameterf", gl_TexParameterf ),
	P( "glTexParameterfv", gl_TexParameterfv ),
	P( "glTexImage1D", gl_TexImage1D ),
	P( "glTexImage2D", gl_TexImage2D ),
	P( "glTexSubImage1D", gl_TexSubImage1D ),
	P( "glTexSubImage2D", gl_TexSubImage2D ),

	P( "glBegin", gl_Begin ),
	P( "glEnd", gl_End ),
	P( "glVertex2f", gl_Vertex2f ),
	P( "glVertex3f", gl_Vertex3f ),
	P( "glVertex3fv", gl_Vertex3fv ),
	P( "glColor3f", gl_Color3f ),
	P( "glColor4f", gl_Color4f ),
	P( "glColor4ub", gl_Color4ub ),
	P( "glColor4ubv", gl_Color4ubv ),
	P( "glTexCoord2f", gl_TexCoord2f ),
	P( "glTexCoord2fv", gl_TexCoord2fv ),
	P( "glTexCoord3f", gl_TexCoord3f ),
	P( "glMultiTexCoord1f", gl_MultiTexCoord1f ),
	P( "glMultiTexCoord2f", gl_MultiTexCoord2f ),
	P( "glMultiTexCoord3f", gl_MultiTexCoord3f ),
	P( "glMultiTexCoord4f", gl_MultiTexCoord4f ),
	P( "glNormal3fv", gl_Normal3fv ),

	P( "glVertexPointer", gl_VertexPointer ),
	P( "glColorPointer", gl_ColorPointer ),
	P( "glTexCoordPointer", gl_TexCoordPointer ),
	P( "glEnableClientState", gl_EnableClientState ),
	P( "glDisableClientState", gl_DisableClientState ),
	P( "glDrawArrays", gl_DrawArrays ),
	P( "glDrawElements", gl_DrawElements ),
	P( "glDrawRangeElements", gl_DrawRangeElements ),
	P( "glDrawRangeElementsEXT", gl_DrawRangeElements ),

	P( "glGenBuffersARB", gl_GenBuffers ),
	P( "glDeleteBuffersARB", gl_DeleteBuffers ),
	P( "glIsBufferARB", gl_IsBuffer ),
	P( "glBindBufferARB", gl_BindBuffer ),
	P( "glBufferDataARB", gl_BufferData ),
	P( "glBufferSubDataARB", gl_BufferSubData ),
	P( "glMapBufferARB", gl_MapBuffer ),
	P( "glUnmapBufferARB", gl_UnmapBuffer ),
};
#undef P

void *GNM_ShimGetProcAddress( const char *name )
{
	size_t i;

	for( i = 0; i < sizeof( procs ) / sizeof( procs[0] ); i++ )
	{
		if( !strcmp( procs[i].name, name ))
			return procs[i].func;
	}

	return NULL;
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

	for( i = 0; i < GNM_FF_VS_COUNT; i++ )
	{
		if( !gnm_dev_load_vs( &gl.vs[i], gnm_shader_ff_vs[i].data, gnm_shader_ff_vs[i].size ))
			return false;
	}

	for( i = 0; i < GNM_FF_PS_COUNT; i++ )
	{
		if( !gnm_dev_load_ps( &gl.ps[i], gnm_shader_ff_ps[i].data, gnm_shader_ff_ps[i].size ))
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
	int i;

	gl.error = GL_NO_ERROR;
	gl.next_texture = 1;
	gl.next_buffer = 1;
	gl.blend_src = GL_ONE;
	gl.blend_dst = GL_ZERO;
	gl.depth_func = GL_LESS;
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

	for( i = 0; i < MAT_COUNT; i++ )
		identity( gl.matrix[i] );
	gl.matrix_mode = MAT_MODELVIEW;
	gl.color[0] = gl.color[1] = gl.color[2] = gl.color[3] = 1.0f;
	gl.fog_density = 1.0f;

	restart_cb();
}

static void gnm_message( GnmMessageSeverity severity, const char *message, void *userdata )
{
	GNM_LOG( "opengnm %s: %s\n", severity == GNM_MSGSEV_ERR ? "error" : "warning", message );
}

int GNM_ShimInit( const gnm_dev_api_t *api )
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
void GNM_ShimEndFrame( void )
{
	if( !gl.initialized )
		return;

	if( gnm_dev.frame < 3 || gnm_dev.frame == 60 || gnm_dev.frame == 600 )
		GNM_LOG( "frame %u: %u draws, %u vertices\n", gnm_dev.frame, gl.draws, gl.verts );

	gl.draws = gl.verts = 0;
}

// the device is still open, the engine closes it after this
void GNM_ShimShutdown( void )
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
	free( gl.imm_pos );
	free( gl.imm_color );
	for( i = 0; i < MAX_TEXTURE_UNITS; i++ )
		free( gl.imm_tc[i] );
	free( gl.quad_indices );

	memset( &gl, 0, sizeof( gl ));
	// shaders and the white texture stay in GPU memory, they're loaded again on next init
}
