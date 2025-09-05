/*
gl2_shim.c - GL CORE/ES2+ FFP emulation
Copyright (C) 2023 mittorn, fgsfds

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

/*
based on vglshim, as it has almost all needed for using glbegins on gles
BufStorage mode should work similar to vglshim with vitagl's DRAW_SPEEDHACK
as it uses gpu-mapped in similar way

gl2shim will not give much performance gain. It does not batch small drawcalls, just do little copy optimization
It just allows to draw similar way as in native legacy context, but in es/core contexts

Limitations:
1. Matrix support is very limited, only combined MVP
2. No TexEnv support, multitexture always works in MODULATE mode
3. DrawElements with client pointers will not work on CORE/WEBGL contexts, DrawRangeElements will
4. No quads in arrays drawing (simple DrawArrays(GL_QUADS) may be implemented, but DrawElements not)
5. Textures are enabled with texcoord attribs, not glEnable (can be changed, but who cares?)
6. Begin/End limited to 8192 vertices. It is possible to support more, but need change glVertex logic to split drawcals, which may make it slower
7. Textures internalformat ignored except of removing alpha from RGB textures
*/

#include "gl_local.h"
#if !XASH_GL_STATIC
#include "gl2_shim.h"

#define MAX_SHADERLEN 4096
// increase this when adding more attributes
#define MAX_PROGS 32
// must be LESS GL2_MAX_VERTS
#define MAX_BEGINEND_VERTS 8192

enum gl2wrap_attrib_e
{
	GL2_ATTR_POS       = 0, // 1
	GL2_ATTR_COLOR,         // 2
	GL2_ATTR_TEXCOORD0,     // 4
	GL2_ATTR_TEXCOORD1,     // 8
	GL2_ATTR_MAX
};

// continuation of previous enum
enum gl2wrap_flag_e
{
	GL2_FLAG_ALPHA_TEST = GL2_ATTR_MAX, // 16
	GL2_FLAG_FOG,                       // 32
	GL2_FLAG_NORMAL,                    // 64
	GL2_FLAG_MAX
};

typedef struct
{
	GLuint flags;
	GLint attridx[GL2_ATTR_MAX];
	GLuint glprog;
	GLint ucolor;
	GLint ualpha;
	GLint utex0;
	GLint utex1;
	GLint ufog;
	GLint uMVP;
	GLuint *vao_begin;
} gl2wrap_prog_t;

static const char *gl2wrap_vert_src =
#include "vertex.glsl.inc"
;

static const char *gl2wrap_frag_src =
#include "fragment.glsl.inc"
;

static int gl2wrap_init = 0;

static struct
{
	GLfloat *attrbuf[GL2_ATTR_MAX];
	GLuint *attrbufobj[GL2_ATTR_MAX];
	void **mappings[GL2_ATTR_MAX];
	//GLuint attrbufpers[GL2_ATTR_MAX];
	GLuint attrbufcycle;
	GLuint cur_flags;
	GLint begin;
	GLint end;
	GLenum prim;
	GLfloat color[4];
	GLfloat fog[4]; // color + density
	GLfloat alpharef;
	gl2wrap_prog_t progs[MAX_PROGS];
	gl2wrap_prog_t *cur_prog;
	GLboolean uchanged;
	GLuint triquads_ibo[4];
} gl2wrap;

static struct
{
	qboolean buf_storage; // buffer storage is enabled, buffers mapped persistently (zero-copy glBegins)
	qboolean incremental; // incremental buffer streaming
	qboolean supports_mapbuffer; // set to false on systems with mapbuffer issues
	qboolean vao_mandatory; // even if incremental streaming unavailiable (it is very slow without mapbuffers) force VAO+VBO (WebGL-like or broken glcore)
	qboolean coherent; // enable MAP_COHERENT_BIT on persist mappings
	qboolean async; // enable MAP_UNSYNCHRONIZED_BIT on temporary mappings
	qboolean force_flush; // enable MAP_FLUSH_EXPLICIT_BIT and FlushMappedBufferRange calls
	uint32_t cycle_buffers; // cycle N buffers during draw to reduce locking in non-incremental mode
	uint32_t version; // glsl version to use
} gl2wrap_config;

static struct
{
	float mvp[16], mv[16], pr[16], dummy[16];
	GLenum mode;
	float *current;
	uint64_t update;
} gl2wrap_matrix;

static struct
{
	qboolean alpha_test;
	qboolean fog;
	GLuint vbo;
	GLuint tmu;
} gl2wrap_state;

//#define QUAD_BATCH

#ifdef QUAD_BATCH
static struct
{
	unsigned int texture;
	unsigned int flags;
	GLboolean active;
} gl2wrap_quad;
#endif

static const int gl2wrap_attr_size[GL2_ATTR_MAX] = { 3, 4, 2, 2 };

static const char *gl2wrap_flag_name[GL2_FLAG_MAX] =
{
	"ATTR_POSITION",
	"ATTR_COLOR",
	"ATTR_TEXCOORD0",
	"ATTR_TEXCOORD1",
	"FEAT_ALPHA_TEST",
	"FEAT_FOG",
	"ATTR_NORMAL",
};

static const char *gl2wrap_attr_name[GL2_ATTR_MAX] =
{
	"inPosition",
	"inColor",
	"inTexCoord0",
	"inTexCoord1",
};

#define MB( x, y ) (( x ) ? GL_MAP_##y##_BIT : 0 )

static void (APIENTRY *rpglEnable)( GLenum e );
static void (APIENTRY *rpglDisable)( GLenum e );
static void (APIENTRY *rpglDrawElements )( GLenum mode, GLsizei count, GLenum type, const GLvoid *indices );
static void (APIENTRY *rpglDrawArrays )( GLenum mode, GLint first, GLsizei count );
static void (APIENTRY *rpglDrawRangeElements )( GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid *indices );
static void (APIENTRY *rpglBindBufferARB)( GLenum buf, GLuint obj );

static void GL2_FreeArrays( void );

#ifdef QUAD_BATCH
static void GL2_FlushPrims( void );
static void (APIENTRY *rpglBindTexture)( GLenum tex, GLuint obj );
static void APIENTRY GL2_BindTexture( GLenum tex, GLuint obj )
{
	if( gl2wrap_quad.texture != obj )
	{
		GL2_FlushPrims();
		gl2wrap_quad.texture = obj;
	}
	rpglBindTexture( tex, obj );
}
#endif

static char *GL_PrintInfoLog( GLhandleARB object, qboolean program )
{
	static char	msg[8192];
	GLuint maxLength = 0;

	if( program && pglGetProgramiv )
		pglGetProgramiv( object, GL_OBJECT_INFO_LOG_LENGTH_ARB, &maxLength );
	else
		pglGetObjectParameterivARB( object, GL_OBJECT_INFO_LOG_LENGTH_ARB, &maxLength );

	if( maxLength >= sizeof( msg ))
	{
		//ALERT( at_warning, "GL_PrintInfoLog: message exceeds %i symbols\n", sizeof( msg ));
		maxLength = sizeof( msg ) - 1;
	}

	if( program && pglGetProgramInfoLog )
		pglGetProgramInfoLog( object, maxLength, &maxLength, msg );
	else
		pglGetInfoLogARB( object, maxLength, &maxLength, msg );

	return msg;
}


static GLuint GL2_GenerateShader( gl2wrap_prog_t *prog, GLenum type )
{
	char *shader, shader_buf[MAX_SHADERLEN + 1];
	char tmp[256];
	int i;
	GLint status, len;
	GLuint id, loc;
	int version = gl2wrap_config.version;

	shader = shader_buf;
	//shader[0] = '\n';
	shader[0] = 0;

	Q_snprintf( shader, MAX_SHADERLEN, "#version %d%s\n", version, version >= 300 && version < 330 ? " es" : "" );

	Q_snprintf( tmp, sizeof( tmp ), "#define VER %d\n", version );
	Q_strncat( shader, tmp, MAX_SHADERLEN );

	for( i = 0; i < GL2_FLAG_MAX; ++i )
	{
		Q_snprintf( tmp, sizeof( tmp ), "#define %s %d\n", gl2wrap_flag_name[i], FBitSet( prog->flags, BIT( i )));
		Q_strncat( shader, tmp, MAX_SHADERLEN );
	}

	if( version >= 310 )
	{
		loc = 0;
		for( i = 0; i < GL2_ATTR_MAX; ++i )
		{
			if( FBitSet( prog->flags, BIT( i )))
			{
				Q_snprintf( tmp, sizeof( tmp ), "#define LOC_%s %d\n", gl2wrap_flag_name[i], loc++ );
				Q_strncat( shader, tmp, MAX_SHADERLEN );
				prog->attridx[i] = loc;

			}
			else
			{
				prog->attridx[i] = -1;
			}
		}
	}

	if( type == GL_FRAGMENT_SHADER_ARB )
		Q_strncat( shader, gl2wrap_frag_src, MAX_SHADERLEN );
	else
		Q_strncat( shader, gl2wrap_vert_src, MAX_SHADERLEN );

	id = pglCreateShaderObjectARB( type );
	len = Q_strlen( shader );
	pglShaderSourceARB( id, 1, (void *)&shader, &len );
	pglCompileShaderARB( id );
	pglGetObjectParameterivARB( id, GL_OBJECT_COMPILE_STATUS_ARB, &status );

	if( status == GL_FALSE )
	{
		gEngfuncs.Con_Reportf( S_ERROR "%s( 0x%04x, 0x%x ): compile failed: %s\n", __func__, prog->flags, type, GL_PrintInfoLog( id, false ));

		gEngfuncs.Con_DPrintf( "Shader text:\n%s\n\n", shader );
		pglDeleteObjectARB( id );
		return 0;
	}

	return id;
}

static gl2wrap_prog_t *GL2_GetProg( const GLuint flags )
{
	int i, loc;
	GLuint status = 0, vp, fp, glprog;
	gl2wrap_prog_t *prog;

	// try to find existing prog matching this feature set

	if( gl2wrap.cur_prog && gl2wrap.cur_prog->flags == flags )
		return gl2wrap.cur_prog;

	for( i = 0; i < MAX_PROGS; ++i )
	{
		if( gl2wrap.progs[i].flags == flags )
			return &gl2wrap.progs[i];
		else if( gl2wrap.progs[i].flags == 0 )
			break;
	}

	if( i == MAX_PROGS )
	{
		gEngfuncs.Host_Error( "%s: Ran out of program slots for 0x%04x\n", __func__, flags );
		return NULL;
	}

	// new prog; generate shaders

	gEngfuncs.Con_DPrintf( S_NOTE "%s: Generating progs for 0x%04x\n", __func__, flags );
	prog = &gl2wrap.progs[i];
	prog->flags = flags;

	vp = GL2_GenerateShader( prog, GL_VERTEX_SHADER_ARB );
	fp = GL2_GenerateShader( prog, GL_FRAGMENT_SHADER_ARB );
	if( !vp || !fp )
	{
		prog->flags = 0;
		return NULL;
	}

	glprog = pglCreateProgramObjectARB();
	pglAttachObjectARB( glprog, vp );
	pglAttachObjectARB( glprog, fp );

	loc = 0;
	for( i = 0; i < GL2_ATTR_MAX; ++i )
	{
		if( FBitSet( flags, BIT( i )))
		{
			prog->attridx[i] = loc;
			if( gl2wrap_config.version <= 300 )
				pglBindAttribLocationARB( glprog, loc++, gl2wrap_attr_name[i] );
			else
				loc++;
		}
		else
		{
			prog->attridx[i] = -1;
		}
	}

	pglLinkProgramARB( glprog );
	pglDetachObjectARB( glprog, vp );
	pglDetachObjectARB( glprog, fp );
	pglDeleteObjectARB( vp );
	pglDeleteObjectARB( fp );

/// TODO: detect arb/core shaders in engine

	if( pglGetProgramiv )
		pglGetProgramiv( glprog, GL_OBJECT_LINK_STATUS_ARB, &status );
	else
		pglGetObjectParameterivARB( glprog, GL_OBJECT_LINK_STATUS_ARB, &status );

	if( status == GL_FALSE )
	{
		gEngfuncs.Con_Reportf( S_ERROR "%s: Failed linking progs for 0x%04x!\n%s\n", __func__, prog->flags, GL_PrintInfoLog( glprog, true ));
		prog->flags = 0;
		if( pglDeleteProgram )
			pglDeleteProgram( glprog );
		else
			pglDeleteObjectARB( glprog );
		return NULL;
	}

	prog->ucolor = pglGetUniformLocationARB( glprog, "uColor" );
	prog->ualpha = pglGetUniformLocationARB( glprog, "uAlphaTest" );
	prog->utex0  = pglGetUniformLocationARB( glprog, "uTex0" );
	prog->utex1  = pglGetUniformLocationARB( glprog, "uTex1" );
	prog->ufog   = pglGetUniformLocationARB( glprog, "uFog" );
	prog->uMVP   = pglGetUniformLocationARB( glprog, "uMVP" );

	if( gl2wrap_config.vao_mandatory )
	{
		prog->vao_begin = Mem_Calloc( r_temppool, gl2wrap_config.cycle_buffers * sizeof( GLuint ));
		pglGenVertexArrays( gl2wrap_config.cycle_buffers, prog->vao_begin );
	}
	pglUseProgramObjectARB( glprog );
	for( i = 0; i < GL2_ATTR_MAX; ++i )
	{
		if( prog->attridx[i] >= 0 )
		{
			if( gl2wrap_config.vao_mandatory || gl2wrap_config.incremental )
			{
				int j;

				for( j = 0; j < gl2wrap_config.cycle_buffers; j++ )
				{
					pglBindVertexArray( prog->vao_begin[j] );
					pglEnableVertexAttribArrayARB( prog->attridx[i] );
					pglBindBufferARB( GL_ARRAY_BUFFER_ARB, gl2wrap.attrbufobj[i][j] );
					pglVertexAttribPointerARB( prog->attridx[i], gl2wrap_attr_size[i], GL_FLOAT, GL_FALSE, 0, 0 );
				}
			}
		}
	}
	if( gl2wrap_config.vao_mandatory )
		pglBindVertexArray( 0 );

	// these never change
	if( FBitSet( prog->flags, BIT( GL2_ATTR_TEXCOORD0 )) && prog->utex0 >= 0 )
		pglUniform1iARB( prog->utex0, 0 );
	if( FBitSet( prog->flags, BIT( GL2_ATTR_TEXCOORD1 )) && prog->utex1 >= 0 )
		pglUniform1iARB( prog->utex1, 1 );
	if( gl2wrap.cur_prog )
		pglUseProgramObjectARB( gl2wrap.cur_prog->glprog );
	prog->glprog = glprog;

	gEngfuncs.Con_DPrintf( S_NOTE "%s: Generated progs for 0x%04x\n", __func__, flags );

	return prog;
}

static void GL2_UpdateMVP( gl2wrap_prog_t *prog );
static gl2wrap_prog_t *GL2_SetProg( const GLuint flags )
{
	gl2wrap_prog_t *prog = NULL;

	if( flags && ( prog = GL2_GetProg( flags )))
	{
		if( prog != gl2wrap.cur_prog )
		{
			pglUseProgramObjectARB( prog->glprog );
			gl2wrap.uchanged = GL_TRUE;
		}
		if( gl2wrap.uchanged )
		{
			if( prog->ualpha >= 0 )
				pglUniform1fARB( prog->ualpha, gl2wrap.alpharef );
			if( prog->ucolor >= 0 )
				pglUniform4fvARB( prog->ucolor, 1, gl2wrap.color );
			if( prog->ufog >= 0 )
				pglUniform4fvARB( prog->ufog, 1, gl2wrap.fog );
			gl2wrap.uchanged = GL_FALSE;
		}
		GL2_UpdateMVP( prog );
	}
	else
	{
		pglUseProgramObjectARB( 0 );
	}

	gl2wrap.cur_prog = prog;
	return prog;
}

#define TRIQUADS_SIZE GL2_MAX_VERTS / 4 * 6

static void GL2_InitTriQuads( void )
{
	int i;
	for( i = 0; i < ( !!pglDrawRangeElementsBaseVertex ? 1 : 4 ); i++ )
	{
		int j;
		GLushort triquads_array[TRIQUADS_SIZE];

		for( j = 0; j < TRIQUADS_SIZE / 6; j++ )
		{
			triquads_array[j * 6] = j * 4 + i;
			triquads_array[j * 6 + 1] = j * 4 + 1 + i;
			triquads_array[j * 6 + 2] = j * 4 + 2 + i;
			triquads_array[j * 6 + 3] = j * 4 + i;
			triquads_array[j * 6 + 4] = j * 4 + 2 + i;
			triquads_array[j * 6 + 5] = j * 4 + 3 + i;
		}
		pglGenBuffersARB( 1, &gl2wrap.triquads_ibo[i] );
		rpglBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, gl2wrap.triquads_ibo[i] );
		pglBufferDataARB( GL_ELEMENT_ARRAY_BUFFER_ARB, sizeof( triquads_array ), triquads_array, GL_STATIC_DRAW_ARB );
	}

	rpglBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, 0 );
}

static void GL2_InitIncrementalBuffer( int i, GLuint size )
{
	int j;

	gl2wrap.attrbufobj[i] = Mem_Calloc( r_temppool, gl2wrap_config.cycle_buffers * sizeof( GLuint ));
	if( gl2wrap_config.buf_storage )
		gl2wrap.mappings[i] = Mem_Calloc( r_temppool, gl2wrap_config.cycle_buffers * sizeof( void * ));
	pglGenBuffersARB( gl2wrap_config.cycle_buffers, gl2wrap.attrbufobj[i] );

	for( j = 0; j < gl2wrap_config.cycle_buffers; j++ )
	{
		rpglBindBufferARB( GL_ARRAY_BUFFER_ARB, gl2wrap.attrbufobj[i][j] );
		if( gl2wrap_config.buf_storage )
		{
			GLuint flags = GL_MAP_WRITE_BIT | MB( !gl2wrap_config.coherent, FLUSH_EXPLICIT ) |
				GL_MAP_PERSISTENT_BIT | MB( gl2wrap_config.coherent, COHERENT );
			pglBufferStorage( GL_ARRAY_BUFFER_ARB, size, NULL, GL_MAP_WRITE_BIT | MB( gl2wrap_config.coherent, COHERENT ) | GL_MAP_PERSISTENT_BIT );
			gl2wrap.mappings[i][j] = pglMapBufferRange( GL_ARRAY_BUFFER_ARB, 0, size, flags );
		}
		else
			pglBufferDataARB( GL_ARRAY_BUFFER_ARB, size, NULL, GL_STREAM_DRAW_ARB );
	}
	if( gl2wrap_config.buf_storage )
		gl2wrap.attrbuf[i] = gl2wrap.mappings[i][0];
}


static qboolean GL2_InitProgs( void )
{
	static const GLuint precache_progs[] = {
		BIT( GL2_ATTR_POS ),                                                                                // out = ucolor
		BIT( GL2_ATTR_POS ) | BIT( GL2_ATTR_TEXCOORD0 ),                                                    // out = tex0 * ucolor
		BIT( GL2_ATTR_POS ) | BIT( GL2_ATTR_TEXCOORD0 ) | BIT( GL2_ATTR_COLOR ),                            // out = tex0 * vcolor
		BIT( GL2_ATTR_POS ) | BIT( GL2_ATTR_TEXCOORD0 ) | BIT( GL2_FLAG_ALPHA_TEST ),                       // out = tex0 * ucolor + FEAT_ALPHA_TEST
		BIT( GL2_ATTR_POS ) | BIT( GL2_FLAG_FOG ),                                                          // out = ucolor + FEAT_FOG
		BIT( GL2_ATTR_POS ) | BIT( GL2_ATTR_TEXCOORD0 ) | BIT( GL2_FLAG_FOG ),                              // out = tex0 * ucolor + FEAT_FOG
		BIT( GL2_ATTR_POS ) | BIT( GL2_ATTR_TEXCOORD0 ) | BIT( GL2_ATTR_COLOR ) | BIT( GL2_FLAG_FOG ),      // out = tex0 * vcolor + FEAT_FOG
		BIT( GL2_ATTR_POS ) | BIT( GL2_ATTR_TEXCOORD0 ) | BIT( GL2_FLAG_ALPHA_TEST ) | BIT( GL2_FLAG_FOG ), // out = tex0 * ucolor + FEAT_ALPHA_TEST + FEAT_FOG
	};
	const size_t precache_progs_count = sizeof( precache_progs ) / sizeof( precache_progs[0] );
	int i;

	gEngfuncs.Con_DPrintf( S_NOTE "GL2_InitProgs: Pre-generating %u progs, version %d...\n", (uint)( precache_progs_count ), gl2wrap_config.version );
	for( i = 0; i < (int)( precache_progs_count ); ++i )
		if( !GL2_GetProg( precache_progs[i] ))
				return false;
	return true;
}


int GL2_ShimInit( void )
{
	int i;
	GLuint total;

	if( gl2wrap_init )
		return 0;

	if( !pglBindBufferARB )
	{
		gEngfuncs.Con_Printf( S_ERROR "GL2_ShimInit: missing VBO, disabling\n" );
		return 1;
	}

	if( !pglCompileShaderARB )
	{
		gEngfuncs.Con_Printf( S_ERROR "GL2_ShimInit: missing shaders, disabling\n" );
		return 1;
	}

	gl2wrap_config.vao_mandatory = gEngfuncs.Sys_CheckParm( "-vao" ) || glConfig.context == CONTEXT_TYPE_GL_CORE;
	gl2wrap_config.incremental = true;
	gl2wrap_config.async = true;
	gl2wrap_config.force_flush = false;
	gl2wrap_config.buf_storage = true;
	gl2wrap_config.coherent = true;
	gl2wrap_config.supports_mapbuffer = true;
	gl2wrap_config.cycle_buffers = 4096;

	if( !pglBufferStorage )
	{
		gl2wrap_config.buf_storage = false;
		gEngfuncs.Con_Printf( S_NOTE "GL2_ShimInit: missing BufferStorage\n" );
	}

	if( !pglMapBufferRange )
	{
		gl2wrap_config.incremental = false;
		gl2wrap_config.supports_mapbuffer = false;
		gEngfuncs.Con_Printf( S_NOTE "GL2_ShimInit: missing MapBufferRange, disabling incremental rendering\n" );
	}

	if( gEngfuncs.Sys_CheckParm( "-nocoherent" ))
		gl2wrap_config.coherent = false;
	if( gEngfuncs.Sys_CheckParm( "-nobufstor" ))
		gl2wrap_config.buf_storage = false;
	if( gEngfuncs.Sys_CheckParm( "-noasync" ))
		gl2wrap_config.async = false;
	if( gEngfuncs.Sys_CheckParm( "-forceflush" ))
		gl2wrap_config.force_flush = true;
	if( gEngfuncs.Sys_CheckParm( "-nomapbuffer" ))
		gl2wrap_config.supports_mapbuffer = false;
	if( gEngfuncs.Sys_CheckParm( "-noincremental" ))
		gl2wrap_config.incremental = gl2wrap_config.buf_storage = false;

	gl2wrap_config.version = 310;
	if( gEngfuncs.Sys_CheckParm( "-minshaders" ))
		gl2wrap_config.version = 100;
	if( gl2wrap_config.buf_storage )
		gl2wrap_config.incremental = gl2wrap_config.vao_mandatory = true;
	if( !pglBindVertexArray || !gl2wrap_config.vao_mandatory )
		gl2wrap_config.incremental = gl2wrap_config.buf_storage = gl2wrap_config.vao_mandatory = false;
	if( gl2wrap_config.incremental && !gl2wrap_config.buf_storage )
		gl2wrap_config.async = true;
	if( gl2wrap_config.incremental )
		gl2wrap_config.cycle_buffers = 4;
	if( !gl2wrap_config.vao_mandatory )
		gl2wrap_config.cycle_buffers = 1;
	gEngfuncs.Con_Printf( S_NOTE "GL2_ShimInit: config: %s%s%s%s%s%s%sCYCLE=%d VER=%d\n",
		gl2wrap_config.buf_storage ? "BUF_STOR " : "",
		gl2wrap_config.buf_storage&&gl2wrap_config.coherent ? "COHERENT " : "",
		gl2wrap_config.async ? "ASYNC " : "",
		gl2wrap_config.incremental ? "INC " : "",
		gl2wrap_config.force_flush ? "FLUSH " : "",
		gl2wrap_config.vao_mandatory ? "VAO " : "",
		gl2wrap_config.supports_mapbuffer ? "MAP " : "",
		gl2wrap_config.cycle_buffers, gl2wrap_config.version );

	memset( &gl2wrap, 0, sizeof( gl2wrap ));
	GL2_ShimInstall();
	GL2_InitTriQuads();

	gl2wrap.color[0] = 1.f;
	gl2wrap.color[1] = 1.f;
	gl2wrap.color[2] = 1.f;
	gl2wrap.color[3] = 1.f;
	gl2wrap.uchanged = GL_TRUE;

	total = 0;

	for( i = 0; i < GL2_ATTR_MAX; ++i )
	{
		GLuint size = GL2_MAX_VERTS * gl2wrap_attr_size[i] * sizeof( GLfloat );
		if( !gl2wrap_config.buf_storage )
		{
			gl2wrap.attrbuf[i] = Mem_Calloc( r_temppool, size );
		}

		if( gl2wrap_config.incremental )
		{
			GL2_InitIncrementalBuffer( i, size );
		}
		else
		{
			if( !gl2wrap_config.incremental && gl2wrap_config.vao_mandatory )
			{
				gl2wrap.attrbufobj[i] = malloc( gl2wrap_config.cycle_buffers * 4 );
				pglGenBuffersARB( gl2wrap_config.cycle_buffers, gl2wrap.attrbufobj[i] );
				if( gl2wrap_config.supports_mapbuffer )
				{
					int j;

					for( j = 0; j < gl2wrap_config.cycle_buffers; j++ )
					{
						rpglBindBufferARB( GL_ARRAY_BUFFER_ARB, gl2wrap.attrbufobj[i][j] );
						pglBufferDataARB( GL_ARRAY_BUFFER_ARB, MAX_BEGINEND_VERTS, NULL, GL_STREAM_DRAW_ARB );
					}
				}
			}
		}

		total += size;
	}
	if( gl2wrap_config.vao_mandatory )
		pglBindVertexArray( 0 );
	rpglBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );

	gEngfuncs.Con_DPrintf( S_NOTE "%s: %u bytes allocated for vertex buffer\n", __func__, total );

	if( !GL2_InitProgs( ))
	{
		gl2wrap_config.version = 300;
		if( !GL2_InitProgs( ))
		{
			gl2wrap_config.version = 110;
			if( !GL2_InitProgs( ))
			{
				gl2wrap_config.version = 100;
				if( !GL2_InitProgs( ))
					gEngfuncs.Host_Error( "%s: Failed to compile shaders!\n", __func__ );
			}
		}
	}

	gl2wrap_init = 1;
	return 0;
}

void GL2_ShimShutdown( void )
{
	int i;

	if( !gl2wrap_init )
		return;

	pglFinish();
	pglUseProgramObjectARB( 0 );
	GL2_FreeArrays();
	pglDeleteBuffersARB(( !!pglDrawRangeElementsBaseVertex ? 1 : 4 ), gl2wrap.triquads_ibo );

	for( i = 0; i < MAX_PROGS; ++i )
	{
		if( gl2wrap.progs[i].flags )
		{
			pglDeleteProgram( gl2wrap.progs[i].glprog );
			if( gl2wrap.progs[i].vao_begin )
			{
				pglDeleteVertexArrays( gl2wrap_config.cycle_buffers, gl2wrap.progs[i].vao_begin );
				Mem_Free( gl2wrap.progs[i].vao_begin );
			}
		}

	}

	for( i = 0; i < GL2_ATTR_MAX; ++i )
	{
		int j;
		if( gl2wrap_config.buf_storage )
		{
			for( j = 0; j < gl2wrap_config.cycle_buffers; j++ )
			{
				pglBindBufferARB( GL_ARRAY_BUFFER_ARB, gl2wrap.attrbufobj[i][j] );
				pglUnmapBufferARB( GL_ARRAY_BUFFER_ARB );
			}
		}
		if( gl2wrap.attrbufobj[i] )
		{
			pglDeleteBuffersARB( gl2wrap_config.cycle_buffers, gl2wrap.attrbufobj[i] );
			Mem_Free( gl2wrap.attrbufobj[i] );
		}
		if( gl2wrap.mappings[i] )
			Mem_Free( gl2wrap.mappings[i] );

		if( !gl2wrap_config.buf_storage )
			Mem_Free( gl2wrap.attrbuf[i] );
	}

	memset( &gl2wrap, 0, sizeof( gl2wrap ));

	gl2wrap_init = 0;
}

static void GL2_ResetPersistentBuffer( void )
{
	int i;

#ifdef QUAD_BATCH
	GL2_FlushPrims();
#endif
	gl2wrap.end = gl2wrap.begin = 0;

	if( gl2wrap_config.incremental )
	{
		gl2wrap.attrbufcycle = ( gl2wrap.attrbufcycle + 1 ) % gl2wrap_config.cycle_buffers;
		for( i = 0; i < GL2_ATTR_MAX; ++i )
		{
			int size = GL2_MAX_VERTS * gl2wrap_attr_size[i] * sizeof( GLfloat );
			rpglBindBufferARB( GL_ARRAY_BUFFER_ARB, gl2wrap.attrbufobj[i][gl2wrap.attrbufcycle] );
			if( gl2wrap_config.buf_storage )
			{
				GLuint flags = GL_MAP_WRITE_BIT | MB( !gl2wrap_config.coherent, FLUSH_EXPLICIT ) |
					GL_MAP_PERSISTENT_BIT | MB( gl2wrap_config.coherent, COHERENT );
				pglUnmapBufferARB( GL_ARRAY_BUFFER_ARB );
				gl2wrap.mappings[i][gl2wrap.attrbufcycle] = pglMapBufferRange( GL_ARRAY_BUFFER_ARB, 0, size, flags );
				gl2wrap.attrbuf[i] = gl2wrap.mappings[i][gl2wrap.attrbufcycle];
			}
			else
			{
				void *mem = pglMapBufferRange( GL_ARRAY_BUFFER_ARB, 0, size, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT );
				(void)mem;
				pglUnmapBufferARB( GL_ARRAY_BUFFER_ARB );
			}

		}
	}
}


void GL2_ShimEndFrame( void )
{
#ifdef QUAD_BATCH
	GL2_FlushPrims();
#endif
}

static void APIENTRY GL2_Begin( GLenum prim )
{
	int i;
	if( gl2wrap.begin + MAX_BEGINEND_VERTS > GL2_MAX_VERTS )
		GL2_ResetPersistentBuffer();

#ifdef QUAD_BATCH
	if( gl2wrap.prim == GL_QUADS && gl2wrap_quad.active )
	{
		GLuint flags = gl2wrap.cur_flags;
		GLuint flags2 = gl2wrap.cur_flags;

		if( gl2wrap_quad.flags != flags || prim != GL_QUADS )
			GL2_FlushPrims();
		else if( gl2wrap_quad.flags == flags && prim == GL_QUADS )
			return;
	}
	gl2wrap_quad.active = false;
#endif
	gl2wrap.prim = prim;
	gl2wrap.begin = gl2wrap.end;
	// pos always enabled
	SetBits( gl2wrap.cur_flags, BIT( GL2_ATTR_POS ));
}

/*
==============================
UpdateIncrementalBuffer

glBufferStorage allows to write directly without mapping buffer every time before draw
This allows keep VAO unchanged and fastly build needed pipeline in driver for every program variant
When buffer storage not supported, we still may use cached VAO, but map/unmap it every time writing new data
==============================
*/
static void GL2_UpdateIncrementalBuffer( gl2wrap_prog_t *prog, int count )
{
	int i;
	if( !gl2wrap_config.buf_storage )
	{
		for( i = 0; i < GL2_ATTR_MAX; i++ )
		{
			if( prog->attridx[i] >= 0 )
			{
				void *mem;
				GLuint flags = GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT |
					MB( gl2wrap_config.async, UNSYNCHRONIZED ) |
					MB( gl2wrap_config.force_flush, FLUSH_EXPLICIT );
				rpglBindBufferARB( GL_ARRAY_BUFFER_ARB, gl2wrap.attrbufobj[i][gl2wrap.attrbufcycle] );
				mem = pglMapBufferRange( GL_ARRAY_BUFFER_ARB, gl2wrap_attr_size[i] * 4 * gl2wrap.begin, gl2wrap_attr_size[i] * 4 * count, flags );
				memcpy( mem, gl2wrap.attrbuf[i] + gl2wrap_attr_size[i] * gl2wrap.begin, gl2wrap_attr_size[i] * 4 * count );
				if( gl2wrap_config.force_flush )
					pglFlushMappedBufferRange( GL_ARRAY_BUFFER_ARB, 0, gl2wrap_attr_size[i] * 4 * count );
				pglUnmapBufferARB( GL_ARRAY_BUFFER_ARB );
			}
		}
	}
	else if( !gl2wrap_config.coherent )
	{
		// non-coherent buffers anyway require unmapping or flushing after write
		for( i = 0; i < GL2_ATTR_MAX; i++ )
		{
			if( prog->attridx[i] >= 0 )
			{
				rpglBindBufferARB( GL_ARRAY_BUFFER_ARB, gl2wrap.attrbufobj[i][gl2wrap.attrbufcycle] );
				pglFlushMappedBufferRange( GL_ARRAY_BUFFER_ARB, 0, gl2wrap_attr_size[i] * 4 * count );
			}
		}
	}
}

static void GL2_FlushPrims( void )
{
	int i;
	int startindex = 0;
	GLuint flags = gl2wrap.cur_flags;
	GLint count = gl2wrap.end - gl2wrap.begin;
	gl2wrap_prog_t *prog;

	if( !gl2wrap.prim || !count )
		goto leave_label; // end without begin

	// enable alpha test and fog if needed
	if( gl2wrap_state.alpha_test )
		SetBits( flags, BIT( GL2_FLAG_ALPHA_TEST ));
	if( gl2wrap_state.fog )
		SetBits( flags, BIT( GL2_FLAG_FOG ));

	// disable all vertex attrib pointers
	if( !gl2wrap_config.vao_mandatory )
	{
		for( i = 0; i < GL2_ATTR_MAX; ++i )
			pglDisableVertexAttribArrayARB( i );
	}

	prog = GL2_SetProg( flags );
	if( !prog )
	{
		gEngfuncs.Host_Error( "%s: Could not find program for flags 0x%04x!\n", __func__, flags );
		goto leave_label;
	}

	if( gl2wrap_config.incremental )
	{
		GL2_UpdateIncrementalBuffer( prog, count );
		pglBindVertexArray( prog->vao_begin[gl2wrap.attrbufcycle] );
		startindex = gl2wrap.begin;
	}
	else
	{
		if( gl2wrap_config.vao_mandatory )
			pglBindVertexArray( prog->vao_begin[gl2wrap.attrbufcycle] );
		for( i = 0; i < GL2_ATTR_MAX; ++i )
		{
			if( prog->attridx[i] >= 0 )
			{
				if(( gl2wrap_config.vao_mandatory && !gl2wrap_config.supports_mapbuffer ) || !gl2wrap_config.vao_mandatory )
					pglEnableVertexAttribArrayARB( prog->attridx[i] );
				if( gl2wrap_config.vao_mandatory )
				{
					pglBindBufferARB( GL_ARRAY_BUFFER_ARB, gl2wrap.attrbufobj[i][gl2wrap.attrbufcycle] );
					if( gl2wrap_config.supports_mapbuffer )
					{
						if( gl2wrap_attr_size[i] * 4 * count > MAX_BEGINEND_VERTS )
						{
							pglBufferDataARB( GL_ARRAY_BUFFER_ARB, gl2wrap_attr_size[i] * 4 * count, gl2wrap.attrbuf[i] + gl2wrap_attr_size[i] * gl2wrap.begin, GL_STREAM_DRAW_ARB );
							pglEnableVertexAttribArrayARB( prog->attridx[i] );
							pglVertexAttribPointerARB( prog->attridx[i], gl2wrap_attr_size[i], GL_FLOAT, GL_FALSE, 0, 0 );
						}
						else
						{
							GLuint flags = GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT |
								MB( gl2wrap_config.async, UNSYNCHRONIZED ) | MB( gl2wrap_config.force_flush, FLUSH_EXPLICIT );
							void *mem = pglMapBufferRange( GL_ARRAY_BUFFER_ARB, 0, gl2wrap_attr_size[i] * 4 * count, flags );
							memcpy( mem, gl2wrap.attrbuf[i] + gl2wrap_attr_size[i] * gl2wrap.begin, gl2wrap_attr_size[i] * 4 * count );
							pglUnmapBufferARB( GL_ARRAY_BUFFER_ARB );
						}
					}
					else
						pglBufferDataARB( GL_ARRAY_BUFFER_ARB, gl2wrap_attr_size[i] * 4 * count, gl2wrap.attrbuf[i] + gl2wrap_attr_size[i] * gl2wrap.begin, GL_STREAM_DRAW_ARB );

					if( gl2wrap_config.vao_mandatory && !gl2wrap_config.supports_mapbuffer )
						pglVertexAttribPointerARB( prog->attridx[i], gl2wrap_attr_size[i], GL_FLOAT, GL_FALSE, 0, 0 );

				}
				else // if vao is not mandatory, try use client pointers here
					pglVertexAttribPointerARB( prog->attridx[i], gl2wrap_attr_size[i], GL_FLOAT, GL_FALSE, 0, gl2wrap.attrbuf[i] + gl2wrap_attr_size[i] * gl2wrap.begin );
			}
		}
		gl2wrap.attrbufcycle = ( gl2wrap.attrbufcycle + 1 ) % gl2wrap_config.cycle_buffers;
	}

	if( gl2wrap.prim == GL_QUADS )
	{
		// simple case, one quad may draw like polygon(4)
		if( count == 4 )
			rpglDrawArrays( GL_TRIANGLE_FAN, startindex, count );
		else if( pglDrawRangeElementsBaseVertex )
		{
			/*
			 * OpenGL deprecated QUADS, but made some workarounds availiable
			 * idea: bound static index array that will repeat 0 1 2 0 2 3 4 5 6 4 6 7...
			 * sequence and draw source arrays. But our array may have different offset
			 * When DrawRangeElementsBaseVertex unavailiable, we need build 4 different index arrays (as sequence have period 4)
			 * or just put 0-4 offset when it's availiable
			 * */
			pglBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, gl2wrap.triquads_ibo[0] );
			pglDrawRangeElementsBaseVertex( GL_TRIANGLES, startindex, startindex + count,
				Q_min( count / 4 * 6, TRIQUADS_SIZE * 6 - startindex ), GL_UNSIGNED_SHORT,
				(void *)(size_t)( startindex / 4 * 6 * 2 ), startindex % 4 );
			pglBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, 0 );
		}
		else if( rpglDrawRangeElements )
		{
			pglBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, gl2wrap.triquads_ibo[startindex % 4] );
			rpglDrawRangeElements( GL_TRIANGLES, startindex, startindex + count,
				Q_min( count / 4 * 6, TRIQUADS_SIZE * 6 - startindex ), GL_UNSIGNED_SHORT,
				(void *)(size_t)( startindex / 4 * 6 * 2 ));
			pglBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, 0 );
		}
		else
		{
			pglBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, gl2wrap.triquads_ibo[startindex % 4] );
			rpglDrawElements( GL_TRIANGLES,
				Q_min( count / 4 * 6, TRIQUADS_SIZE * 6 - startindex ), GL_UNSIGNED_SHORT,
				(void *)(size_t)( startindex / 4 * 6 * 2 ));
			pglBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, 0 );
		}
	}
	else if( gl2wrap.prim == GL_POLYGON ) // does it have any difference with TRIFAN?
		rpglDrawArrays( GL_TRIANGLE_FAN, startindex, count );
	else // TRIANGLES, LINES, TRISTRIP, TRIFAN supported anyway
		rpglDrawArrays( gl2wrap.prim, startindex, count );

leave_label:
	if( gl2wrap_config.vao_mandatory )
	{
		pglBindVertexArray( 0 );
		pglBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );
	}

	gl2wrap.prim = GL_NONE;
	gl2wrap.begin = gl2wrap.end;
	gl2wrap.cur_flags = 0;
#ifdef QUAD_BATCH
	gl2wrap_quad.active = 0;
#endif
}


static void APIENTRY GL2_End( void )
{
	int i;
#ifdef QUAD_BATCH
	if( gl2wrap.prim == GL_QUADS )
	{
		GLuint flags = gl2wrap.cur_flags;
		// enable alpha test and fog if needed
		/*if( alpha_test_state )
			SetBits( flags, BIT( GL2_FLAG_ALPHA_TEST ));
		if( fogging )
			SetBits( flags, BIT( GL2_FLAG_FOG ));*/
		gl2wrap_quad.flags = flags;
		gl2wrap_quad.active = 1;
		return;
	}
#endif
	GL2_FlushPrims();
}

static void (APIENTRY *rpglTexImage2D)( GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels );
static void APIENTRY GL2_TexImage2D( GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels )
{
	void *data = (void *)pixels;
	if( pixels && format == GL_RGBA && (
		internalformat == GL_RGB ||
		internalformat == GL_RGB8 ||
		internalformat == GL_RGB5 ||
		internalformat == GL_LUMINANCE ||
		internalformat == GL_LUMINANCE8 ||
		internalformat == GL_LUMINANCE4 )) // strip alpha from texture
	{
		unsigned char *in = data, *out;
		int i = 0, size = width * height * 4;

		data = out = (unsigned char *)malloc( size );

		for( i = 0; i < size; i += 4, in += 4, out += 4 )
		{
			memcpy( out, in, 3 );
			out[3] = 255;
		}
		internalformat = format;
	}
	if( internalformat == GL_LUMINANCE8_ALPHA8 || internalformat == GL_RGB || internalformat == GL_RGB8 || internalformat == GL_RGB5 )
		internalformat = GL_RGBA;
	rpglTexImage2D( target, level, internalformat, width, height, border, format, type, data );
	if( data != pixels )
		free( data );
}

static void (APIENTRY *rpglTexParameteri)( GLenum target, GLenum pname, GLint param );
static void APIENTRY GL2_TexParameteri( GLenum target, GLenum pname, GLint param )
{
	if( pname == GL_TEXTURE_BORDER_COLOR )
	{
		return; // not supported by opengl es
	}
	if(( pname == GL_TEXTURE_WRAP_S ||
		pname == GL_TEXTURE_WRAP_T ) &&
		param == GL_CLAMP )
	{
		param = GL_CLAMP_TO_EDGE;
	}

	rpglTexParameteri( target, pname, param );
}


GLboolean (APIENTRY *rpglIsEnabled)( GLenum e );
static GLboolean APIENTRY GL2_IsEnabled( GLenum e )
{
	if( e == GL_FOG )
		return gl2wrap_state.fog;
	return rpglIsEnabled( e );
}

static void APIENTRY GL2_Vertex3f( GLfloat x, GLfloat y, GLfloat z )
{
	GLfloat *p = gl2wrap.attrbuf[GL2_ATTR_POS] + gl2wrap.end * 3;
	*p++ = x;
	*p++ = y;
	*p++ = z;

	if( FBitSet( gl2wrap.cur_flags, BIT( GL2_ATTR_COLOR )))
	{
		GLfloat *p = gl2wrap.attrbuf[GL2_ATTR_COLOR] + gl2wrap.end * 4;
		SetBits( gl2wrap.cur_flags, BIT( GL2_ATTR_COLOR ));
		*p++ = gl2wrap.color[0];
		*p++ = gl2wrap.color[1];
		*p++ = gl2wrap.color[2];
		*p++ = gl2wrap.color[3];
	}
	++gl2wrap.end;

	if( gl2wrap.prim == GL_QUADS )
	{
		if( !( ( gl2wrap.end - gl2wrap.begin ) % 4 ) && gl2wrap.end > ( GL2_MAX_VERTS - 4 ) )
		{
			GL2_FlushPrims();
			GL2_Begin( GL_QUADS );
		}
	}
	else if( gl2wrap.end - gl2wrap.begin >= MAX_BEGINEND_VERTS )
	{
		GLenum prim = gl2wrap.prim;
		gEngfuncs.Con_DPrintf( S_ERROR "GL2_Vertex3f: Vertex buffer overflow!\n" );
		GL2_FlushPrims();
		GL2_Begin( prim );
	}
}

static void APIENTRY GL2_Vertex2f( GLfloat x, GLfloat y )
{
	GL2_Vertex3f( x, y, 0.f );
}

static void APIENTRY GL2_Vertex3fv( const GLfloat *v )
{
	GL2_Vertex3f( v[0], v[1], v[2] );
}

static void APIENTRY GL2_Color4f( GLfloat r, GLfloat g, GLfloat b, GLfloat a )
{
#ifdef QUAD_BATCH
	if( gl2wrap_quad.active )
	{
		if( !( gl2wrap.color[0] == r && gl2wrap.color[1] == g && gl2wrap.color[2] == b && gl2wrap.color[3] == a ))
			GL2_FlushPrims();
	}
#endif
	gl2wrap.color[0] = r;
	gl2wrap.color[1] = g;
	gl2wrap.color[2] = b;
	gl2wrap.color[3] = a;
	gl2wrap.uchanged = GL_TRUE;
#ifdef QUAD_BATCH
	if( gl2wrap_quad.active )
		return;
#endif
	if( gl2wrap.prim )
	{
		// HACK: enable color attribute if we're using color inside a Begin-End pair
		SetBits( gl2wrap.cur_flags, BIT( GL2_ATTR_COLOR ));
	}
}

static void APIENTRY GL2_Color3f( GLfloat r, GLfloat g, GLfloat b )
{
	GL2_Color4f( r, g, b, 1.f );
}

static void APIENTRY GL2_Color4ub( GLubyte r, GLubyte g, GLubyte b, GLubyte a )
{
	GL2_Color4f((GLfloat)r / 255.f, (GLfloat)g / 255.f, (GLfloat)b / 255.f, (GLfloat)a / 255.f );
}

static void APIENTRY GL2_Color4ubv( const GLubyte *v )
{
	GL2_Color4ub( v[0], v[1], v[2], v[3] );
}

static void APIENTRY GL2_TexCoord2f( GLfloat u, GLfloat v )
{
	// by spec glTexCoord always updates texunit 0
	GLfloat *p = gl2wrap.attrbuf[GL2_ATTR_TEXCOORD0] + gl2wrap.end * 2;
	SetBits( gl2wrap.cur_flags, BIT( GL2_ATTR_TEXCOORD0 ));
	*p++ = u;
	*p++ = v;
}

static void APIENTRY GL2_MultiTexCoord2f( GLenum tex, GLfloat u, GLfloat v )
{
	GLfloat *p;

	// assume there can only be two
	if( tex == GL_TEXTURE0_ARB )
	{
		p = gl2wrap.attrbuf[GL2_ATTR_TEXCOORD0] + gl2wrap.end * 2;
		SetBits( gl2wrap.cur_flags, BIT( GL2_ATTR_TEXCOORD0 ));
	}
	else
	{
		p = gl2wrap.attrbuf[GL2_ATTR_TEXCOORD1] + gl2wrap.end * 2;
		SetBits( gl2wrap.cur_flags, BIT( GL2_ATTR_TEXCOORD1 ));
	}
	*p++ = u;
	*p++ = v;
}


static void APIENTRY GL2_AlphaFunc( GLenum mode, GLfloat ref )
{
	gl2wrap.alpharef = ref;
	gl2wrap.uchanged = GL_TRUE;
	// mode is always GL_GREATER
}

static void APIENTRY GL2_Fogf( GLenum param, GLfloat val )
{
	if( param == GL_FOG_DENSITY )
	{
		gl2wrap.fog[3] = val;
		gl2wrap.uchanged = GL_TRUE;
	}
}

static void APIENTRY GL2_Fogfv( GLenum param, const GLfloat *val )
{
	if( param == GL_FOG_COLOR )
	{
		gl2wrap.fog[0] = val[0];
		gl2wrap.fog[1] = val[1];
		gl2wrap.fog[2] = val[2];
		gl2wrap.uchanged = GL_TRUE;
	}
}

static qboolean GL2_SkipEnable( GLenum e )
{
	return e == GL_TEXTURE_2D || e == GL_TEXTURE_1D;
}

static qboolean GL2_CatchEnable( GLenum e, qboolean enable )
{
	if( e == GL_FOG )
		gl2wrap_state.fog = enable;
	else if( e == GL_ALPHA_TEST )
		gl2wrap_state.alpha_test = enable;
	else
		return false;
	return true;
}

static void APIENTRY GL2_Enable( GLenum e )
{
	if( !GL2_SkipEnable( e ) && !GL2_CatchEnable( e, true ))
		rpglEnable( e );
}


static void APIENTRY GL2_Disable( GLenum e )
{
	if( !GL2_SkipEnable( e ) && !GL2_CatchEnable( e, false ))
		rpglDisable( e );
}

/*
===========================

Limited matrix emulation

===========================
*/

static void APIENTRY GL2_MatrixMode( GLenum m )
{
//	if( gl2wrap_matrix.mode == m )
//		return;
#ifdef QUAD_BATCH
	GL2_FlushPrims();
#endif
	gl2wrap_matrix.mode = m;
	switch( m )
	{
	case GL_MODELVIEW:
		gl2wrap_matrix.current = gl2wrap_matrix.mv;
		break;
	case GL_PROJECTION:
		gl2wrap_matrix.current = gl2wrap_matrix.pr;
		break;
	default:
		gl2wrap_matrix.current = gl2wrap_matrix.dummy;
		break;
	}
}

static void APIENTRY GL2_LoadIdentity( void )
{
	float *m = (float *)gl2wrap_matrix.current;
	m[1]  = m[2]  = m[3]  = m[4]  = 0.0f;
	m[6]  = m[7]  = m[8]  = m[9]  = 0.0f;
	m[11] = m[12] = m[13] = m[14] = 0.0f;
	m[0]  = m[5]  = m[10] = m[15] = 1.0f;
	gl2wrap_matrix.update = 0xFFFFFFFFFFFFFFFF;
}

static void APIENTRY GL2_Ortho( double l, double r, double b, double t, double n, double f )
{
	GLfloat m0  = 2 / ( r - l );
	GLfloat m5  = 2 / ( t - b );
	GLfloat m10 = - 2 / ( f - n );
	GLfloat m12 = - ( r + l ) / ( r - l );
	GLfloat m13 = - ( t + b ) / ( t - b );
	GLfloat m14 = - ( f + n ) / ( f - n );
	float *m = gl2wrap_matrix.current;

	m[12] += m12 * m[0] + m13 * m[4] + m14 * m[8];
	m[13] += m12 * m[1] + m13 * m[5] + m14 * m[9];
	m[14] += m12 * m[2] + m13 * m[6] + m14 * m[10];
	m[15] += m12 * m[3] + m13 * m[7] + m14 * m[11];
	m[0]  *= m0;
	m[1]  *= m0;
	m[2]  *= m0;
	m[3]  *= m0;
	m[4]  *= m5;
	m[5]  *= m5;
	m[6]  *= m5;
	m[7]  *= m5;
	m[8]  *= m10;
	m[9]  *= m10;
	m[10] *= m10;
	m[11] *= m10;
	gl2wrap_matrix.update = 0xFFFFFFFFFFFFFFFF;
}

static void GL2_Mul4x4( const GLfloat *in0, const GLfloat *in1, GLfloat *out )
{
	out[0]  = in0[0] * in1[0] + in0[1] * in1[4] + in0[2] * in1[8] + in0[3] * in1[12];
	out[1]  = in0[0] * in1[1] + in0[1] * in1[5] + in0[2] * in1[9] + in0[3] * in1[13];
	out[2]  = in0[0] * in1[2] + in0[1] * in1[6] + in0[2] * in1[10] + in0[3] * in1[14];
	out[3]  = in0[0] * in1[3] + in0[1] * in1[7] + in0[2] * in1[11] + in0[3] * in1[15];
	out[4]  = in0[4] * in1[0] + in0[5] * in1[4] + in0[6] * in1[8] + in0[7] * in1[12];
	out[5]  = in0[4] * in1[1] + in0[5] * in1[5] + in0[6] * in1[9] + in0[7] * in1[13];
	out[6]  = in0[4] * in1[2] + in0[5] * in1[6] + in0[6] * in1[10] + in0[7] * in1[14];
	out[7]  = in0[4] * in1[3] + in0[5] * in1[7] + in0[6] * in1[11] + in0[7] * in1[15];
	out[8]  = in0[8] * in1[0] + in0[9] * in1[4] + in0[10] * in1[8] + in0[11] * in1[12];
	out[9]  = in0[8] * in1[1] + in0[9] * in1[5] + in0[10] * in1[9] + in0[11] * in1[13];
	out[10] = in0[8] * in1[2] + in0[9] * in1[6] + in0[10] * in1[10] + in0[11] * in1[14];
	out[11] = in0[8] * in1[3] + in0[9] * in1[7] + in0[10] * in1[11] + in0[11] * in1[15];
	out[12] = in0[12] * in1[0] + in0[13] * in1[4] + in0[14] * in1[8] + in0[15] * in1[12];
	out[13] = in0[12] * in1[1] + in0[13] * in1[5] + in0[14] * in1[9] + in0[15] * in1[13];
	out[14] = in0[12] * in1[2] + in0[13] * in1[6] + in0[14] * in1[10] + in0[15] * in1[14];
	out[15] = in0[12] * in1[3] + in0[13] * in1[7] + in0[14] * in1[11] + in0[15] * in1[15];
}

static void GL2_UpdateMVP( gl2wrap_prog_t *prog )
{
	// use bitset to determine if need update matrix for this prog
	if( FBitSet( gl2wrap_matrix.update, BIT64( prog->flags )))
	{
		ClearBits( gl2wrap_matrix.update, BIT64( prog->flags ));
		GL2_Mul4x4( gl2wrap_matrix.mv, gl2wrap_matrix.pr, gl2wrap_matrix.mvp );
		pglUniformMatrix4fvARB( prog->uMVP, 1, false, (void *)gl2wrap_matrix.mvp );
	}
}

static void APIENTRY GL2_LoadMatrixf( const GLfloat *m )
{
	memcpy( gl2wrap_matrix.current, m, 16 * sizeof( float ));
	gl2wrap_matrix.update = 0xFFFFFFFFFFFFFFFF;
}

#if XASH_GLES
static void ( APIENTRY *_pglDepthRangef)( GLfloat zFar, GLfloat zNear );
static void APIENTRY GL2_DepthRange( GLdouble zFar, GLdouble zNear )
{
	_pglDepthRangef( zFar, zNear );
}
#endif

/*
=====================

Array drawing

=====================
*/
typedef struct gl2wrap_arraypointer_s
{
	const void *userptr;
	GLint size;
	GLenum type;
	GLsizei stride;
	GLuint vbo, *vbo_fb, vbo_cycle;
} gl2wrap_arraypointer_t;

static struct
{
	gl2wrap_arraypointer_t ptr[GL2_ATTR_MAX];
	unsigned int flags;
	//unsigned int vbo_flags;
	GLuint stream_buffer;
	void *stream_pointer;
	size_t stream_counter;
	GLuint vao_dynamic;
} gl2wrap_arrays;

static void GL2_SetPointer( int idx, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer )
{
	gl2wrap_arrays.ptr[idx].size = size;
	gl2wrap_arrays.ptr[idx].type = type;
	gl2wrap_arrays.ptr[idx].stride = stride;
	gl2wrap_arrays.ptr[idx].userptr = pointer;
	gl2wrap_arrays.ptr[idx].vbo = gl2wrap_state.vbo;
//	if( vbo )
//		SetBits( gl2wrap_arrays.vbo_flags, BIT( idx );
//	else
//		ClearBits( gl2wrap_arrays.vbo_flags, BIT( idx );
}

static void APIENTRY GL2_VertexPointer( GLint size, GLenum type, GLsizei stride, const GLvoid *pointer )
{
	GL2_SetPointer( GL2_ATTR_POS, size, type, stride, pointer );
}

static void APIENTRY GL2_ColorPointer( GLint size, GLenum type, GLsizei stride, const GLvoid *pointer )
{
	GL2_SetPointer( GL2_ATTR_COLOR, size, type, stride, pointer );
}

static void APIENTRY GL2_TexCoordPointer( GLint size, GLenum type, GLsizei stride, const GLvoid *pointer )
{
	GL2_SetPointer( GL2_ATTR_TEXCOORD0 + gl2wrap_state.tmu, size, type, stride, pointer );
}

static unsigned int GL2_GetArrIdx( GLenum array )
{
	switch( array )
	{
	case GL_VERTEX_ARRAY:
		return GL2_ATTR_POS;
	case GL_COLOR_ARRAY:
		return GL2_ATTR_COLOR;
	case GL_TEXTURE_COORD_ARRAY:
		Assert( gl2wrap_state.tmu < 2 );
		return GL2_ATTR_TEXCOORD0 + gl2wrap_state.tmu;
	}
	return 0;
}

static void APIENTRY GL2_EnableClientState( GLenum array )
{
	unsigned int idx = GL2_GetArrIdx( array );
	SetBits( gl2wrap_arrays.flags, BIT( idx ));
}

static void APIENTRY GL2_DisableClientState( GLenum array )
{
	unsigned int idx = GL2_GetArrIdx( array );
	ClearBits( gl2wrap_arrays.flags, BIT( idx ));
}


/*
===========================
UploadBufferData

Dumb buffer upload
Used when uploading very large buffers or when persistent/incremental buffers disabled (MapBuffer unavailiable?)
===========================
 */
static void GL2_UploadBufferData( gl2wrap_prog_t *prog, int size, GLuint start, GLuint end, int stride, int attr )
{
	if( !gl2wrap_arrays.ptr[attr].vbo_fb )
	{
		gl2wrap_arrays.ptr[attr].vbo_fb = malloc( 4 * gl2wrap_config.cycle_buffers );
		pglGenBuffersARB( gl2wrap_config.cycle_buffers, gl2wrap_arrays.ptr[attr].vbo_fb );
	}
	rpglBindBufferARB( GL_ARRAY_BUFFER_ARB, gl2wrap_arrays.ptr[attr].vbo_fb[gl2wrap_arrays.ptr[attr].vbo_cycle] );
	gl2wrap_arrays.ptr[attr].vbo_cycle = ( gl2wrap_arrays.ptr[attr].vbo_cycle + 1 ) % gl2wrap_config.cycle_buffers;
	pglBufferDataARB( GL_ARRAY_BUFFER_ARB, end * stride, gl2wrap_arrays.ptr[attr].userptr, GL_STREAM_DRAW_ARB );
	pglVertexAttribPointerARB( prog->attridx[attr], gl2wrap_arrays.ptr[attr].size, gl2wrap_arrays.ptr[attr].type, attr == GL2_ATTR_COLOR, gl2wrap_arrays.ptr[attr].stride, 0 );
}
/*
===========================
UpdatePersistentArrayBuffer

Persistent array always mapped to stream_pointer with BufferStorage
just memcopy it into and flush when overflowed
===========================
 */
static void GL2_UpdatePersistentArrayBuffer( gl2wrap_prog_t *prog, int size, int offset, GLuint start, GLuint end, int stride, int attr )
{
	if( gl2wrap_arrays.stream_counter + size > GL2_MAX_VERTS * 64 )
	{
		GLuint flags = GL_MAP_WRITE_BIT | MB( !gl2wrap_config.coherent, FLUSH_EXPLICIT ) |
			GL_MAP_PERSISTENT_BIT | MB( gl2wrap_config.coherent, COHERENT );
		pglUnmapBufferARB( GL_ARRAY_BUFFER_ARB );
		gl2wrap_arrays.stream_counter = 0;
		gl2wrap_arrays.stream_pointer = pglMapBufferRange( GL_ARRAY_BUFFER_ARB, 0, GL2_MAX_VERTS * 64, flags );
		//i = -1;
		//continue;
		size = end * stride, offset = 0;
	}

	memcpy(((char *)gl2wrap_arrays.stream_pointer ) + gl2wrap_arrays.stream_counter, ((char *)gl2wrap_arrays.ptr[attr].userptr ) + offset, size );
	if( !gl2wrap_config.coherent )
		pglFlushMappedBufferRange( GL_ARRAY_BUFFER_ARB, gl2wrap_arrays.stream_counter, size );
	pglVertexAttribPointerARB( prog->attridx[attr], gl2wrap_arrays.ptr[attr].size, gl2wrap_arrays.ptr[attr].type, attr == GL2_ATTR_COLOR, gl2wrap_arrays.ptr[attr].stride, (void *)( gl2wrap_arrays.stream_counter - offset ));
	gl2wrap_arrays.stream_counter += size;
}

/*
===========================
UpdateIncrementalArrayBuffer

Like persistent buffer, but map every time when copying data when BufferStorage unavailiable
===========================
 */
static void GL2_UpdateIncrementalArrayBuffer( gl2wrap_prog_t *prog, int size, int offset, GLuint start, GLuint end, int stride, int attr )
{
	void *mem;
	qboolean inv = false;
	GLuint flags = GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | MB( inv,INVALIDATE_BUFFER ) |
		MB( gl2wrap_config.async, UNSYNCHRONIZED ) | MB( gl2wrap_config.force_flush, FLUSH_EXPLICIT );

	if( gl2wrap_arrays.stream_counter + size > GL2_MAX_VERTS * 64 )
	{
		size = end * stride;
		offset = 0;
		gl2wrap_arrays.stream_counter = 0;
		inv = true;
	}
	mem = pglMapBufferRange( GL_ARRAY_BUFFER_ARB, gl2wrap_arrays.stream_counter, size, flags );
	memcpy( mem, ((char *)gl2wrap_arrays.ptr[attr].userptr ) + offset, size );
	if( gl2wrap_config.force_flush )
		pglFlushMappedBufferRange( GL_ARRAY_BUFFER_ARB, 0, size );
	pglUnmapBufferARB( GL_ARRAY_BUFFER_ARB );
	pglVertexAttribPointerARB( prog->attridx[attr], gl2wrap_arrays.ptr[attr].size, gl2wrap_arrays.ptr[attr].type, attr == GL2_ATTR_COLOR, gl2wrap_arrays.ptr[attr].stride, (void *)( gl2wrap_arrays.stream_counter - offset ));
	gl2wrap_arrays.stream_counter += size;
}

/*
===========================
AllocArrayPersistenStorage

Prepare BufferStorage
===========================
 */
static void GL2_AllocArrayPersistenStorage( void )
{
	GLuint flags = GL_MAP_WRITE_BIT | MB( !gl2wrap_config.coherent, FLUSH_EXPLICIT ) |
		GL_MAP_PERSISTENT_BIT | MB( gl2wrap_config.coherent, COHERENT );
	pglGenBuffersARB( 1, &gl2wrap_arrays.stream_buffer );
	rpglBindBufferARB( GL_ARRAY_BUFFER_ARB, gl2wrap_arrays.stream_buffer );
	pglBufferStorage( GL_ARRAY_BUFFER_ARB, GL2_MAX_VERTS * 64, NULL, GL_MAP_WRITE_BIT | MB( gl2wrap_config.coherent, COHERENT ) | GL_MAP_PERSISTENT_BIT );
	gl2wrap_arrays.stream_pointer = pglMapBufferRange( GL_ARRAY_BUFFER_ARB, 0, GL2_MAX_VERTS * 64, flags );
}

static void GL2_AllocArrays( void )
{
	if( gl2wrap_config.buf_storage && !gl2wrap_arrays.stream_pointer )
		GL2_AllocArrayPersistenStorage();
	else if( !gl2wrap_config.buf_storage && gl2wrap_config.incremental && !gl2wrap_arrays.stream_buffer )
	{
		// prepare incremental buffer
		pglGenBuffersARB( 1, &gl2wrap_arrays.stream_buffer );
		rpglBindBufferARB( GL_ARRAY_BUFFER_ARB, gl2wrap_arrays.stream_buffer );
		pglBufferDataARB( GL_ARRAY_BUFFER_ARB, GL2_MAX_VERTS * 64, NULL, GL_STREAM_DRAW_ARB );
	}
}

static void GL2_FreeArrays( void )
{
	if( gl2wrap_arrays.vao_dynamic )
		pglDeleteVertexArrays( 1, &gl2wrap_arrays.vao_dynamic );
	if( gl2wrap_arrays.stream_pointer )
	{
		pglBindBufferARB( GL_ARRAY_BUFFER_ARB, gl2wrap_arrays.stream_buffer );
		pglUnmapBufferARB( GL_ARRAY_BUFFER_ARB );
	}
	pglDeleteBuffersARB( 1, &gl2wrap_arrays.stream_buffer );
	memset( &gl2wrap_arrays, 0, sizeof( gl2wrap_arrays ));
}


/*
======================
SetupArrays

If vao usage mandatory, use persistent/incremental buffers when possible
else just set client pointers to default VAO
Usage of client pointers is forbidden with non-default VAO and unavailiable in Core
======================
*/
static void GL2_SetupArrays( GLuint start, GLuint end )
{
	gl2wrap_prog_t *prog;
	unsigned int flags = gl2wrap_arrays.flags;
	int i;

	if( !flags )
		return; // Legacy pointers not used

#ifdef QUAD_BATCH
	GL2_FlushPrims();
#endif

	if( gl2wrap_state.alpha_test )
		SetBits( flags, BIT( GL2_FLAG_ALPHA_TEST ));
	if( gl2wrap_state.fog )
		SetBits( flags, BIT( GL2_FLAG_FOG ));
	prog = GL2_SetProg( flags );// | GL2_ATTR_TEXCOORD0 );
	if( !prog )
		return;

	if( gl2wrap_config.vao_mandatory )
	{
		if( !gl2wrap_arrays.vao_dynamic )
			pglGenVertexArrays( 1, &gl2wrap_arrays.vao_dynamic );
		pglBindVertexArray( gl2wrap_arrays.vao_dynamic );
	}

	for( i = 0; i < GL2_ATTR_MAX; i++ )
	{
		if( prog->attridx[i] < 0 )
			continue;
		if( FBitSet( flags, BIT( i ))) // attribute is enabled
		{
			pglEnableVertexAttribArrayARB( prog->attridx[i] );
			// sometimes usage of client pointers may be faster, sometimes not
			// anyway gl core disallows that, so try use streaming
			if( gl2wrap_config.vao_mandatory && !gl2wrap_arrays.ptr[i].vbo )
			{
				// detect stride by type
				int stride = gl2wrap_arrays.ptr[i].stride, size, offset;

				if( stride == 0 )
				{
					if( gl2wrap_arrays.ptr[i].type == GL_UNSIGNED_BYTE )
						stride = gl2wrap_arrays.ptr[i].size;
					else
						stride = gl2wrap_arrays.ptr[i].size * 4;
				}

				rpglBindBufferARB( GL_ARRAY_BUFFER_ARB, gl2wrap_arrays.stream_buffer );
				if( !end )
				{
					// we cannot handle this case in VAO without known buffer length
					// only workaround is scanning index array to determine buffer limits, but it is slow,
					// so just do not use DrawElements when DrawRangeElements availiable
					pglDisableVertexAttribArrayARB( prog->attridx[i] );
					gEngfuncs.Con_Printf( S_ERROR "NON-vbo array for DrawElements call, SKIPPING!\n" );
					continue;
				}
				size = ( end - start ) * stride;
				offset = start * stride;

				// Logical buffer start can lie before real buffer start
				// but attrib pointer cannot have negative buffer offset
				if( gl2wrap_arrays.stream_counter < offset )
					size = end * stride, offset = 0;

				if(( !gl2wrap_config.buf_storage && !gl2wrap_config.incremental ) || size > GL2_MAX_VERTS * 32 )
				{
					GL2_UploadBufferData( prog, size, start, end, stride, i );
					continue;
				}
				if( !gl2wrap_config.buf_storage && gl2wrap_config.incremental )
				{
					GL2_UpdateIncrementalArrayBuffer( prog, size, offset, start, end, stride, i );
					continue;
				}
				GL2_UpdatePersistentArrayBuffer( prog, size, offset, start, end, stride, i );
			}
			else
			{
				rpglBindBufferARB( GL_ARRAY_BUFFER_ARB, gl2wrap_arrays.ptr[i].vbo );
				pglVertexAttribPointerARB( prog->attridx[i], gl2wrap_arrays.ptr[i].size, gl2wrap_arrays.ptr[i].type, i == GL2_ATTR_COLOR, gl2wrap_arrays.ptr[i].stride, gl2wrap_arrays.ptr[i].userptr );
			}
			/*
			if( i == GL2_ATTR_TEXCOORD0 )
				pglUniform1iARB( prog->utex0, 0 );
			if( i == GL2_ATTR_TEXCOORD1 )
				pglUniform1iARB( prog->utex1, 1 );
			*/
		}
		else
		{
			pglDisableVertexAttribArrayARB( prog->attridx[i] );
		}
	}
	// restore state
	rpglBindBufferARB( GL_ARRAY_BUFFER_ARB, gl2wrap_state.vbo );
}

static void APIENTRY GL2_DrawElements( GLenum mode, GLsizei count, GLenum type, const GLvoid *indices )
{
	GL2_SetupArrays( 0, 0 );
	rpglDrawElements( mode, count, type, indices );
}

static void APIENTRY GL2_DrawRangeElements( GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid *indices )
{
	GL2_SetupArrays( start, end );
	if( rpglDrawRangeElements )
		rpglDrawRangeElements( mode, start, end, count, type, indices );
	else
		rpglDrawElements( mode, count, type, indices );
}

static void APIENTRY GL2_DrawArrays( GLenum mode, GLint first, GLsizei count )
{
	GL2_SetupArrays( 0, count );
	rpglDrawArrays( mode, first, count );
}

static void APIENTRY GL2_BindBufferARB( GLenum buf, GLuint obj )
{
	if( buf == GL_ARRAY_BUFFER_ARB )
		gl2wrap_state.vbo = obj;
	rpglBindBufferARB( buf, obj );
}

static void APIENTRY GL2_ActiveTextureARB( GLenum tex )
{
	//gl2wrap_arrays.texture = GL_TEXTURE0_ARB - tex;
}

static void APIENTRY GL2_ClientActiveTextureARB( GLenum tex )
{
	gl2wrap_state.tmu = tex - GL_TEXTURE0_ARB;

	//pglActiveTextureARB( tex );
}

#define GL2_OVERRIDE_PTR( name ) \
{ \
	pgl ## name = GL2_ ## name; \
}

#define GL2_OVERRIDE_PTR_B( name ) \
{ \
	rpgl ## name = pgl ## name; \
	pgl ## name = GL2_ ## name; \
}

static void APIENTRY GL2_Normal3fv(const GLfloat *v)
{
}

static void APIENTRY GL2_Hint(GLenum target, GLenum mode)
{
}

static void APIENTRY GL2_Scalef(GLfloat x, GLfloat y, GLfloat z)
{
}

static void APIENTRY GL2_Translatef(GLfloat x, GLfloat y, GLfloat z)
{
}

static void APIENTRY GL2_TexEnvi(GLenum target, GLenum pname, GLint param)
{
}

static void APIENTRY GL2_TexEnvf(GLenum target, GLenum pname, GLfloat param)
{
}

static void APIENTRY GL2_Fogi(GLenum pname, GLint param)
{
}

static void APIENTRY GL2_ShadeModel(GLenum mode)
{
}

static void APIENTRY GL2_PolygonMode(GLenum face, GLenum mode)
{
}

static void APIENTRY GL2_PointSize(GLfloat size)
{
}

static void APIENTRY GL2_DrawBuffer(GLenum mode)
{
}

#if XASH_EMSCRIPTEN
static void GL2_PolygonOffset( GLfloat factor, GLfloat units )
{
}
#endif // XASH_EMSCRIPTEN

void GL2_ShimInstall( void )
{
	GL2_OVERRIDE_PTR( Vertex2f )
	GL2_OVERRIDE_PTR( Vertex3f )
	GL2_OVERRIDE_PTR( Vertex3fv )
	GL2_OVERRIDE_PTR( Color3f )
	GL2_OVERRIDE_PTR( Color4f )
	GL2_OVERRIDE_PTR( Color4ub )
	GL2_OVERRIDE_PTR( Color4ubv )
	GL2_OVERRIDE_PTR( Normal3fv )
	GL2_OVERRIDE_PTR( TexCoord2f )
	GL2_OVERRIDE_PTR( MultiTexCoord2f )
	GL2_OVERRIDE_PTR( AlphaFunc )
	GL2_OVERRIDE_PTR( Fogf )
	GL2_OVERRIDE_PTR( Fogfv )
	GL2_OVERRIDE_PTR( Hint ) // fog
	GL2_OVERRIDE_PTR( Begin )
	GL2_OVERRIDE_PTR( End )
	GL2_OVERRIDE_PTR_B( Enable )
	GL2_OVERRIDE_PTR_B( Disable )
	GL2_OVERRIDE_PTR( MatrixMode )
	GL2_OVERRIDE_PTR( LoadIdentity )
	GL2_OVERRIDE_PTR( Ortho )
	GL2_OVERRIDE_PTR( LoadMatrixf )
	GL2_OVERRIDE_PTR( Scalef )
	GL2_OVERRIDE_PTR( Translatef )
	GL2_OVERRIDE_PTR( TexEnvi )
	GL2_OVERRIDE_PTR( TexEnvf )
	GL2_OVERRIDE_PTR( ClientActiveTextureARB )
	//GL2_OVERRIDE_PTR( ActiveTextureARB )
	GL2_OVERRIDE_PTR( Fogi )
	GL2_OVERRIDE_PTR( ShadeModel )
#ifdef XASH_GLES
	_pglDepthRangef = gEngfuncs.GL_GetProcAddress( "glDepthRangef" );
	GL2_OVERRIDE_PTR( PolygonMode )
	GL2_OVERRIDE_PTR( PointSize )
	GL2_OVERRIDE_PTR( DepthRange )
	GL2_OVERRIDE_PTR( DrawBuffer )
#endif
	if( glConfig.context != CONTEXT_TYPE_GL )
	{
		GL2_OVERRIDE_PTR_B( TexImage2D )
		GL2_OVERRIDE_PTR_B( TexParameteri )
	}
#if XASH_EMSCRIPTEN
	GL2_OVERRIDE_PTR( PolygonOffset )
#endif // XASH_EMSCRIPTEN
	GL2_OVERRIDE_PTR_B( IsEnabled )
	GL2_OVERRIDE_PTR_B( DrawRangeElements )
	GL2_OVERRIDE_PTR_B( DrawElements )
	GL2_OVERRIDE_PTR_B( DrawArrays )
	GL2_OVERRIDE_PTR_B( BindBufferARB )
	GL2_OVERRIDE_PTR( EnableClientState )
	GL2_OVERRIDE_PTR( DisableClientState )
	GL2_OVERRIDE_PTR( VertexPointer )
	GL2_OVERRIDE_PTR( ColorPointer )
	GL2_OVERRIDE_PTR( TexCoordPointer )

#ifdef QUAD_BATCH
	GL2_OVERRIDE_PTR_B( BindTexture )
#endif
	GL2_AllocArrays();
}
#endif
