
#include "gl_local.h"
#if XASH_GL4ES
#include "gl4es/include/gl4esinit.h"
#include "gl4es/include/gl4eshint.h"
#endif // XASH_GL4ES

CVAR_DEFINE( gl_extensions, "gl_allow_extensions", "1", FCVAR_GLCONFIG|FCVAR_READ_ONLY, "allow gl_extensions" );
CVAR_DEFINE( gl_texture_anisotropy, "gl_anisotropy", "8", FCVAR_GLCONFIG, "textures anisotropic filter" );
CVAR_DEFINE_AUTO( gl_texture_lodbias, "0.0", FCVAR_GLCONFIG, "LOD bias for mipmapped textures (perfomance|quality)" );
CVAR_DEFINE_AUTO( gl_texture_nearest, "0", FCVAR_GLCONFIG, "disable texture filter" );
CVAR_DEFINE_AUTO( gl_lightmap_nearest, "0", FCVAR_GLCONFIG, "disable lightmap filter" );
CVAR_DEFINE_AUTO( gl_keeptjunctions, "1", FCVAR_GLCONFIG, "removing tjuncs causes blinking pixels" );
CVAR_DEFINE_AUTO( gl_check_errors, "1", FCVAR_GLCONFIG, "ignore video engine errors" );
CVAR_DEFINE_AUTO( gl_polyoffset, "2.0", FCVAR_GLCONFIG, "polygon offset for decals" );
CVAR_DEFINE_AUTO( gl_wireframe, "0", FCVAR_GLCONFIG|FCVAR_SPONLY, "show wireframe overlay" );
CVAR_DEFINE_AUTO( gl_finish, "0", FCVAR_GLCONFIG, "use glFinish instead of glFlush" );
CVAR_DEFINE_AUTO( gl_nosort, "0", FCVAR_GLCONFIG, "disable sorting of translucent surfaces" );
CVAR_DEFINE_AUTO( gl_test, "0", 0, "engine developer cvar for quick testing new features" );
CVAR_DEFINE_AUTO( gl_msaa, "1", FCVAR_GLCONFIG, "enable or disable multisample anti-aliasing" );
CVAR_DEFINE_AUTO( gl_stencilbits, "8", FCVAR_GLCONFIG|FCVAR_READ_ONLY, "pixelformat stencil bits (0 - auto)" );
CVAR_DEFINE_AUTO( gl_overbright, "1", FCVAR_GLCONFIG, "overbrights" );
CVAR_DEFINE_AUTO( gl_fog, "1", FCVAR_GLCONFIG, "allow for rendering fog using built-in OpenGL fog implementation" );
CVAR_DEFINE_AUTO( r_lighting_extended, "1", FCVAR_GLCONFIG, "allow to get lighting from world and bmodels" );
CVAR_DEFINE_AUTO( r_lighting_ambient, "0.3", FCVAR_GLCONFIG, "map ambient lighting scale" );
CVAR_DEFINE_AUTO( r_detailtextures, "1", FCVAR_GLCONFIG, "enable detail textures support" );
CVAR_DEFINE_AUTO( r_novis, "0", 0, "ignore vis information (perfomance test)" );
CVAR_DEFINE_AUTO( r_nocull, "0", 0, "ignore frustrum culling (perfomance test)" );
CVAR_DEFINE_AUTO( r_lockpvs, "0", FCVAR_CHEAT, "lockpvs area at current point (pvs test)" );
CVAR_DEFINE_AUTO( r_lockfrustum, "0", FCVAR_CHEAT, "lock frustrum area at current point (cull test)" );
CVAR_DEFINE_AUTO( r_traceglow, "0", FCVAR_GLCONFIG, "cull flares behind models" );
CVAR_DEFINE_AUTO( gl_round_down, "2", FCVAR_GLCONFIG|FCVAR_READ_ONLY, "round texture sizes to nearest POT value" );
CVAR_DEFINE( r_vbo, "gl_vbo", "0", FCVAR_GLCONFIG, "draw world using VBO (known to be glitchy)" );
CVAR_DEFINE( r_vbo_detail, "gl_vbo_detail", "0", FCVAR_GLCONFIG, "detail vbo mode (0: disable, 1: multipass, 2: singlepass, broken decal dlights)" );
CVAR_DEFINE( r_vbo_dlightmode, "gl_vbo_dlightmode", "1", FCVAR_GLCONFIG, "vbo dlight rendering mode (0-1)" );
CVAR_DEFINE( r_vbo_overbrightmode, "gl_vbo_overbrightmode", "0", FCVAR_GLCONFIG, "vbo overbright rendering mode (0-1)" );
CVAR_DEFINE_AUTO( r_ripple, "0", FCVAR_GLCONFIG, "enable software-like water texture ripple simulation" );
CVAR_DEFINE_AUTO( r_ripple_updatetime, "0.05", FCVAR_GLCONFIG, "how fast ripple simulation is" );
CVAR_DEFINE_AUTO( r_ripple_spawntime, "0.1", FCVAR_GLCONFIG, "how fast new ripples spawn" );
CVAR_DEFINE_AUTO( r_large_lightmaps, "0", FCVAR_GLCONFIG|FCVAR_LATCH, "enable larger lightmap atlas textures (might break custom renderer mods)" );
CVAR_DEFINE_AUTO( r_dlight_virtual_radius, "3", FCVAR_GLCONFIG, "increase dlight radius virtually by this amount, should help against ugly cut off dlights on highly scaled textures" );

DEFINE_ENGINE_SHARED_CVAR_LIST()

poolhandle_t r_temppool;

gl_globals_t	tr;
glconfig_t	glConfig;
glstate_t	glState;
glwstate_t	glw_state;

#if XASH_GL_STATIC
	#define GL_CALL( x ) #x, NULL
#else
	#define GL_CALL( x ) #x, (void**)&p##x
#endif

static const dllfunc_t opengl_110funcs[] =
{
{ GL_CALL( glClearColor ) },
{ GL_CALL( glClear ) },
{ GL_CALL( glAlphaFunc ) },
{ GL_CALL( glBlendFunc ) },
{ GL_CALL( glCullFace ) },
{ GL_CALL( glDrawBuffer ) },
{ GL_CALL( glReadBuffer ) },
{ GL_CALL( glAccum ) },
{ GL_CALL( glEnable ) },
{ GL_CALL( glDisable ) },
{ GL_CALL( glEnableClientState ) },
{ GL_CALL( glDisableClientState ) },
{ GL_CALL( glGetBooleanv ) },
{ GL_CALL( glGetDoublev ) },
{ GL_CALL( glGetFloatv ) },
{ GL_CALL( glGetIntegerv ) },
{ GL_CALL( glGetError ) },
{ GL_CALL( glGetString ) },
{ GL_CALL( glFinish ) },
{ GL_CALL( glFlush ) },
{ GL_CALL( glClearDepth ) },
{ GL_CALL( glDepthFunc ) },
{ GL_CALL( glDepthMask ) },
{ GL_CALL( glDepthRange ) },
{ GL_CALL( glFrontFace ) },
{ GL_CALL( glDrawElements ) },
{ GL_CALL( glDrawArrays ) },
{ GL_CALL( glColorMask ) },
{ GL_CALL( glIndexPointer ) },
{ GL_CALL( glVertexPointer ) },
{ GL_CALL( glNormalPointer ) },
{ GL_CALL( glColorPointer ) },
{ GL_CALL( glTexCoordPointer ) },
{ GL_CALL( glArrayElement ) },
{ GL_CALL( glColor3f ) },
{ GL_CALL( glColor3fv ) },
{ GL_CALL( glColor4f ) },
{ GL_CALL( glColor4fv ) },
{ GL_CALL( glColor3ub ) },
{ GL_CALL( glColor4ub ) },
{ GL_CALL( glColor4ubv ) },
{ GL_CALL( glTexCoord1f ) },
{ GL_CALL( glTexCoord2f ) },
{ GL_CALL( glTexCoord3f ) },
{ GL_CALL( glTexCoord4f ) },
{ GL_CALL( glTexCoord1fv ) },
{ GL_CALL( glTexCoord2fv ) },
{ GL_CALL( glTexCoord3fv ) },
{ GL_CALL( glTexCoord4fv ) },
{ GL_CALL( glTexGenf ) },
{ GL_CALL( glTexGenfv ) },
{ GL_CALL( glTexGeni ) },
{ GL_CALL( glVertex2f ) },
{ GL_CALL( glVertex3f ) },
{ GL_CALL( glVertex3fv ) },
{ GL_CALL( glNormal3f ) },
{ GL_CALL( glNormal3fv ) },
{ GL_CALL( glBegin ) },
{ GL_CALL( glEnd ) },
{ GL_CALL( glLineWidth ) },
{ GL_CALL( glPointSize ) },
{ GL_CALL( glMatrixMode ) },
{ GL_CALL( glOrtho ) },
{ GL_CALL( glRasterPos2f ) },
{ GL_CALL( glFrustum ) },
{ GL_CALL( glViewport ) },
{ GL_CALL( glPushMatrix ) },
{ GL_CALL( glPopMatrix ) },
{ GL_CALL( glPushAttrib ) },
{ GL_CALL( glPopAttrib ) },
{ GL_CALL( glLoadIdentity ) },
{ GL_CALL( glLoadMatrixd ) },
{ GL_CALL( glLoadMatrixf ) },
{ GL_CALL( glMultMatrixd ) },
{ GL_CALL( glMultMatrixf ) },
{ GL_CALL( glRotated ) },
{ GL_CALL( glRotatef ) },
{ GL_CALL( glScaled ) },
{ GL_CALL( glScalef ) },
{ GL_CALL( glTranslated ) },
{ GL_CALL( glTranslatef ) },
{ GL_CALL( glReadPixels ) },
{ GL_CALL( glDrawPixels ) },
{ GL_CALL( glStencilFunc ) },
{ GL_CALL( glStencilMask ) },
{ GL_CALL( glStencilOp ) },
{ GL_CALL( glClearStencil ) },
{ GL_CALL( glIsEnabled ) },
{ GL_CALL( glIsList ) },
{ GL_CALL( glIsTexture ) },
{ GL_CALL( glTexEnvf ) },
{ GL_CALL( glTexEnvfv ) },
{ GL_CALL( glTexEnvi ) },
{ GL_CALL( glTexParameterf ) },
{ GL_CALL( glTexParameterfv ) },
{ GL_CALL( glTexParameteri ) },
{ GL_CALL( glHint ) },
{ GL_CALL( glPixelStoref ) },
{ GL_CALL( glPixelStorei ) },
{ GL_CALL( glGenTextures ) },
{ GL_CALL( glDeleteTextures ) },
{ GL_CALL( glBindTexture ) },
{ GL_CALL( glTexImage1D ) },
{ GL_CALL( glTexImage2D ) },
{ GL_CALL( glTexSubImage1D ) },
{ GL_CALL( glTexSubImage2D ) },
{ GL_CALL( glCopyTexImage1D ) },
{ GL_CALL( glCopyTexImage2D ) },
{ GL_CALL( glCopyTexSubImage1D ) },
{ GL_CALL( glCopyTexSubImage2D ) },
{ GL_CALL( glScissor ) },
{ GL_CALL( glGetTexImage ) },
{ GL_CALL( glGetTexEnviv ) },
{ GL_CALL( glPolygonOffset ) },
{ GL_CALL( glPolygonMode ) },
{ GL_CALL( glPolygonStipple ) },
{ GL_CALL( glClipPlane ) },
{ GL_CALL( glGetClipPlane ) },
{ GL_CALL( glShadeModel ) },
{ GL_CALL( glGetTexLevelParameteriv ) },
{ GL_CALL( glGetTexLevelParameterfv ) },
{ GL_CALL( glFogfv ) },
{ GL_CALL( glFogf ) },
{ GL_CALL( glFogi ) },
};

static const dllfunc_t debugoutputfuncs[] =
{
{ GL_CALL( glDebugMessageControlARB ) },
{ GL_CALL( glDebugMessageInsertARB ) },
{ GL_CALL( glDebugMessageCallbackARB ) },
{ GL_CALL( glGetDebugMessageLogARB ) },
};

static const dllfunc_t multitexturefuncs[] =
{
{ GL_CALL( glMultiTexCoord1f ) },
{ GL_CALL( glMultiTexCoord2f ) },
{ GL_CALL( glMultiTexCoord3f ) },
{ GL_CALL( glMultiTexCoord4f ) },
{ GL_CALL( glActiveTexture ) },
{ GL_CALL( glActiveTextureARB ) },
{ GL_CALL( glClientActiveTexture ) },
{ GL_CALL( glClientActiveTextureARB ) },
};

static const dllfunc_t texture3dextfuncs[] =
{
{ GL_CALL( glTexImage3D ) },
{ GL_CALL( glTexSubImage3D ) },
{ GL_CALL( glCopyTexSubImage3D ) },
};

static const dllfunc_t texturecompressionfuncs[] =
{
{ GL_CALL( glCompressedTexImage3DARB ) },
{ GL_CALL( glCompressedTexImage2DARB ) },
{ GL_CALL( glCompressedTexImage1DARB ) },
{ GL_CALL( glCompressedTexSubImage3DARB ) },
{ GL_CALL( glCompressedTexSubImage2DARB ) },
{ GL_CALL( glCompressedTexSubImage1DARB ) },
{ GL_CALL( glGetCompressedTexImage ) },
};

static const dllfunc_t vbofuncs[] =
{
{ GL_CALL( glBindBufferARB ) },
{ GL_CALL( glDeleteBuffersARB ) },
{ GL_CALL( glGenBuffersARB ) },
{ GL_CALL( glIsBufferARB ) },
#if !XASH_GLES
{ GL_CALL( glMapBufferARB ) },
{ GL_CALL( glUnmapBufferARB ) },
#endif
{ GL_CALL( glBufferDataARB ) },
{ GL_CALL( glBufferSubDataARB ) },
};

static const dllfunc_t multisampletexfuncs[] =
{
{ GL_CALL(glTexImage2DMultisample) },
};

static const dllfunc_t drawrangeelementsfuncs[] =
{
{ GL_CALL( glDrawRangeElements ) },
};

static const dllfunc_t drawrangeelementsextfuncs[] =
{
{ GL_CALL( glDrawRangeElementsEXT ) },
};


// mangling in gl2shim???
// still need resolve some ext dynamicly, and mangling beginend wrappers will help only with LTO
// anyway this will not work with gl-wes/nanogl, we do not link to libGLESv2, so skip this now
#if !XASH_GL_STATIC
static const dllfunc_t mapbufferrangefuncs[] =
{
{ GL_CALL( glMapBufferRange ) },
{ GL_CALL( glFlushMappedBufferRange ) },
#if XASH_GLES
{ GL_CALL( glUnmapBufferARB ) },
#endif
};

static const dllfunc_t drawrangeelementsbasevertexfuncs[] =
{
{ GL_CALL( glDrawRangeElementsBaseVertex ) },
};

static const dllfunc_t bufferstoragefuncs[] =
{
{ GL_CALL( glBufferStorage ) },
};

static const dllfunc_t shaderobjectsfuncs[] =
{
{ GL_CALL( glDeleteObjectARB ) },
{ GL_CALL( glGetHandleARB ) },
{ GL_CALL( glDetachObjectARB ) },
{ GL_CALL( glCreateShaderObjectARB ) },
{ GL_CALL( glShaderSourceARB ) },
{ GL_CALL( glCompileShaderARB ) },
{ GL_CALL( glCreateProgramObjectARB ) },
{ GL_CALL( glAttachObjectARB ) },
{ GL_CALL( glLinkProgramARB ) },
{ GL_CALL( glUseProgramObjectARB ) },
{ GL_CALL( glValidateProgramARB ) },
{ GL_CALL( glUniform1fARB ) },
{ GL_CALL( glUniform2fARB ) },
{ GL_CALL( glUniform3fARB ) },
{ GL_CALL( glUniform4fARB ) },
{ GL_CALL( glUniform1iARB ) },
{ GL_CALL( glUniform2iARB ) },
{ GL_CALL( glUniform3iARB ) },
{ GL_CALL( glUniform4iARB ) },
{ GL_CALL( glUniform1fvARB ) },
{ GL_CALL( glUniform2fvARB ) },
{ GL_CALL( glUniform3fvARB ) },
{ GL_CALL( glUniform4fvARB ) },
{ GL_CALL( glUniform1ivARB ) },
{ GL_CALL( glUniform2ivARB ) },
{ GL_CALL( glUniform3ivARB ) },
{ GL_CALL( glUniform4ivARB ) },
{ GL_CALL( glUniformMatrix2fvARB ) },
{ GL_CALL( glUniformMatrix3fvARB ) },
{ GL_CALL( glUniformMatrix4fvARB ) },
{ GL_CALL( glGetObjectParameterfvARB ) },
{ GL_CALL( glGetObjectParameterivARB ) },
{ GL_CALL( glGetInfoLogARB ) },
{ GL_CALL( glGetAttachedObjectsARB ) },
{ GL_CALL( glGetUniformLocationARB ) },
{ GL_CALL( glGetActiveUniformARB ) },
{ GL_CALL( glGetUniformfvARB ) },
{ GL_CALL( glGetUniformivARB ) },
{ GL_CALL( glGetShaderSourceARB ) },
{ GL_CALL( glVertexAttribPointerARB ) },
{ GL_CALL( glEnableVertexAttribArrayARB ) },
{ GL_CALL( glDisableVertexAttribArrayARB ) },
{ GL_CALL( glBindAttribLocationARB ) },
{ GL_CALL( glGetActiveAttribARB ) },
{ GL_CALL( glGetAttribLocationARB ) },
{ GL_CALL( glVertexAttrib2fARB ) },
{ GL_CALL( glVertexAttrib2fvARB ) },
//{ GL_CALL( glVertexAttrib3fv ) },
//{ GL_CALL( glVertexAttrib4f ) },
//{ GL_CALL( glVertexAttrib4fv ) },
//{ GL_CALL( glVertexAttrib4ubv ) },
};

/*
==================
Even if *ARB functions may work in GL driver in Core context,
renderdoc completely ignores this calls, so we cannot workaround this
by removing ARB suffix after failed function resolve
I desided not to remove ARB suffix from function declarations because
it historicaly related to ARB_shader_object extension, not GL2+ functions
and all shader code from XashXT/ancient xash3d uses it too
Commented out lines left there intentionally to prevent usage on core/gles
==================
*/

static const dllfunc_t shaderobjectsfuncs_gles[] =
{
{ "glDeleteShader"             , (void **)&pglDeleteObjectARB },
//{ "glGetHandleARB"           , (void **)&pglGetHandleARB },
{ "glDetachShader"             , (void **)&pglDetachObjectARB },
{ "glCreateShader"             , (void **)&pglCreateShaderObjectARB },
{ "glShaderSource"             , (void **)&pglShaderSourceARB },
{ "glCompileShader"            , (void **)&pglCompileShaderARB },
{ "glCreateProgram"            , (void **)&pglCreateProgramObjectARB },
{ "glAttachShader"             , (void **)&pglAttachObjectARB },
{ "glLinkProgram"              , (void **)&pglLinkProgramARB },
{ "glUseProgram"               , (void **)&pglUseProgramObjectARB },
{ "glValidateProgram"          , (void **)&pglValidateProgramARB },
{ "glUniform1f"                , (void **)&pglUniform1fARB },
{ "glUniform2f"                , (void **)&pglUniform2fARB },
{ "glUniform3f"                , (void **)&pglUniform3fARB },
{ "glUniform4f"                , (void **)&pglUniform4fARB },
{ "glUniform1i"                , (void **)&pglUniform1iARB },
{ "glUniform2i"                , (void **)&pglUniform2iARB },
{ "glUniform3i"                , (void **)&pglUniform3iARB },
{ "glUniform4i"                , (void **)&pglUniform4iARB },
{ "glUniform1f"                , (void **)&pglUniform1fvARB },
{ "glUniform2fv"               , (void **)&pglUniform2fvARB },
{ "glUniform3fv"               , (void **)&pglUniform3fvARB },
{ "glUniform4fv"               , (void **)&pglUniform4fvARB },
{ "glUniform1iv"               , (void **)&pglUniform1ivARB },
{ "glUniform2iv"               , (void **)&pglUniform2ivARB },
{ "glUniform3iv"               , (void **)&pglUniform3ivARB },
{ "glUniform4iv"               , (void **)&pglUniform4ivARB },
{ "glUniformMatrix2fv"         , (void **)&pglUniformMatrix2fvARB },
{ "glUniformMatrix3fv"         , (void **)&pglUniformMatrix3fvARB },
{ "glUniformMatrix4fv"         , (void **)&pglUniformMatrix4fvARB },
//{ "glGetShaderfv"            , (void **)&pglGetObjectParameterfvARB }, // missing in ES2?
{ "glGetShaderiv"              , (void **)&pglGetObjectParameterivARB },
{ "glGetShaderInfoLog"         , (void **)&pglGetInfoLogARB },
//{ "glGetAttachedObjects"     , (void **)&pglGetAttachedObjectsARB }, // missing in ES2?
{ "glGetUniformLocation"       , (void **)&pglGetUniformLocationARB },
{ "glGetActiveUniform"         , (void **)&pglGetActiveUniformARB },
{ "glGetUniformfv"             , (void **)&pglGetUniformfvARB },
{ "glGetUniformiv"             , (void **)&pglGetUniformivARB },
{ "glGetShaderSource"          , (void **)&pglGetShaderSourceARB },
{ "glVertexAttribPointer"      , (void **)&pglVertexAttribPointerARB },
{ "glEnableVertexAttribArray"  , (void **)&pglEnableVertexAttribArrayARB },
{ "glDisableVertexAttribArray" , (void **)&pglDisableVertexAttribArrayARB },
{ "glBindAttribLocation"       , (void **)&pglBindAttribLocationARB },
{ "glGetActiveAttrib"          , (void **)&pglGetActiveAttribARB },
{ "glGetAttribLocation"        , (void **)&pglGetAttribLocationARB },
{ "glVertexAttrib2f"           , (void **)&pglVertexAttrib2fARB },
{ "glVertexAttrib2fv"          , (void **)&pglVertexAttrib2fvARB },
{ "glVertexAttrib3fv"          , (void **)&pglVertexAttrib3fvARB },

// Core/GLES only
{ GL_CALL( glGetProgramiv ) },
{ GL_CALL( glDeleteProgram ) },
{ GL_CALL( glGetProgramInfoLog ) },
//{ "glVertexAttrib4f"              , (void **)&pglVertexAttrib4fARB },
//{ "glVertexAttrib4fv"             , (void **)&pglVertexAttrib4fvARB },
//{ "glVertexAttrib4ubv"            , (void **)&pglVertexAttrib4ubvARB },
};

static const dllfunc_t vaofuncs[] =
{
{ GL_CALL( glBindVertexArray ) },
{ GL_CALL( glDeleteVertexArrays ) },
{ GL_CALL( glGenVertexArrays ) },
{ GL_CALL( glIsVertexArray ) },
};

static const dllfunc_t multitexturefuncs_es[] =
{
{ GL_CALL( glActiveTexture ) },
{ GL_CALL( glActiveTextureARB ) },
{ GL_CALL( glClientActiveTexture ) },
{ GL_CALL( glClientActiveTextureARB ) },
};

static const dllfunc_t multitexturefuncs_es2[] =
{
{ GL_CALL( glActiveTexture ) },
{ GL_CALL( glActiveTextureARB ) },
};

#endif // !XASH_GL_STATIC

/*
========================
DebugCallback

For ARB_debug_output
========================
*/
static void APIENTRY GL_DebugOutput( GLuint source, GLuint type, GLuint id, GLuint severity, GLint length, const GLcharARB *message, GLvoid *userParam )
{
	switch( type )
	{
	case GL_DEBUG_TYPE_ERROR_ARB:
		gEngfuncs.Con_Printf( S_OPENGL_ERROR "%s\n", message );
		break;
	case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_ARB:
	case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_ARB:
		gEngfuncs.Con_Printf( S_OPENGL_WARN "%s\n", message );
		break;
	case GL_DEBUG_TYPE_PORTABILITY_ARB:
		gEngfuncs.Con_Reportf( S_OPENGL_WARN "%s\n", message );
		break;
	case GL_DEBUG_TYPE_PERFORMANCE_ARB:
	case GL_DEBUG_TYPE_OTHER_ARB:
	default:
		gEngfuncs.Con_Printf( S_OPENGL_NOTE "%s\n", message );
		break;
	}
}

/*
=================
GL_SetExtension
=================
*/
static void GL_SetExtension( int r_ext, int enable )
{
	if( r_ext >= 0 && r_ext < GL_EXTCOUNT )
		glConfig.extension[r_ext] = enable ? GL_TRUE : GL_FALSE;
	else gEngfuncs.Con_Printf( S_ERROR "%s: invalid extension %d\n", __func__, r_ext );
}

/*
=================
GL_CheckExtension
=================
*/
static qboolean GL_CheckExtension( const char *name, const dllfunc_t *funcs, size_t num_funcs, const char *cvarname, int r_ext, float minver )
{
	size_t i;
	cvar_t *parm = NULL;
	const char *extensions_string;
	char desc[MAX_VA_STRING];
	float glver = (float)glConfig.version_major + glConfig.version_minor / 10.0f;

	gEngfuncs.Con_Reportf( "%s: %s ", __func__, name );
	GL_SetExtension( r_ext, true );

	if( cvarname )
	{
		// system config disable extensions
		Q_snprintf( desc, sizeof( desc ), CVAR_GLCONFIG_DESCRIPTION, name );
		parm = gEngfuncs.Cvar_Get( cvarname, "1", FCVAR_GLCONFIG|FCVAR_READ_ONLY, desc );
	}

	if(( parm && !parm->value ) || ( !gl_extensions.value && r_ext != GL_OPENGL_110 ))
	{
		gEngfuncs.Con_Reportf( "- disabled\n" );
		GL_SetExtension( r_ext, false );
		return false; // nothing to process at
	}

	extensions_string = glConfig.extensions_string;

	if(( name[2] == '_' || name[3] == '_' ) && !Q_strstr( extensions_string, name ) && ( glver < minver  || !minver || !glver ) )
	{
		GL_SetExtension( r_ext, false );	// update render info
		gEngfuncs.Con_Reportf( "- ^1failed\n" );
		return false;
	}

#if !XASH_GL_STATIC
	// clear exports
	ClearExports( funcs, num_funcs );

	for( i = 0; i < num_funcs; i++ )
	{
		// functions are cleared before all the extensions are evaluated
		if(( *(funcs[i].func) = (void *)gEngfuncs.GL_GetProcAddress( funcs[i].name )) == NULL )
		{
			string name;
			char *end;
			size_t j = 0;
#if XASH_GLES
			const char *suffixes[] = { "", "EXT", "OES" };
#else
			const char *suffixes[] = { "", "EXT" };
#endif

			// HACK: fix ARB names
			Q_strncpy( name, funcs[i].name, sizeof( name ));
			if(( end = Q_strstr( name, "ARB" )))
			{
				*end = '\0';
			}
			else // I need Q_strstrnul
			{
				end = name + Q_strlen( name );
				j++; // skip empty suffix
			}

			for( ; j < sizeof( suffixes ) / sizeof( suffixes[0] ); j++ )
			{
				void *f;

				Q_strncat( name, suffixes[j], sizeof( name ));

				if(( f = gEngfuncs.GL_GetProcAddress( name )))
				{
					// GL_GetProcAddress prints errors about missing functions, so tell user that we found it with different name
					gEngfuncs.Con_Printf( S_NOTE "found %s\n", name );

					*(funcs[i].func) = f;
					break;
				}
				else
				{
					*end = '\0'; // cut suffix, try next
				}
			}

			// not found...
			if( j == sizeof( suffixes ) / sizeof( suffixes[0] ))
			{
				GL_SetExtension( r_ext, false );
			}
		}
	}
#endif

	if( GL_Support( r_ext ))
	{
		gEngfuncs.Con_Reportf( "- ^2enabled\n" );
		return true;
	}

	gEngfuncs.Con_Reportf( "- ^1failed\n" );
	return false;
}

/*
==============
GL_GetProcAddress

defined just for nanogl/glwes, so it don't link to SDL2 directly, nor use dlsym
==============
*/
void GAME_EXPORT *GL_GetProcAddress( const char *name ); // keep defined for nanogl/wes
void GAME_EXPORT *GL_GetProcAddress( const char *name )
{
	return gEngfuncs.GL_GetProcAddress( name );
}

/*
===============
GL_SetDefaultTexState
===============
*/
static void GL_SetDefaultTexState( void )
{

	int	i;

	memset( glState.currentTextures, -1, MAX_TEXTURE_UNITS * sizeof( *glState.currentTextures ));
	memset( glState.texCoordArrayMode, 0, MAX_TEXTURE_UNITS * sizeof( *glState.texCoordArrayMode ));
	memset( glState.genSTEnabled, 0, MAX_TEXTURE_UNITS * sizeof( *glState.genSTEnabled ));

	for( i = 0; i < MAX_TEXTURE_UNITS; i++ )
	{
		glState.currentTextureTargets[i] = GL_NONE;
		glState.texIdentityMatrix[i] = true;
	}
}

/*
===============
GL_SetDefaultState
===============
*/
static void GL_SetDefaultState( void )
{
	memset( &glState, 0, sizeof( glState ));
	GL_SetDefaultTexState ();

	// init draw stack
	tr.draw_list = &tr.draw_stack[0];
	tr.draw_stack_pos = 0;
}

/*
===============
GL_SetDefaults
===============
*/
static void GL_SetDefaults( void )
{
	pglFinish();

	pglClearColor( 0.5f, 0.5f, 0.5f, 1.0f );

	pglDisable( GL_DEPTH_TEST );
	pglDisable( GL_CULL_FACE );
	pglDisable( GL_SCISSOR_TEST );
	pglDepthFunc( GL_LEQUAL );
	pglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );

	if( glState.stencilEnabled )
	{
		pglDisable( GL_STENCIL_TEST );
		pglStencilMask( ( GLuint ) ~0 );
		pglStencilFunc( GL_EQUAL, 0, ~0 );
		pglStencilOp( GL_KEEP, GL_INCR, GL_INCR );
	}

	pglPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
	pglPolygonOffset( -1.0f, -2.0f );

	GL_CleanupAllTextureUnits();

	pglDisable( GL_BLEND );
	pglDisable( GL_ALPHA_TEST );
	pglDisable( GL_POLYGON_OFFSET_FILL );
	pglAlphaFunc( GL_GREATER, DEFAULT_ALPHATEST );
	pglEnable( GL_TEXTURE_2D );
	pglShadeModel( GL_SMOOTH );
	pglFrontFace( GL_CCW );

	pglPointSize( 1.2f );
	pglLineWidth( 1.2f );

	GL_Cull( GL_NONE );
}


/*
=================
R_RenderInfo_f
=================
*/
static void R_RenderInfo_f( void )
{
	gEngfuncs.Con_Printf( "\n" );
	gEngfuncs.Con_Printf( "GL_VENDOR: %s\n", glConfig.vendor_string );
	gEngfuncs.Con_Printf( "GL_RENDERER: %s\n", glConfig.renderer_string );
	gEngfuncs.Con_Printf( "GL_VERSION: %s\n", glConfig.version_string );

	// don't spam about extensions
	gEngfuncs.Con_Reportf( "GL_EXTENSIONS: %s\n", glConfig.extensions_string );

	if( glConfig.wrapper == GLES_WRAPPER_GL4ES )
	{
		const char *vendor = (const char *)pglGetString( GL_VENDOR | 0x10000 );
		const char *renderer = (const char *)pglGetString( GL_RENDERER | 0x10000 );
		const char *version = (const char *)pglGetString( GL_VERSION | 0x10000 );
		const char *extensions = (const char *)pglGetString( GL_EXTENSIONS | 0x10000 );

		if( vendor )
			gEngfuncs.Con_Printf( "GL4ES_VENDOR: %s\n", vendor );
		if( renderer )
			gEngfuncs.Con_Printf( "GL4ES_RENDERER: %s\n", renderer );
		if( version )
			gEngfuncs.Con_Printf( "GL4ES_VERSION: %s\n", version );
		if( extensions )
			gEngfuncs.Con_Reportf( "GL4ES_EXTENSIONS: %s\n", extensions );
	}

	gEngfuncs.Con_Printf( "GL_MAX_TEXTURE_SIZE: %i\n", glConfig.max_2d_texture_size );

	if( GL_Support( GL_ARB_MULTITEXTURE ))
		gEngfuncs.Con_Printf( "GL_MAX_TEXTURE_UNITS_ARB: %i\n", glConfig.max_texture_units );
	if( GL_Support( GL_TEXTURE_CUBEMAP_EXT ))
		gEngfuncs.Con_Printf( "GL_MAX_CUBE_MAP_TEXTURE_SIZE_ARB: %i\n", glConfig.max_cubemap_size );
	if( GL_Support( GL_ANISOTROPY_EXT ))
		gEngfuncs.Con_Printf( "GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT: %.1f\n", glConfig.max_texture_anisotropy );
	if( GL_Support( GL_TEXTURE_2D_RECT_EXT ))
		gEngfuncs.Con_Printf( "GL_MAX_RECTANGLE_TEXTURE_SIZE: %i\n", glConfig.max_2d_rectangle_size );
	if( GL_Support( GL_TEXTURE_ARRAY_EXT ))
		gEngfuncs.Con_Printf( "GL_MAX_ARRAY_TEXTURE_LAYERS_EXT: %i\n", glConfig.max_2d_texture_layers );
	if( GL_Support( GL_SHADER_GLSL100_EXT ))
	{
		gEngfuncs.Con_Printf( "GL_MAX_TEXTURE_COORDS_ARB: %i\n", glConfig.max_texture_coords );
		gEngfuncs.Con_Printf( "GL_MAX_TEXTURE_IMAGE_UNITS_ARB: %i\n", glConfig.max_teximage_units );
		gEngfuncs.Con_Printf( "GL_MAX_VERTEX_UNIFORM_COMPONENTS_ARB: %i\n", glConfig.max_vertex_uniforms );
		gEngfuncs.Con_Printf( "GL_MAX_VERTEX_ATTRIBS_ARB: %i\n", glConfig.max_vertex_attribs );
	}

	gEngfuncs.Con_Printf( "\n" );
	gEngfuncs.Con_Printf( "MODE: %ix%i\n", gpGlobals->width, gpGlobals->height );
	gEngfuncs.Con_Printf( "\n" );
	gEngfuncs.Con_Printf( "VERTICAL SYNC: %s\n", gl_vsync->value ? "enabled" : "disabled" );
	gEngfuncs.Con_Printf( "Color %d bits, Alpha %d bits, Depth %d bits, Stencil %d bits\n", glConfig.color_bits,
		glConfig.alpha_bits, glConfig.depth_bits, glConfig.stencil_bits );
}

#if XASH_GLES
static void GL_InitExtensionsGLES( void )
{
	int extid;

	// intialize wrapper type
#if XASH_NANOGL
	glConfig.context = CONTEXT_TYPE_GLES_1_X;
	glConfig.wrapper = GLES_WRAPPER_NANOGL;
#elif XASH_WES
	glConfig.context = CONTEXT_TYPE_GLES_2_X;
	glConfig.wrapper = GLES_WRAPPER_WES;
#elif XASH_GLES3COMPAT
	glConfig.context = CONTEXT_TYPE_GLES_2_X;
	glConfig.wrapper = GLES_WRAPPER_NONE;
#elif XASH_WEBGL
	glConfig.context = CONTEXT_TYPE_GL_CORE;
	glConfig.wrapper = GLES_WRAPPER_NONE;
#else
	#error "unknown gles wrapper"
#endif

	glConfig.hardware_type = GLHW_GENERIC;

	for( extid = GL_OPENGL_110 + 1; extid < GL_EXTCOUNT; extid++ )
	{
		switch( extid )
		{
		case GL_ARB_VERTEX_BUFFER_OBJECT_EXT:
			GL_CheckExtension( "vertex_buffer_object", vbofuncs, ARRAYSIZE( vbofuncs ), "gl_vertex_buffer_object", extid, 1.0 );
			break;
		case GL_ARB_MULTITEXTURE:
			if( !GL_CheckExtension( "multitexture", multitexturefuncs, ARRAYSIZE( multitexturefuncs ), "gl_arb_multitexture", GL_ARB_MULTITEXTURE, 1.0 ) && glConfig.wrapper == GLES_WRAPPER_NONE )
			{
#if !XASH_GL_STATIC
				if( !GL_CheckExtension( "multitexture_es1", multitexturefuncs_es, ARRAYSIZE( multitexturefuncs_es ), "gl_arb_multitexture", GL_ARB_MULTITEXTURE, 1.0 )
						&& !GL_CheckExtension( "multitexture_es2", multitexturefuncs_es2, ARRAYSIZE( multitexturefuncs_es2 ), "gl_arb_multitexture", GL_ARB_MULTITEXTURE, 2.0 ))
					break;
#endif
			}
			GL_SetExtension( extid, true ); // required to be supported by wrapper

			pglGetIntegerv( GL_MAX_TEXTURE_UNITS_ARB, &glConfig.max_texture_units );
			if( glConfig.max_texture_units <= 1 )
				pglGetIntegerv( GL_MAX_TEXTURE_IMAGE_UNITS_ARB, &glConfig.max_texture_units );
			if( glConfig.max_texture_units <= 1 )
			{
				GL_SetExtension( extid, false );
				glConfig.max_texture_units = 1;
			}

			glConfig.max_texture_coords = glConfig.max_teximage_units = glConfig.max_texture_units;
			break;
		case GL_TEXTURE_CUBEMAP_EXT:
			if( GL_CheckExtension( "GL_OES_texture_cube_map", NULL, 0, "gl_texture_cubemap", extid, 0 ))
				pglGetIntegerv( GL_MAX_CUBE_MAP_TEXTURE_SIZE_ARB, &glConfig.max_cubemap_size );
			break;
		case GL_ANISOTROPY_EXT:
			glConfig.max_texture_anisotropy = 0.0f;
			if( GL_CheckExtension( "GL_EXT_texture_filter_anisotropic", NULL, 0, "gl_ext_anisotropic_filter", extid, 0 ))
				pglGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &glConfig.max_texture_anisotropy );
			break;
		case GL_TEXTURE_LOD_BIAS:
			if( GL_CheckExtension( "GL_EXT_texture_lod_bias", NULL, 0, "gl_texture_mipmap_biasing", extid, 0 ))
				pglGetFloatv( GL_MAX_TEXTURE_LOD_BIAS_EXT, &glConfig.max_texture_lod_bias );
			break;
		case GL_ARB_TEXTURE_NPOT_EXT:
			GL_CheckExtension( "GL_OES_texture_npot", NULL, 0, "gl_texture_npot", extid, 0 );
			break;
#if !XASH_GL_STATIC
		case GL_SHADER_OBJECTS_EXT:
			GL_CheckExtension( "ES2 Shaders", shaderobjectsfuncs_gles, ARRAYSIZE( shaderobjectsfuncs_gles ), "gl_shaderobjects", extid, 2.0 );
			break;
		case GL_ARB_VERTEX_ARRAY_OBJECT_EXT:
			if( !GL_CheckExtension( "GL_OES_vertex_array_object", vaofuncs, ARRAYSIZE( vaofuncs ), "gl_vertex_array_object", extid, 3.0 ))
				GL_CheckExtension( "GL_EXT_vertex_array_object", vaofuncs, ARRAYSIZE( vaofuncs ), "gl_vertex_array_object", extid, 3.0 );
			break;
		case GL_DRAW_RANGEELEMENTS_EXT:
			if( !GL_CheckExtension( "GL_EXT_draw_range_elements", drawrangeelementsfuncs, ARRAYSIZE( drawrangeelementsfuncs ), "gl_drawrangeelements", extid, 3.0 ))
				GL_CheckExtension( "GL_OES_draw_range_elements", drawrangeelementsfuncs, ARRAYSIZE( drawrangeelementsfuncs ), "gl_drawrangeelements", extid, 3.0 );
			break;
		case GL_DRAW_RANGE_ELEMENTS_BASE_VERTEX_EXT:
			if( !GL_CheckExtension( "GL_OES_draw_elements_base_vertex", drawrangeelementsbasevertexfuncs, ARRAYSIZE( drawrangeelementsbasevertexfuncs ), "gl_drawrangeelementsbasevertex", GL_DRAW_RANGE_ELEMENTS_BASE_VERTEX_EXT, 0 ))
				GL_CheckExtension( "GL_EXT_draw_elements_base_vertex", drawrangeelementsbasevertexfuncs, ARRAYSIZE( drawrangeelementsbasevertexfuncs ), "gl_drawrangeelementsbasevertex", GL_DRAW_RANGE_ELEMENTS_BASE_VERTEX_EXT, 3.2 );
			break;
		case GL_MAP_BUFFER_RANGE_EXT:
			GL_CheckExtension( "GL_EXT_map_buffer_range", mapbufferrangefuncs, ARRAYSIZE( mapbufferrangefuncs ), "gl_map_buffer_range", GL_MAP_BUFFER_RANGE_EXT , 3.0);
			break;
		case GL_BUFFER_STORAGE_EXT:
			GL_CheckExtension( "GL_EXT_buffer_storage", bufferstoragefuncs, ARRAYSIZE( bufferstoragefuncs ), "gl_buffer_storage", GL_BUFFER_STORAGE_EXT, 0);
			break;

#endif
		case GL_DEBUG_OUTPUT:
			if( glw_state.extended )
				GL_CheckExtension( "GL_KHR_debug", debugoutputfuncs, ARRAYSIZE( debugoutputfuncs ), "gl_debug_output", extid, 0 );
			else
				GL_SetExtension( extid, false );
			break;
		// case GL_TEXTURE_COMPRESSION_EXT: NOPE
		// case GL_SHADER_GLSL100_EXT: NOPE
		// case GL_TEXTURE_2D_RECT_EXT: NOPE
		// case GL_TEXTURE_ARRAY_EXT: NOPE
		// case GL_TEXTURE_3D_EXT: NOPE
		// case GL_CLAMPTOEDGE_EXT: NOPE
		// case GL_CLAMP_TEXBORDER_EXT: NOPE
		// case GL_ARB_TEXTURE_FLOAT_EXT: NOPE
		// case GL_ARB_DEPTH_FLOAT_EXT: NOPE
		// case GL_ARB_SEAMLESS_CUBEMAP: NOPE
		// case GL_EXT_GPU_SHADER4: NOPE
		// case GL_DEPTH_TEXTURE: NOPE
		// case GL_DRAWRANGEELEMENTS_EXT: NOPE
		default:
			GL_SetExtension( extid, false );
		}
	}
#if !XASH_GL_STATIC
	GL2_ShimInit();
#endif
}
#else
static void GL_InitExtensionsBigGL( void )
{
	// intialize wrapper type
	glConfig.context = gEngfuncs.Sys_CheckParm( "-glcore" )? CONTEXT_TYPE_GL_CORE : CONTEXT_TYPE_GL;
	glConfig.wrapper = GLES_WRAPPER_NONE;

	if( Q_stristr( glConfig.renderer_string, "geforce" ))
		glConfig.hardware_type = GLHW_NVIDIA;
	else if( Q_stristr( glConfig.renderer_string, "quadro fx" ))
		glConfig.hardware_type = GLHW_NVIDIA;
	else if( Q_stristr(glConfig.renderer_string, "rv770" ))
		glConfig.hardware_type = GLHW_RADEON;
	else if( Q_stristr(glConfig.renderer_string, "radeon hd" ))
		glConfig.hardware_type = GLHW_RADEON;
	else if( Q_stristr( glConfig.renderer_string, "eah4850" ) || Q_stristr( glConfig.renderer_string, "eah4870" ))
		glConfig.hardware_type = GLHW_RADEON;
	else if( Q_stristr( glConfig.renderer_string, "radeon" ))
		glConfig.hardware_type = GLHW_RADEON;
	else if( Q_stristr( glConfig.renderer_string, "intel" ))
		glConfig.hardware_type = GLHW_INTEL;
	else glConfig.hardware_type = GLHW_GENERIC;

	// gl4es may be used system-wide
	if( Q_stristr( glConfig.renderer_string, "gl4es" ))
		glConfig.wrapper = GLES_WRAPPER_GL4ES;

	// multitexture
	glConfig.max_texture_units = glConfig.max_texture_coords = glConfig.max_teximage_units = 1;
	if( GL_CheckExtension( "GL_ARB_multitexture", multitexturefuncs, ARRAYSIZE( multitexturefuncs ), "gl_arb_multitexture", GL_ARB_MULTITEXTURE, 1.3f ))
	{
		pglGetIntegerv( GL_MAX_TEXTURE_UNITS_ARB, &glConfig.max_texture_units );
	}

	if( glConfig.max_texture_units == 1 )
		GL_SetExtension( GL_ARB_MULTITEXTURE, false );

	// 3d texture support
	if( GL_CheckExtension( "GL_EXT_texture3D", texture3dextfuncs, ARRAYSIZE( texture3dextfuncs ), "gl_texture_3d", GL_TEXTURE_3D_EXT, 2.0f ))
	{
		pglGetIntegerv( GL_MAX_3D_TEXTURE_SIZE, &glConfig.max_3d_texture_size );

		if( glConfig.max_3d_texture_size < 32 )
		{
			GL_SetExtension( GL_TEXTURE_3D_EXT, false );
			gEngfuncs.Con_Printf( S_ERROR "GL_EXT_texture3D reported bogus GL_MAX_3D_TEXTURE_SIZE, disabled\n" );
		}
	}

	// 2d texture array support
	if( GL_CheckExtension( "GL_EXT_texture_array", texture3dextfuncs, ARRAYSIZE( texture3dextfuncs ), "gl_texture_2d_array", GL_TEXTURE_ARRAY_EXT, 0 ))
		pglGetIntegerv( GL_MAX_ARRAY_TEXTURE_LAYERS_EXT, &glConfig.max_2d_texture_layers );

	// cubemaps support
	if( GL_CheckExtension( "GL_ARB_texture_cube_map", NULL, 0, "gl_texture_cubemap", GL_TEXTURE_CUBEMAP_EXT, 0 ))
	{
		pglGetIntegerv( GL_MAX_CUBE_MAP_TEXTURE_SIZE_ARB, &glConfig.max_cubemap_size );

		// check for seamless cubemaps too
		GL_CheckExtension( "GL_ARB_seamless_cube_map", NULL, 0, "gl_texture_cubemap_seamless", GL_ARB_SEAMLESS_CUBEMAP, 0 );
	}

	GL_CheckExtension( "GL_ARB_texture_non_power_of_two", NULL, 0, "gl_texture_npot", GL_ARB_TEXTURE_NPOT_EXT, 0 );
	GL_CheckExtension( "GL_ARB_texture_compression", texturecompressionfuncs, ARRAYSIZE( texturecompressionfuncs ), "gl_texture_dxt_compression", GL_TEXTURE_COMPRESSION_EXT, 0 );
	if( !GL_CheckExtension( "GL_EXT_texture_edge_clamp", NULL, 0, "gl_clamp_to_edge", GL_CLAMPTOEDGE_EXT, 2.0 )) // present in ES2
		GL_CheckExtension( "GL_SGIS_texture_edge_clamp", NULL, 0, "gl_clamp_to_edge", GL_CLAMPTOEDGE_EXT, 0 );

	glConfig.max_texture_anisotropy = 0.0f;
	if( GL_CheckExtension( "GL_EXT_texture_filter_anisotropic", NULL, 0, "gl_texture_anisotropic_filter", GL_ANISOTROPY_EXT, 0 ))
		pglGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &glConfig.max_texture_anisotropy );

#if XASH_WIN32 // Win32 only drivers?
	// g-cont. because lodbias it too glitchy on Intel's cards
	if( glConfig.hardware_type != GLHW_INTEL )
#endif
	{
		if( GL_CheckExtension( "GL_EXT_texture_lod_bias", NULL, 0, "gl_texture_mipmap_biasing", GL_TEXTURE_LOD_BIAS, 1.4 ))
			pglGetFloatv( GL_MAX_TEXTURE_LOD_BIAS_EXT, &glConfig.max_texture_lod_bias );
	}

	GL_CheckExtension( "GL_ARB_texture_border_clamp", NULL, 0, NULL, GL_CLAMP_TEXBORDER_EXT, 2.0 ); // present in ES2

	GL_CheckExtension( "GL_ARB_depth_texture", NULL, 0, NULL, GL_DEPTH_TEXTURE, 1.4 ); // missing in gles, check GL_OES_depth_texture
	GL_CheckExtension( "GL_ARB_texture_float", NULL, 0, "gl_texture_float", GL_ARB_TEXTURE_FLOAT_EXT, 0 );
	GL_CheckExtension( "GL_ARB_depth_buffer_float", NULL, 0, "gl_texture_depth_float", GL_ARB_DEPTH_FLOAT_EXT, 0 );
	GL_CheckExtension( "GL_EXT_gpu_shader4", NULL, 0, NULL, GL_EXT_GPU_SHADER4, 0 ); // don't confuse users
	GL_CheckExtension( "GL_ARB_vertex_buffer_object", vbofuncs, ARRAYSIZE( vbofuncs ), "gl_vertex_buffer_object", GL_ARB_VERTEX_BUFFER_OBJECT_EXT, 2.0 );
	GL_CheckExtension( "GL_ARB_texture_multisample", multisampletexfuncs, ARRAYSIZE( multisampletexfuncs ), "gl_texture_multisample", GL_TEXTURE_MULTISAMPLE, 0 );
	GL_CheckExtension( "GL_ARB_texture_compression_bptc", NULL, 0, "gl_texture_bptc_compression", GL_ARB_TEXTURE_COMPRESSION_BPTC, 0 );
#if !XASH_GL_STATIC
	if( glConfig.context == CONTEXT_TYPE_GL_CORE )
		GL_CheckExtension( "shader_objects", shaderobjectsfuncs_gles, ARRAYSIZE( shaderobjectsfuncs_gles ), "gl_shaderobjects", GL_SHADER_OBJECTS_EXT, 2.0 );
	else
		GL_CheckExtension( "GL_ARB_shader_objects", shaderobjectsfuncs, ARRAYSIZE( shaderobjectsfuncs ), "gl_shaderobjects", GL_SHADER_OBJECTS_EXT, 2.0 );
	GL_CheckExtension( "GL_ARB_vertex_array_object", vaofuncs, ARRAYSIZE( vaofuncs ), "gl_vertex_array_object", GL_ARB_VERTEX_ARRAY_OBJECT_EXT, 3.0 );
	GL_CheckExtension( "GL_ARB_buffer_storage", bufferstoragefuncs, ARRAYSIZE( bufferstoragefuncs ), "gl_buffer_storage", GL_BUFFER_STORAGE_EXT, 4.4);
	GL_CheckExtension( "GL_ARB_map_buffer_range", mapbufferrangefuncs, ARRAYSIZE( mapbufferrangefuncs ), "gl_map_buffer_range", GL_MAP_BUFFER_RANGE_EXT , 3.0);
	GL_CheckExtension( "GL_ARB_draw_elements_base_vertex", drawrangeelementsbasevertexfuncs, ARRAYSIZE( drawrangeelementsbasevertexfuncs ), "gl_drawrangeelementsbasevertex", GL_DRAW_RANGE_ELEMENTS_BASE_VERTEX_EXT, 3.2 );
#endif
	if( GL_CheckExtension( "GL_ARB_shading_language_100", NULL, 0, NULL, GL_SHADER_GLSL100_EXT, 2.0 ))
	{
		pglGetIntegerv( GL_MAX_TEXTURE_COORDS_ARB, &glConfig.max_texture_coords );
		pglGetIntegerv( GL_MAX_TEXTURE_IMAGE_UNITS_ARB, &glConfig.max_teximage_units );

		// check for hardware skinning
		pglGetIntegerv( GL_MAX_VERTEX_UNIFORM_COMPONENTS_ARB, &glConfig.max_vertex_uniforms );
		pglGetIntegerv( GL_MAX_VERTEX_ATTRIBS_ARB, &glConfig.max_vertex_attribs );

#if XASH_WIN32 // Win32 only drivers?
		if( glConfig.hardware_type == GLHW_RADEON && glConfig.max_vertex_uniforms > 512 )
			glConfig.max_vertex_uniforms /= 4; // radeon returns not correct info
#endif
	}
	else
	{
		// just get from multitexturing
		glConfig.max_texture_coords = glConfig.max_teximage_units = glConfig.max_texture_units;
	}

	// rectangle textures support
	GL_CheckExtension( "GL_ARB_texture_rectangle", NULL, 0, "gl_texture_rectangle", GL_TEXTURE_2D_RECT_EXT, 0 );

	if( !GL_CheckExtension( "glDrawRangeElements", drawrangeelementsfuncs, ARRAYSIZE( drawrangeelementsfuncs ), "gl_drawrangeelements", GL_DRAW_RANGEELEMENTS_EXT, 0 ) )
	{
		if( GL_CheckExtension( "glDrawRangeElementsEXT", drawrangeelementsextfuncs, ARRAYSIZE( drawrangeelementsextfuncs ),
			"gl_drawrangelements", GL_DRAW_RANGEELEMENTS_EXT, 0 ))
		{
#if !XASH_GL_STATIC
			pglDrawRangeElements = pglDrawRangeElementsEXT;
#endif
		}
	}

	// this won't work without extended context
	if( glw_state.extended )
		GL_CheckExtension( "GL_ARB_debug_output", debugoutputfuncs, ARRAYSIZE( debugoutputfuncs ), "gl_debug_output", GL_DEBUG_OUTPUT, 0 );

#if XASH_PSVITA
	// not all GL1.1 functions are implemented in vitaGL, but there's enough
	GL_SetExtension( GL_OPENGL_110, true );
	// init our immediate mode override
	VGL_ShimInit();
#endif
#if !XASH_GLES && !XASH_GL_STATIC
	if( gEngfuncs.Sys_CheckParm( "-gl2shim" ))
		GL2_ShimInit();
#endif
}
#endif

void GL_InitExtensions( void )
{
	char value[MAX_VA_STRING];
	GLint major = 0, minor = 0;

	GL_OnContextCreated();

	// initialize gl extensions
	GL_CheckExtension( "OpenGL 1.1.0", opengl_110funcs, ARRAYSIZE( opengl_110funcs ), NULL, GL_OPENGL_110, 1.0 );

	// get our various GL strings
	glConfig.vendor_string = (const char *)pglGetString( GL_VENDOR );
	glConfig.renderer_string = (const char *)pglGetString( GL_RENDERER );
	glConfig.version_string = (const char *)pglGetString( GL_VERSION );
	glConfig.extensions_string = (const char *)pglGetString( GL_EXTENSIONS );

	pglGetIntegerv( GL_MAJOR_VERSION, &major );
	pglGetIntegerv( GL_MINOR_VERSION, &minor );
	if( !major && glConfig.version_string )
	{
		const char *str = glConfig.version_string;
		float ver;

		while( *str && ( *str < '0' || *str > '9' )) str++;
		ver = Q_atof(str);
		if( ver )
		{
			glConfig.version_major = ver;
			glConfig.version_minor = (int)(ver * 10) % 10;
		}
	}
	else
	{
		glConfig.version_major = major;
		glConfig.version_minor = minor;
	}
#if !XASH_GL_STATIC
	if( !glConfig.extensions_string )
	{
		int n = 0;
		pglGetStringi = gEngfuncs.GL_GetProcAddress( "glGetStringi" );

		pglGetIntegerv( GL_NUM_EXTENSIONS, &n );
		if( n && pglGetStringi )
		{
			int i, len = 1;
			char *str;

			for( i = 0; i < n; i++ )
				len += Q_strlen((const char *)pglGetStringi( GL_EXTENSIONS, i )) + 1;

			str = (char*)Mem_Calloc( r_temppool, len );
			glConfig.extensions_string = str;

			for( i = 0; i < n; i++ )
			{
				int l = Q_strncpy( str, pglGetStringi( GL_EXTENSIONS, i ), len );
				str += l;
				*str++ = ' ';
				len -= l + 1;
			}
		}
	}
#endif
	gEngfuncs.Con_Reportf( "^3Video^7: %s\n", glConfig.renderer_string );

#if XASH_GLES
	GL_InitExtensionsGLES();
#else
	GL_InitExtensionsBigGL();
#endif

	pglGetIntegerv( GL_MAX_TEXTURE_SIZE, &glConfig.max_2d_texture_size );
	if( glConfig.max_2d_texture_size <= 0 ) glConfig.max_2d_texture_size = 256;
#if !XASH_GL4ES
	// enable gldebug if allowed
	if( GL_Support( GL_DEBUG_OUTPUT ))
	{
		if( gpGlobals->developer )
		{
			gEngfuncs.Con_Reportf( "Installing GL_DebugOutput...\n");
			pglDebugMessageCallbackARB( GL_DebugOutput, NULL );

			// force everything to happen in the main thread instead of in a separate driver thread
			pglEnable( GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB );

		}

		// enable all the low priority messages
		pglDebugMessageControlARB( GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_LOW_ARB, 0, NULL, true );
	}
#endif
	if( GL_Support( GL_TEXTURE_2D_RECT_EXT ))
		pglGetIntegerv( GL_MAX_RECTANGLE_TEXTURE_SIZE_EXT, &glConfig.max_2d_rectangle_size );

	Q_snprintf( value, sizeof( value ), "%i", glConfig.max_2d_texture_size );
	gEngfuncs.Cvar_Get( "gl_max_size", value, 0, "opengl texture max dims" );
	gEngfuncs.Cvar_SetValue( "gl_anisotropy", bound( 0, gl_texture_anisotropy.value, glConfig.max_texture_anisotropy ));

	if( GL_Support( GL_TEXTURE_COMPRESSION_EXT ))
		gEngfuncs.Image_AddCmdFlags( IL_DDS_HARDWARE );

	// MCD has buffering issues
#if XASH_WIN32
	if( Q_strstr( glConfig.renderer_string, "gdi" ))
		gEngfuncs.Cvar_SetValue( "gl_finish", 1 );
#endif

	R_RenderInfo_f();

	tr.framecount = tr.visframecount = 1;
	glw_state.initialized = true;
}

void GL_ClearExtensions( void )
{
	// now all extensions are disabled
	memset( glConfig.extension, 0, sizeof( glConfig.extension ));
	glw_state.initialized = false;
#if XASH_PSVITA
	// deinit our immediate mode override
	VGL_ShimShutdown();
#endif
}

//=======================================================================

/*
=================
GL_InitCommands
=================
*/
static void GL_InitCommands( void )
{
	RETRIEVE_ENGINE_SHARED_CVAR_LIST();

	gEngfuncs.Cvar_RegisterVariable( &r_lighting_extended );
	gEngfuncs.Cvar_RegisterVariable( &r_lighting_ambient );
	gEngfuncs.Cvar_RegisterVariable( &r_novis );
	gEngfuncs.Cvar_RegisterVariable( &r_nocull );
	gEngfuncs.Cvar_RegisterVariable( &r_detailtextures );
	gEngfuncs.Cvar_RegisterVariable( &r_lockpvs );
	gEngfuncs.Cvar_RegisterVariable( &r_lockfrustum );
	gEngfuncs.Cvar_RegisterVariable( &r_traceglow );
	gEngfuncs.Cvar_RegisterVariable( &r_studio_sort_textures );
	gEngfuncs.Cvar_RegisterVariable( &r_studio_drawelements );
	gEngfuncs.Cvar_RegisterVariable( &r_ripple );
	gEngfuncs.Cvar_RegisterVariable( &r_ripple_updatetime );
	gEngfuncs.Cvar_RegisterVariable( &r_ripple_spawntime );
	gEngfuncs.Cvar_RegisterVariable( &r_shadows );
	gEngfuncs.Cvar_RegisterVariable( &r_vbo );
	gEngfuncs.Cvar_RegisterVariable( &r_vbo_dlightmode );
	gEngfuncs.Cvar_RegisterVariable( &r_vbo_overbrightmode );
	gEngfuncs.Cvar_RegisterVariable( &r_vbo_detail );
	gEngfuncs.Cvar_RegisterVariable( &r_large_lightmaps );
	gEngfuncs.Cvar_RegisterVariable( &r_dlight_virtual_radius );

	gEngfuncs.Cvar_RegisterVariable( &gl_extensions );
	gEngfuncs.Cvar_RegisterVariable( &gl_texture_nearest );
	gEngfuncs.Cvar_RegisterVariable( &gl_lightmap_nearest );
	gEngfuncs.Cvar_RegisterVariable( &gl_check_errors );
	gEngfuncs.Cvar_RegisterVariable( &gl_texture_anisotropy );
	gEngfuncs.Cvar_RegisterVariable( &gl_texture_lodbias );
	gEngfuncs.Cvar_RegisterVariable( &gl_keeptjunctions );
	gEngfuncs.Cvar_RegisterVariable( &gl_finish );
	gEngfuncs.Cvar_RegisterVariable( &gl_nosort );
	gEngfuncs.Cvar_RegisterVariable( &gl_test );
	gEngfuncs.Cvar_RegisterVariable( &gl_wireframe );
	gEngfuncs.Cvar_RegisterVariable( &gl_msaa );
	gEngfuncs.Cvar_RegisterVariable( &gl_stencilbits );
	gEngfuncs.Cvar_RegisterVariable( &gl_round_down );
	gEngfuncs.Cvar_RegisterVariable( &gl_overbright );
	gEngfuncs.Cvar_RegisterVariable( &gl_fog );

	// these cvar not used by engine but some mods requires this
	gEngfuncs.Cvar_RegisterVariable( &gl_polyoffset );

	// make sure gl_vsync is checked after vid_restart
	SetBits( gl_vsync->flags, FCVAR_CHANGED );

	gEngfuncs.Cmd_AddCommand( "r_info", R_RenderInfo_f, "display renderer info" );
	gEngfuncs.Cmd_AddCommand( "timerefresh", SCR_TimeRefresh_f, "turn quickly and print rendering statistcs" );
}

/*
===============
R_CheckVBO

register VBO cvars and get default value
===============
*/
static void R_CheckVBO( void )
{
	qboolean disable = false;
	int flags = 0;

	// some bad GLES1 implementations breaks dlights completely
	if( glConfig.max_texture_units < 3 )
		disable = true;

#if XASH_MOBILE_PLATFORM
	// VideoCore4 drivers have a problem with mixing VBO and client arrays
	// Disable it, as there is no suitable workaround here
	if( Q_stristr( glConfig.renderer_string, "VideoCore IV" ) || Q_stristr( glConfig.renderer_string, "vc4" ) )
		disable = true;
#endif

	// we do not want to write vbo code that does not use multitexture
	if( !GL_Support( GL_ARB_VERTEX_BUFFER_OBJECT_EXT ) || !GL_Support( GL_ARB_MULTITEXTURE ) || glConfig.max_texture_units < 2 )
	{
		flags = FCVAR_READ_ONLY;
		disable = true;
	}

	if( disable )
	{
		gEngfuncs.Cvar_FullSet( r_vbo.name, "0", flags );
		gEngfuncs.Cvar_FullSet( r_vbo_dlightmode.name, "0", flags );
	}
}

/*
=================
GL_RemoveCommands
=================
*/
static void GL_RemoveCommands( void )
{
	gEngfuncs.Cmd_RemoveCommand( "r_info" );
	gEngfuncs.Cmd_RemoveCommand( "timerefresh" );
}

/*
===============
R_Init
===============
*/
qboolean R_Init( void )
{
	if( glw_state.initialized )
		return true;

	GL_InitCommands();
	GL_InitRandomTable();

	GL_SetDefaultState();

	r_temppool = Mem_AllocPool( "Render Zone" );

	// create the window and set up the context
	if( !gEngfuncs.R_Init_Video( REF_GL )) // request GL context
	{
		GL_RemoveCommands();
		gEngfuncs.R_Free_Video();
// Why? Host_Error again???
//		gEngfuncs.Host_Error( "Can't initialize video subsystem\nProbably driver was not installed" );
		Mem_FreePool( &r_temppool );
		return false;
	}

	// see R_ProcessEntData for tr.entities initialization
	tr.world = (struct world_static_s *)ENGINE_GET_PARM( PARM_GET_WORLD_PTR );
	tr.movevars = (movevars_t *)ENGINE_GET_PARM( PARM_GET_MOVEVARS_PTR );
	tr.palette = (color24 *)ENGINE_GET_PARM( PARM_GET_PALETTE_PTR );
	tr.viewent = (cl_entity_t *)ENGINE_GET_PARM( PARM_GET_VIEWENT_PTR );
	tr.texgammatable = (byte *)ENGINE_GET_PARM( PARM_GET_TEXGAMMATABLE_PTR );
	tr.lightgammatable = (uint *)ENGINE_GET_PARM( PARM_GET_LIGHTGAMMATABLE_PTR );
	tr.screengammatable = (uint *)ENGINE_GET_PARM( PARM_GET_SCREENGAMMATABLE_PTR );
	tr.lineargammatable = (uint *)ENGINE_GET_PARM( PARM_GET_LINEARGAMMATABLE_PTR );
	tr.dlights = (dlight_t *)ENGINE_GET_PARM( PARM_GET_DLIGHTS_PTR );
	tr.elights = (dlight_t *)ENGINE_GET_PARM( PARM_GET_ELIGHTS_PTR );

	GL_SetDefaults();
	R_CheckVBO();
	R_InitImages();
	R_SpriteInit();
	R_StudioInit();
	R_AliasInit();
	R_ClearDecals();
	R_ClearScene();

	return true;
}

/*
===============
R_Shutdown
===============
*/
void R_Shutdown( void )
{
	if( !glw_state.initialized )
		return;

	GL_RemoveCommands();
	R_ShutdownImages();
#if !XASH_GLES && !XASH_GL_STATIC
	GL2_ShimShutdown();
#endif

	Mem_FreePool( &r_temppool );

#if XASH_GL4ES
	close_gl4es();
#endif // XASH_GL4ES

	// shut down OS specific OpenGL stuff like contexts, etc.
	gEngfuncs.R_Free_Video();
}

/*
=================
GL_ErrorString
convert errorcode to string
=================
*/
const char *GL_ErrorString( int err )
{
	switch( err )
	{
	case GL_STACK_OVERFLOW:
		return "GL_STACK_OVERFLOW";
	case GL_STACK_UNDERFLOW:
		return "GL_STACK_UNDERFLOW";
	case GL_INVALID_ENUM:
		return "GL_INVALID_ENUM";
	case GL_INVALID_VALUE:
		return "GL_INVALID_VALUE";
	case GL_INVALID_OPERATION:
		return "GL_INVALID_OPERATION";
	case GL_OUT_OF_MEMORY:
		return "GL_OUT_OF_MEMORY";
	default:
		return "UNKNOWN ERROR";
	}
}

/*
=================
GL_CheckForErrors
obsolete
=================
*/
void GL_CheckForErrors_( const char *filename, const int fileline )
{
	int	err;

	if( !gl_check_errors.value )
		return;

	if(( err = pglGetError( )) == GL_NO_ERROR )
		return;

	gEngfuncs.Con_Printf( S_OPENGL_ERROR "%s (at %s:%i)\n", GL_ErrorString( err ), filename, fileline );
}

void GL_SetupAttributes( int safegl )
{
	int context_flags = 0; // REFTODO!!!!!
	int samples = 0;

#if XASH_GLES
	gEngfuncs.GL_SetAttribute( REF_GL_CONTEXT_PROFILE_MASK, REF_GL_CONTEXT_PROFILE_ES );
	gEngfuncs.GL_SetAttribute( REF_GL_CONTEXT_EGL, 1 );
#if XASH_NANOGL
	gEngfuncs.GL_SetAttribute( REF_GL_CONTEXT_MAJOR_VERSION, 1 );
	gEngfuncs.GL_SetAttribute( REF_GL_CONTEXT_MINOR_VERSION, 1 );
#else // !XASH_NANOGL
	gEngfuncs.GL_SetAttribute( REF_GL_CONTEXT_MAJOR_VERSION, 2 );
	gEngfuncs.GL_SetAttribute( REF_GL_CONTEXT_MINOR_VERSION, 0 );
#endif

#elif XASH_GL4ES
	gEngfuncs.GL_SetAttribute( REF_GL_CONTEXT_PROFILE_MASK, REF_GL_CONTEXT_PROFILE_ES );
	gEngfuncs.GL_SetAttribute( REF_GL_CONTEXT_EGL, 1 );
	gEngfuncs.GL_SetAttribute( REF_GL_CONTEXT_MAJOR_VERSION, 2 );
	gEngfuncs.GL_SetAttribute( REF_GL_CONTEXT_MINOR_VERSION, 0 );
#else // GL1.x
	if( gEngfuncs.Sys_CheckParm( "-glcore" ))
	{
		SetBits( context_flags, FCONTEXT_CORE_PROFILE );

		gEngfuncs.GL_SetAttribute( REF_GL_CONTEXT_PROFILE_MASK, REF_GL_CONTEXT_PROFILE_CORE );
		gEngfuncs.GL_SetAttribute( REF_GL_CONTEXT_MAJOR_VERSION, 3 );
		gEngfuncs.GL_SetAttribute( REF_GL_CONTEXT_MINOR_VERSION, 3 );
	}
	else
	{
		if( !safegl )
			gEngfuncs.GL_SetAttribute( REF_GL_CONTEXT_PROFILE_MASK, REF_GL_CONTEXT_PROFILE_COMPATIBILITY );
		else
		{
			gEngfuncs.GL_SetAttribute( REF_GL_CONTEXT_MAJOR_VERSION, 1 );
			gEngfuncs.GL_SetAttribute( REF_GL_CONTEXT_MINOR_VERSION, 1 );
		}
	}
#endif // XASH_GLES

	if( gEngfuncs.Sys_CheckParm( "-gldebug" ))
	{
		gEngfuncs.Con_Reportf( "Creating an extended GL context for debug...\n" );
		SetBits( context_flags, FCONTEXT_DEBUG_ARB );
		gEngfuncs.GL_SetAttribute( REF_GL_CONTEXT_FLAGS, REF_GL_CONTEXT_DEBUG_FLAG );
		glw_state.extended = true;
	}

	if( safegl > SAFE_DONTCARE )
	{
		safegl = -1; // can't retry anymore, can only shutdown engine
		return;
	}

	gEngfuncs.Con_Printf( "Trying safe opengl mode %d\n", safegl );

	if( safegl == SAFE_DONTCARE )
		return;

	gEngfuncs.GL_SetAttribute( REF_GL_DOUBLEBUFFER, 1 );

	if( safegl < SAFE_NOACC )
		gEngfuncs.GL_SetAttribute( REF_GL_ACCELERATED_VISUAL, 1 );

	gEngfuncs.Con_Printf( "bpp %d\n", gpGlobals->desktopBitsPixel );

	if( safegl < SAFE_NOSTENCIL )
		gEngfuncs.GL_SetAttribute( REF_GL_STENCIL_SIZE, gl_stencilbits.value );

	if( safegl < SAFE_NOALPHA )
		gEngfuncs.GL_SetAttribute( REF_GL_ALPHA_SIZE, 8 );

	if( safegl < SAFE_NODEPTH )
		gEngfuncs.GL_SetAttribute( REF_GL_DEPTH_SIZE, 24 );
	else
		gEngfuncs.GL_SetAttribute( REF_GL_DEPTH_SIZE, 8 );

	if( safegl < SAFE_NOCOLOR )
	{
		if( gpGlobals->desktopBitsPixel >= 24 )
		{
			gEngfuncs.GL_SetAttribute( REF_GL_RED_SIZE, 8 );
			gEngfuncs.GL_SetAttribute( REF_GL_GREEN_SIZE, 8 );
			gEngfuncs.GL_SetAttribute( REF_GL_BLUE_SIZE, 8 );
		}
		else if( gpGlobals->desktopBitsPixel >= 16 )
		{
			gEngfuncs.GL_SetAttribute( REF_GL_RED_SIZE, 5 );
			gEngfuncs.GL_SetAttribute( REF_GL_GREEN_SIZE, 6 );
			gEngfuncs.GL_SetAttribute( REF_GL_BLUE_SIZE, 5 );
		}
		else
		{
			gEngfuncs.GL_SetAttribute( REF_GL_RED_SIZE, 3 );
			gEngfuncs.GL_SetAttribute( REF_GL_GREEN_SIZE, 3 );
			gEngfuncs.GL_SetAttribute( REF_GL_BLUE_SIZE, 2 );
		}
	}

	if( safegl < SAFE_NOMSAA )
	{
		switch( (int)gEngfuncs.pfnGetCvarFloat( "gl_msaa_samples" ))
		{
		case 2:
		case 4:
		case 8:
		case 16:
			samples = gEngfuncs.pfnGetCvarFloat( "gl_msaa_samples" );
			break;
		default:
			samples = 0; // don't use, because invalid parameter is passed
		}

		if( samples )
		{
			gEngfuncs.GL_SetAttribute( REF_GL_MULTISAMPLEBUFFERS, 1 );
			gEngfuncs.GL_SetAttribute( REF_GL_MULTISAMPLESAMPLES, samples );

			glConfig.max_multisamples = samples;
		}
		else
		{
			gEngfuncs.GL_SetAttribute( REF_GL_MULTISAMPLEBUFFERS, 0 );
			gEngfuncs.GL_SetAttribute( REF_GL_MULTISAMPLESAMPLES, 0 );

			glConfig.max_multisamples = 0;
		}
	}
	else
	{
		gEngfuncs.Cvar_Set( "gl_msaa_samples", "0" );
	}
}

void wes_init( const char *gles2 );
int nanoGL_Init( void );
#if XASH_GL4ES
static void APIENTRY GL4ES_GetMainFBSize( int *width, int *height )
{
	*width = gpGlobals->width;
	*height = gpGlobals->height;
}

static void * APIENTRY GL4ES_GetProcAddress( const char *name )
{
	if( !Q_strcmp(name, "glShadeModel") )
		// combined gles/gles2/gl implementation exports this, but it is invalid
		return NULL;
	return gEngfuncs.GL_GetProcAddress( name );
}
#endif // XASH_GL4ES

void GL_OnContextCreated( void )
{
	int colorBits[3];
#if XASH_NANOGL
	nanoGL_Init();
#endif

	gEngfuncs.GL_GetAttribute( REF_GL_RED_SIZE, &colorBits[0] );
	gEngfuncs.GL_GetAttribute( REF_GL_GREEN_SIZE, &colorBits[1] );
	gEngfuncs.GL_GetAttribute( REF_GL_BLUE_SIZE, &colorBits[2] );
	glConfig.color_bits = colorBits[0] + colorBits[1] + colorBits[2];

	gEngfuncs.GL_GetAttribute( REF_GL_ALPHA_SIZE, &glConfig.alpha_bits );
	gEngfuncs.GL_GetAttribute( REF_GL_DEPTH_SIZE, &glConfig.depth_bits );
	gEngfuncs.GL_GetAttribute( REF_GL_STENCIL_SIZE, &glConfig.stencil_bits );
	glState.stencilEnabled = glConfig.stencil_bits ? true : false;

	gEngfuncs.GL_GetAttribute( REF_GL_MULTISAMPLESAMPLES, &glConfig.msaasamples );
	gEngfuncs.GL_GetAttribute( REF_GL_CONTEXT_MAJOR_VERSION, &glConfig.version_major );
	gEngfuncs.GL_GetAttribute( REF_GL_CONTEXT_MINOR_VERSION, &glConfig.version_minor );

#if XASH_WES
	wes_init( "" );
#endif // XASH_WES

#if XASH_GL4ES
	set_getprocaddress( GL4ES_GetProcAddress );
	set_getmainfbsize( GL4ES_GetMainFBSize );
	initialize_gl4es();

	// merge glBegin/glEnd in beams and console
	pglHint( GL_BEGINEND_HINT_GL4ES, 1 );
	// dxt unpacked to 16-bit looks ugly
	pglHint( GL_AVOID16BITS_HINT_GL4ES, 1 );
#endif // XASH_GL4ES
}
