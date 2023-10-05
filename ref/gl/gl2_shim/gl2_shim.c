/*
gl2wrap_shim.c - vitaGL custom immediate mode shim
Copyright (C) 2023 fgsfds

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
	this is a "replacement" for vitaGL's immediate mode tailored specifically for xash
	this will only provide performance gains if vitaGL is built with DRAW_SPEEDHACK=1
	since that makes it assume that all vertex data pointers are GPU-mapped
*/

#include "gl_local.h"
#ifndef XASH_GL_STATIC
#include "gl2_shim.h"
#include <malloc.h>
//#include "xash3d_mathlib.h"
#define MAX_SHADERLEN 4096
// increase this when adding more attributes
#define MAX_PROGS 32

extern ref_api_t gEngfuncs;


enum gl2wrap_attrib_e
{
	GL2_ATTR_POS       = 0, // 1
	GL2_ATTR_COLOR     = 1, // 2
	GL2_ATTR_TEXCOORD0 = 2, // 4
	GL2_ATTR_TEXCOORD1 = 3, // 8
	GL2_ATTR_MAX
};

// continuation of previous enum
enum gl2wrap_flag_e
{
	GL2_FLAG_ALPHA_TEST = GL2_ATTR_MAX, // 16
	GL2_FLAG_FOG,                       // 32
	GL2_FLAG_NORMAL,
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
	GLuint attrbufobj[GL2_ATTR_MAX];
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
	GLuint vao;
} gl2wrap;

static struct
{
	float mvp[16], mv[16], pr[16], dummy[16];
	GLenum mode;
	float *current;
} gl2wrap_matrix;


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

// HACK: borrow alpha test and fog flags from internal vitaGL state
static GLboolean alpha_test_state;
static GLboolean fogging;

static void APIENTRY (*rpglEnable)(GLenum e);
static void APIENTRY (*rpglDisable)(GLenum e);
static void APIENTRY (*rpglDrawElements )( GLenum mode, GLsizei count, GLenum type, const GLvoid *indices );
static void APIENTRY (*rpglDrawArrays )(GLenum mode, GLint first, GLsizei count);
static void APIENTRY (*rpglDrawRangeElements )( GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid *indices );
static void APIENTRY (*rpglBindBufferARB)( GLenum buf, GLuint obj);
static char *GL_PrintInfoLog( GLhandleARB object )
{
	static char	msg[8192];
	int		maxLength = 0;

	pglGetObjectParameterivARB( object, GL_OBJECT_INFO_LOG_LENGTH_ARB, &maxLength );

	if( maxLength >= sizeof( msg ))
	{
		//ALERT( at_warning, "GL_PrintInfoLog: message exceeds %i symbols\n", sizeof( msg ));
		maxLength = sizeof( msg ) - 1;
	}

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
	int version = 130;

	shader = shader_buf;
	//shader[0] = '\n';
	shader[0] = 0;

	Q_snprintf(shader, MAX_SHADERLEN, "#version %d%s\n", version, version >= 300 && version < 330 ? " es":"");

	Q_snprintf(tmp, sizeof( tmp ), "#define VER %d\n", version);
	Q_strncat( shader, tmp, MAX_SHADERLEN );

	for ( i = 0; i < GL2_FLAG_MAX; ++i )
	{
		Q_snprintf( tmp, sizeof( tmp ), "#define %s %d\n", gl2wrap_flag_name[i], prog->flags & ( 1 << i ) );
		Q_strncat( shader, tmp, MAX_SHADERLEN );
	}
	loc = 0;
	for ( i = 0; i < GL2_ATTR_MAX; ++i )
	{
		if ( prog->flags & ( 1 << i ) )
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

	if ( type == GL_FRAGMENT_SHADER_ARB )
		Q_strncat( shader, gl2wrap_frag_src, MAX_SHADERLEN );
	else
		Q_strncat( shader, gl2wrap_vert_src, MAX_SHADERLEN );

	id = pglCreateShaderObjectARB( type );
	len = Q_strlen( shader );
	pglShaderSourceARB( id, 1, (void *)&shader, &len );
	pglCompileShaderARB( id );
	pglGetObjectParameterivARB( id, GL_OBJECT_COMPILE_STATUS_ARB, &status );

	if ( status == GL_FALSE )
	{
		gEngfuncs.Con_Reportf( S_ERROR "GL2_GenerateShader( 0x%04x, 0x%x ): compile failed: %s\n", prog->flags, type, GL_PrintInfoLog(id));

		gEngfuncs.Con_DPrintf( "Shader text:\n%s\n\n", shader );
		pglDeleteObjectARB( id );
		return 0;
	}

	return id;
}

static gl2wrap_prog_t *GL2_GetProg( const GLuint flags )
{
	int i, loc, status;
	GLuint vp, fp, glprog;
	gl2wrap_prog_t *prog;

	// try to find existing prog matching this feature set

	if ( gl2wrap.cur_prog && gl2wrap.cur_prog->flags == flags )
		return gl2wrap.cur_prog;

	for ( i = 0; i < MAX_PROGS; ++i )
	{
		if ( gl2wrap.progs[i].flags == flags )
			return &gl2wrap.progs[i];
		else if ( gl2wrap.progs[i].flags == 0 )
			break;
	}

	if ( i == MAX_PROGS )
	{
		gEngfuncs.Host_Error( "GL2_GetProg(): Ran out of program slots for 0x%04x\n", flags );
		return NULL;
	}

	// new prog; generate shaders

	gEngfuncs.Con_DPrintf( S_NOTE "GL2_GetProg(): Generating progs for 0x%04x\n", flags );
	prog = &gl2wrap.progs[i];
	prog->flags = flags;

	vp = GL2_GenerateShader( prog, GL_VERTEX_SHADER_ARB );
	fp = GL2_GenerateShader( prog, GL_FRAGMENT_SHADER_ARB );
	if ( !vp || !fp )
	{
		prog->flags = 0;
		return NULL;
	}

	glprog = pglCreateProgramObjectARB();
	pglAttachObjectARB( glprog, vp );
	pglAttachObjectARB( glprog, fp );

	loc = 0;
	for ( i = 0; i < GL2_ATTR_MAX; ++i )
	{
		if ( flags & ( 1 << i ) )
		{
			prog->attridx[i] = loc;
			pglBindAttribLocationARB( glprog, loc++, gl2wrap_attr_name[i] );
		}
		else
		{
			prog->attridx[i] = -1;
		}
	}

	pglLinkProgramARB( glprog );
	pglDeleteObjectARB( vp );
	pglDeleteObjectARB( fp );

#ifndef XASH_GLES
	pglGetObjectParameterivARB( glprog, GL_OBJECT_LINK_STATUS_ARB, &status );
	if ( status == GL_FALSE )
	{
		gEngfuncs.Con_Reportf( S_ERROR "GL2_GetProg(): Failed linking progs for 0x%04x!\n%s\n", prog->flags, GL_PrintInfoLog(glprog) );
		prog->flags = 0;
		pglDeleteObjectARB( glprog );
		return NULL;
	}
#endif

	prog->ucolor = pglGetUniformLocationARB( glprog, "uColor" );
	prog->ualpha = pglGetUniformLocationARB( glprog, "uAlphaTest" );
	prog->utex0  = pglGetUniformLocationARB( glprog, "uTex0" );
	prog->utex1  = pglGetUniformLocationARB( glprog, "uTex1" );
	prog->ufog   = pglGetUniformLocationARB( glprog, "uFog" );
	prog->uMVP   = pglGetUniformLocationARB( glprog, "uMVP" );

	// these never change
	if ( prog->flags & ( 1U << GL2_ATTR_TEXCOORD0 ) && prog->utex0 >= 0 )
		pglUniform1iARB( prog->utex0, 0 );
	if ( prog->flags & ( 1U << GL2_ATTR_TEXCOORD1 ) && prog->utex1 >= 0 )
		pglUniform1iARB( prog->utex1, 1 );

	prog->glprog = glprog;

	gEngfuncs.Con_DPrintf( S_NOTE "GL2_GetProg(): Generated progs for 0x%04x\n", flags );

	return prog;
}
static void GL2_UpdateMVP( gl2wrap_prog_t *prog);
static gl2wrap_prog_t *GL2_SetProg( const GLuint flags )
{
	gl2wrap_prog_t *prog = NULL;

	if ( flags && ( prog = GL2_GetProg( flags ) ) )
	{
		if ( prog != gl2wrap.cur_prog )
		{
			pglUseProgramObjectARB( prog->glprog );
			gl2wrap.uchanged = GL_TRUE;
		}
		if ( gl2wrap.uchanged )
		{
			if ( prog->ualpha >= 0 )
				pglUniform1fARB( prog->ualpha, gl2wrap.alpharef );
			if ( prog->ucolor >= 0 )
				pglUniform4fvARB( prog->ucolor, 1, gl2wrap.color );
			if ( prog->ufog >= 0 )
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

#define TRIQUADS_SIZE 256
unsigned short triquads_array[ TRIQUADS_SIZE * 6 ];

int GL2_ShimInit( void )
{
	int i;
	GLuint total, size;
	static const GLuint precache_progs[] = {
		0x0001, // out = ucolor
		0x0005, // out = tex0 * ucolor
		0x0007, // out = tex0 * vcolor
		0x0015, // out = tex0 * ucolor + FEAT_ALPHA_TEST
		0x0021, // out = ucolor + FEAT_FOG
		0x0025, // out = tex0 * ucolor + FEAT_FOG
		0x0027, // out = tex0 * vcolor + FEAT_FOG
		0x0035, // out = tex0 * ucolor + FEAT_ALPHA_TEST + FEAT_FOG
	};

	if ( gl2wrap_init )
		return 0;

	memset( &gl2wrap, 0, sizeof( gl2wrap ) );
	for( i = 0; i < TRIQUADS_SIZE; i++ )
	{
		triquads_array[i * 6] = i * 4;
		triquads_array[i * 6 + 1] = i * 4 + 1;
		triquads_array[i * 6 + 2] = i * 4 + 2;
		triquads_array[i * 6 + 3] = i * 4;
		triquads_array[i * 6 + 4] = i * 4 + 2;
		triquads_array[i * 6 + 5] = i * 4 + 3;
	}

	gl2wrap.color[0] = 1.f;
	gl2wrap.color[1] = 1.f;
	gl2wrap.color[2] = 1.f;
	gl2wrap.color[3] = 1.f;
	gl2wrap.uchanged = GL_TRUE;

	total = 0;
	if( glConfig.context == CONTEXT_TYPE_GL_CORE &&  pglGenVertexArrays )
		pglGenVertexArrays(1, &gl2wrap.vao);
	if(gl2wrap.vao)
		pglBindVertexArray(gl2wrap.vao);
	for ( i = 0; i < GL2_ATTR_MAX; ++i )
	{
		size = GL2_MAX_VERTS * gl2wrap_attr_size[i] * sizeof( GLfloat );
		// TODO: rework storage, support MapBuffer
		gl2wrap.attrbuf[i] = memalign( 0x100, size );
		if( gl2wrap.vao )
			pglGenBuffersARB( 1, &gl2wrap.attrbufobj[i] );
		total += size;
	}
	if(gl2wrap.vao)
		pglBindVertexArray(0);
	

	GL2_ShimInstall();

	gEngfuncs.Con_DPrintf( S_NOTE "GL2_ShimInit(): %u bytes allocated for vertex buffer\n", total );
	gEngfuncs.Con_DPrintf( S_NOTE "GL2_ShimInit(): Pre-generating %u progs...\n", (uint)(sizeof( precache_progs ) / sizeof( *precache_progs ) ));
	for ( i = 0; i < (int)( sizeof( precache_progs ) / sizeof( *precache_progs ) ); ++i )
		GL2_GetProg( precache_progs[i] );


	gl2wrap_init = 1;
	return 0;
}

void GL2_ShimShutdown( void )
{
	int i;

	if ( !gl2wrap_init )
		return;

	pglFinish();
	pglUseProgramObjectARB( 0 );

	/*
	// FIXME: this sometimes causes the game to block on glDeleteProgram for up to a minute
	//        but since this is only called on shutdown or game change, it should be fine to skip
	for ( i = 0; i < MAX_PROGS; ++i )
	{
		if ( gl2wrap.progs[i].flags )
			glDeleteProgram( gl2wrap.progs[i].glprog );
	}
	*/

	for ( i = 0; i < GL2_ATTR_MAX; ++i )
		free( gl2wrap.attrbuf[i] );

	memset( &gl2wrap, 0, sizeof( gl2wrap ) );

	gl2wrap_init = 0;
}

void GL2_ShimEndFrame( void )
{
	gl2wrap.end = gl2wrap.begin = 0;
}

static void APIENTRY GL2_Begin( GLenum prim )
{
	int i;
	gl2wrap.prim = prim;
	gl2wrap.begin = gl2wrap.end;
	// pos always enabled
	gl2wrap.cur_flags = 1 << GL2_ATTR_POS;
	// disable all vertex attrib pointers
	if(gl2wrap.vao)
		pglBindVertexArray(gl2wrap.vao);

	for ( i = 0; i < GL2_ATTR_MAX; ++i )
		pglDisableVertexAttribArrayARB( i );
}


static void APIENTRY GL2_End( void )
{
	int i;
	gl2wrap_prog_t *prog;
	GLuint flags = gl2wrap.cur_flags;
	GLint count = gl2wrap.end - gl2wrap.begin;

	if ( !gl2wrap.prim || !count )
		goto _leave; // end without begin 

	// enable alpha test and fog if needed
	if ( alpha_test_state )
		flags |= 1 << GL2_FLAG_ALPHA_TEST;
	if ( fogging )
		flags |= 1 << GL2_FLAG_FOG;

	prog = GL2_SetProg( flags );
	if ( !prog )
	{
		gEngfuncs.Host_Error( "GL2_End(): Could not find program for flags 0x%04x!\n", flags );
		goto _leave;
	}
	for ( i = 0; i < GL2_ATTR_MAX; ++i )
	{
		if ( prog->attridx[i] >= 0 )
		{
			pglEnableVertexAttribArrayARB( prog->attridx[i] );
			if(gl2wrap.attrbufobj[i])
			{
				pglBindBufferARB( GL_ARRAY_BUFFER_ARB, gl2wrap.attrbufobj[i] );
				pglBufferDataARB( GL_ARRAY_BUFFER_ARB, gl2wrap_attr_size[i] * 4 * count,  gl2wrap.attrbuf[i] + gl2wrap_attr_size[i] * gl2wrap.begin , GL_STATIC_DRAW_ARB );
				pglVertexAttribPointerARB( prog->attridx[i], gl2wrap_attr_size[i], GL_FLOAT, GL_FALSE, 0, 0 );

			}
			else
				pglVertexAttribPointerARB( prog->attridx[i], gl2wrap_attr_size[i], GL_FLOAT, GL_FALSE, 0, gl2wrap.attrbuf[i] + gl2wrap_attr_size[i] * gl2wrap.begin );
		}
	}

#if 1 //def XASH_GLES
	if(gl2wrap.prim == GL_QUADS)
	{
		if(rpglDrawRangeElements)
			rpglDrawRangeElements(GL_TRIANGLES, 0, count, Q_min(count / 4 * 6,sizeof(triquads_array)/2), GL_UNSIGNED_SHORT, triquads_array);
		else
			rpglDrawElements(GL_TRIANGLES, Q_min(count / 4 * 6,sizeof(triquads_array)/2), GL_UNSIGNED_SHORT, triquads_array);
	}
	else if( gl2wrap.prim == GL_POLYGON )
		rpglDrawArrays( GL_TRIANGLE_FAN, 0, count );
	else
#endif
		rpglDrawArrays( gl2wrap.prim, 0, count );

_leave:
	if(gl2wrap.vao)
		pglBindVertexArray(0);
	pglBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );
	
	gl2wrap.prim = GL_NONE;
	gl2wrap.begin = gl2wrap.end;
	gl2wrap.cur_flags = 0;
}

static void (* APIENTRY rpglTexImage2D)( GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels );
static void APIENTRY GL2_TexImage2D( GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels )
{
	void *data = (void*)pixels;
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

		data = out = (unsigned char*)malloc( size );

		for( i = 0; i < size; i += 4, in += 4, out += 4 )
		{
			memcpy( out, in, 3 );
			out[3] = 255;
		}
		internalformat = format;
	}
	if( internalformat == GL_LUMINANCE8_ALPHA8 || internalformat == GL_RGB )
		internalformat = GL_RGBA;
	rpglTexImage2D( target, level, internalformat, width, height, border, format, type, data );
	if( data != pixels )
		free(data);
}
static void (* APIENTRY rpglTexParameteri)( GLenum target, GLenum pname, GLint param );
static void APIENTRY GL2_TexParameteri( GLenum target, GLenum pname, GLint param )
{
	if ( pname == GL_TEXTURE_BORDER_COLOR )
	{
		return; // not supported by opengl es
	}
	if ( ( pname == GL_TEXTURE_WRAP_S ||
		   pname == GL_TEXTURE_WRAP_T ) &&
		 param == GL_CLAMP )
	{
		param = 0x812F;
	}

	rpglTexParameteri( target, pname, param );
}


GLboolean (* APIENTRY rpglIsEnabled)(GLenum e);
static GLboolean APIENTRY GL2_IsEnabled(GLenum e)
{
	if(e == GL_FOG)
		return fogging;
	return rpglIsEnabled(e);
}




static void APIENTRY GL2_Vertex3f( GLfloat x, GLfloat y, GLfloat z )
{
	GLfloat *p = gl2wrap.attrbuf[GL2_ATTR_POS] + gl2wrap.end * 3;
	*p++ = x;
	*p++ = y;
	*p++ = z;
	++gl2wrap.end;
	if ( gl2wrap.end >= GL2_MAX_VERTS )
	{
		gEngfuncs.Con_DPrintf( S_ERROR "GL2_Vertex3f(): Vertex buffer overflow!\n" );
		gl2wrap.end = gl2wrap.begin = 0;
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
	gl2wrap.color[0] = r;
	gl2wrap.color[1] = g;
	gl2wrap.color[2] = b;
	gl2wrap.color[3] = a;
	gl2wrap.uchanged = GL_TRUE;
	if ( gl2wrap.prim )
	{
		// HACK: enable color attribute if we're using color inside a Begin-End pair
		GLfloat *p = gl2wrap.attrbuf[GL2_ATTR_COLOR] + gl2wrap.end * 4;
		gl2wrap.cur_flags |= 1 << GL2_ATTR_COLOR;
		*p++ = r;
		*p++ = g;
		*p++ = b;
		*p++ = a;
	}
}

static void APIENTRY GL2_Color3f( GLfloat r, GLfloat g, GLfloat b )
{
	GL2_Color4f( r, g, b, 1.f );
}

static void APIENTRY GL2_Color4ub( GLubyte r, GLubyte g, GLubyte b, GLubyte a )
{
	GL2_Color4f( (GLfloat)r / 255.f, (GLfloat)g / 255.f, (GLfloat)b / 255.f, (GLfloat)a / 255.f );
}

static void APIENTRY GL2_Color4ubv( const GLubyte *v )
{
	GL2_Color4ub( v[0], v[1], v[2], v[3] );
}

static void APIENTRY GL2_TexCoord2f( GLfloat u, GLfloat v )
{
	// by spec glTexCoord always updates texunit 0
	GLfloat *p = gl2wrap.attrbuf[GL2_ATTR_TEXCOORD0] + gl2wrap.end * 2;
	gl2wrap.cur_flags |= 1 << GL2_ATTR_TEXCOORD0;
	*p++ = u;
	*p++ = v;
}

static void APIENTRY GL2_MultiTexCoord2f( GLenum tex, GLfloat u, GLfloat v )
{
	GLfloat *p;
	// assume there can only be two
	if ( tex == GL_TEXTURE0_ARB )
	{
		p = gl2wrap.attrbuf[GL2_ATTR_TEXCOORD0] + gl2wrap.end * 2;
		gl2wrap.cur_flags |= 1 << GL2_ATTR_TEXCOORD0;
	}
	else
	{
		p = gl2wrap.attrbuf[GL2_ATTR_TEXCOORD1] + gl2wrap.end * 2;
		gl2wrap.cur_flags |= 1 << GL2_ATTR_TEXCOORD1;
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
	if ( param == GL_FOG_DENSITY )
	{
		gl2wrap.fog[3] = val;
		gl2wrap.uchanged = GL_TRUE;
	}
}

static void APIENTRY GL2_Fogfv( GLenum param, const GLfloat *val )
{
	if ( param == GL_FOG_COLOR )
	{
		gl2wrap.fog[0] = val[0];
		gl2wrap.fog[1] = val[1];
		gl2wrap.fog[2] = val[2];
		gl2wrap.uchanged = GL_TRUE;
	}
}

static void APIENTRY GL2_Enable( GLenum e )
{
	if( e == GL_TEXTURE_2D )
		{}
	else if( e == GL_FOG )
		fogging = 1;
	else if( e == GL_ALPHA_TEST )
		alpha_test_state = 1;
	else
		rpglEnable(e);
}

static void APIENTRY GL2_Disable( GLenum e )
{
	if( e == GL_TEXTURE_2D )
		{}
	else if( e == GL_FOG )
		fogging = 0;
	else if( e == GL_ALPHA_TEST )
		alpha_test_state = 0;
	else
	{
		rpglDisable(e);
	}

}

static void APIENTRY GL2_MatrixMode( GLenum m )
{
//	if(gl2wrap_matrix.mode == m)
//		return;
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
	float *m = (float*)gl2wrap_matrix.current;
	m[1]  = m[2]  = m[3]  = m[4]  = 0.0f;
	m[6]  = m[7]  = m[8]  = m[9]  = 0.0f;
	m[11] = m[12] = m[13] = m[14] = 0.0f;
	m[0]  = m[5]  = m[10] = m[15] = 1.0f;

}


static void APIENTRY GL2_Ortho(double l, double r, double b, double t, double n, double f)
{
	GLfloat m0  = 2 / (r - l);
	GLfloat m5  = 2 / (t - b);
	GLfloat m10 = - 2 / (f - n);
	GLfloat m12 = - (r + l) / (r - l);
	GLfloat m13 = - (t + b) / (t - b);
	GLfloat m14 = - (f + n) / (f - n);
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
}

static void GL2_Mul4x4(const GLfloat *in0, const GLfloat *in1, GLfloat *out)
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
	GL2_Mul4x4(gl2wrap_matrix.mv, gl2wrap_matrix.pr, gl2wrap_matrix.mvp );
	pglUniformMatrix4fvARB(prog->uMVP, 1, false, (void*)gl2wrap_matrix.mvp);
}

static void APIENTRY GL2_LoadMatrixf( const GLfloat *m )
{
	memcpy( gl2wrap_matrix.current, m, 16*sizeof(float) );
}

static void ( APIENTRY*_pglDepthRangef)(GLfloat far, GLfloat near);

static void APIENTRY GL2_DepthRange(GLdouble far, GLdouble near)
{
	_pglDepthRangef(far, near);
}

typedef struct gl2wrap_arraypointer_s
{
	const void *userptr;
	GLint size;
	GLenum type;
	GLsizei stride;
	GLuint vbo;
	//GLboolean enabled;
} gl2wrap_arraypointer_t;

static struct
{
	gl2wrap_arraypointer_t ptr[GL2_ATTR_MAX];
	unsigned int flags;
	unsigned int texture;
	GLuint vbo;
} gl2wrap_arrays;


static void GL2_SetPointer( int idx, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer )
{
	gl2wrap_arrays.ptr[idx].size = size;
	gl2wrap_arrays.ptr[idx].type = type;
	gl2wrap_arrays.ptr[idx].stride = stride;
	gl2wrap_arrays.ptr[idx].userptr = pointer;
	gl2wrap_arrays.ptr[idx].vbo = gl2wrap_arrays.vbo;
}

void GL2_VertexPointer( GLint size, GLenum type, GLsizei stride, const GLvoid *pointer )
{
	GL2_SetPointer(GL2_ATTR_POS, size, type, stride, pointer);
}

void GL2_ColorPointer( GLint size, GLenum type, GLsizei stride, const GLvoid *pointer )
{
	GL2_SetPointer(GL2_ATTR_COLOR, size, type, stride, pointer);
}

void GL2_TexCoordPointer( GLint size, GLenum type, GLsizei stride, const GLvoid *pointer )
{
	GL2_SetPointer(GL2_ATTR_TEXCOORD0 + gl2wrap_arrays.texture, size, type, stride, pointer);
}

static unsigned int GL2_GetArrIdx( GLenum array )
{
	switch (array) {
	case GL_VERTEX_ARRAY:
		return GL2_ATTR_POS;
	case GL_COLOR_ARRAY:
		return GL2_ATTR_COLOR;
	case GL_TEXTURE_COORD_ARRAY:
			ASSERT(gl2wrap_arrays.texture < 2);
		return GL2_ATTR_TEXCOORD0 + gl2wrap_arrays.texture;
	default:
		return 0;
	}
}

void GL2_EnableClientState( GLenum array )
{
	gl2wrap_prog_t *prog;
	GLuint flags = gl2wrap.cur_flags;
	int idx = GL2_GetArrIdx(array);
	//gl2wrap_arrays.ptr[idx].enabled = 1;
	gl2wrap_arrays.flags |= 1 << idx;
	// enable alpha test and fog if needed


	prog = GL2_SetProg( flags );
	//pglEnableVertexAttribArrayARB( prog->attridx[idx] );
}

void GL2_DisableClientState( GLenum array )
{
	unsigned int idx = GL2_GetArrIdx(array);
	//gl2wrap_arrays.ptr[idx].enabled = 0;
	gl2wrap_arrays.flags &= ~(1 << idx);
	//pglDisableVertexAttribArrayARB( gl2wrap.cur_prog->attridx[idx] );
}

static void GL2_SetupArrays( void )
{
	unsigned int flags = gl2wrap_arrays.flags;
	gl2wrap_prog_t *prog;
	if ( alpha_test_state )
		flags |= 1 << GL2_FLAG_ALPHA_TEST;
	if ( fogging )
		flags |= 1 << GL2_FLAG_FOG;
	prog = GL2_SetProg( flags );// | GL2_ATTR_TEXCOORD0 );
	for( int i = 0; i < GL2_ATTR_MAX; i++ )
	{
		if(prog->attridx[i] < 0)
			continue;
		if( flags & (1 << i)  )
		{
			pglEnableVertexAttribArrayARB( prog->attridx[i] );
			rpglBindBufferARB( GL_ARRAY_BUFFER_ARB,  gl2wrap_arrays.ptr[i].vbo );
			pglVertexAttribPointerARB( prog->attridx[i], gl2wrap_arrays.ptr[i].size, gl2wrap_arrays.ptr[i].type, i == GL2_ATTR_COLOR, gl2wrap_arrays.ptr[i].stride, gl2wrap_arrays.ptr[i].userptr );
			if(i == GL2_ATTR_TEXCOORD0)
				pglUniform1iARB( prog->utex0, 0 );
			if(i == GL2_ATTR_TEXCOORD1)
				pglUniform1iARB( prog->utex1, 1 );
		}
		else
		{
			pglDisableVertexAttribArrayARB( prog->attridx[i] );
		}
	}
	rpglBindBufferARB( GL_ARRAY_BUFFER_ARB,  gl2wrap_arrays.vbo );
}

static void APIENTRY GL2_DrawElements( GLenum mode, GLsizei count, GLenum type, const GLvoid *indices )
{
	GL2_SetupArrays();
	rpglDrawElements(mode, count, type, indices);
}

static void APIENTRY GL2_DrawRangeElements( GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid *indices )
{
	GL2_SetupArrays();
	if(rpglDrawRangeElements)
		rpglDrawRangeElements(mode, start, end, count, type, indices);
	else
		rpglDrawElements(mode, count, type, indices);
}

static void APIENTRY GL2_DrawArrays( GLenum mode, GLint first, GLsizei count )
{
	GL2_SetupArrays();
	rpglDrawArrays( mode, first, count );
}

static void APIENTRY GL2_BindBufferARB( GLenum buf, GLuint obj)
{
	if( buf == GL_ARRAY_BUFFER_ARB )
		gl2wrap_arrays.vbo = obj;
	rpglBindBufferARB( buf, obj );
}

static void APIENTRY GL2_ActiveTextureARB( GLenum tex )
{
	//gl2wrap_arrays.texture = GL_TEXTURE0_ARB - tex;
}

static void APIENTRY GL2_ClientActiveTextureARB( GLenum tex )
{
	gl2wrap_arrays.texture = tex - GL_TEXTURE0_ARB;

	//pglActiveTextureARB(tex);
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


static void APIENTRY stub( void ){}

#define GL2_STUB( name ) \
{ \
	*((void**)&pgl ## name) = (void*)stub; \
}

void GL2_ShimInstall( void )
{
	GL2_OVERRIDE_PTR( Vertex2f )
	GL2_OVERRIDE_PTR( Vertex3f )
	GL2_OVERRIDE_PTR( Vertex3fv )
	GL2_OVERRIDE_PTR( Color3f )
	GL2_OVERRIDE_PTR( Color4f )
	GL2_OVERRIDE_PTR( Color4ub )
	GL2_OVERRIDE_PTR( Color4ubv )
	GL2_STUB( Normal3fv )
	GL2_OVERRIDE_PTR( TexCoord2f )
	GL2_OVERRIDE_PTR( MultiTexCoord2f )
	GL2_OVERRIDE_PTR( AlphaFunc )
	GL2_OVERRIDE_PTR( Fogf )
	GL2_OVERRIDE_PTR( Fogfv )
	GL2_STUB( Hint ) // fog
	GL2_OVERRIDE_PTR( Begin )
	GL2_OVERRIDE_PTR( End )
	GL2_OVERRIDE_PTR_B( Enable )
	GL2_OVERRIDE_PTR_B( Disable )
	GL2_OVERRIDE_PTR( MatrixMode )
	GL2_OVERRIDE_PTR( LoadIdentity )
	GL2_OVERRIDE_PTR( Ortho )
	GL2_OVERRIDE_PTR( LoadMatrixf )
	GL2_STUB( Scalef )
	GL2_STUB( Translatef )
	GL2_STUB( TexEnvi )
	GL2_STUB( TexEnvf )
	GL2_OVERRIDE_PTR( ClientActiveTextureARB )
	//GL2_OVERRIDE_PTR( ActiveTextureARB )
	GL2_STUB( Fogi )
	GL2_STUB( ShadeModel )
#ifdef XASH_GLES
	_pglDepthRangef = gEngfuncs.GL_GetProcAddress("glDepthRangef");
	GL2_STUB( PolygonMode )
	GL2_STUB( PointSize )
	GL2_OVERRIDE_PTR( DepthRange )
	GL2_STUB( DrawBuffer )
#endif
	if( glConfig.context != CONTEXT_TYPE_GL )
	{
		GL2_OVERRIDE_PTR_B( TexImage2D )
		GL2_OVERRIDE_PTR_B( TexParameteri )
	}
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
}
#endif
