
#include "gl_local.h"

cvar_t	*gl_extensions;
cvar_t	*gl_texture_anisotropy;
cvar_t	*gl_texture_lodbias;
cvar_t	*gl_texture_nearest;
cvar_t	*gl_lightmap_nearest;
cvar_t	*gl_keeptjunctions;
cvar_t	*gl_emboss_scale;
cvar_t	*gl_detailscale;
cvar_t	*gl_check_errors;
cvar_t	*gl_polyoffset;
cvar_t	*gl_wireframe;
cvar_t	*gl_finish;
cvar_t	*gl_nosort;
cvar_t	*gl_vsync;
cvar_t	*gl_clear;
cvar_t	*gl_test;
cvar_t	*gl_msaa;
cvar_t	*gl_stencilbits;
cvar_t	*r_speeds;
cvar_t	*r_fullbright;
cvar_t	*r_norefresh;
cvar_t	*r_lighting_extended;
cvar_t	*r_lighting_modulate;
cvar_t	*r_lighting_ambient;
cvar_t	*r_detailtextures;
cvar_t	*r_drawentities;
cvar_t	*r_adjust_fov;
cvar_t	*r_decals;
cvar_t	*r_novis;
cvar_t	*r_nocull;
cvar_t	*r_lockpvs;
cvar_t	*r_lockfrustum;
cvar_t	*r_traceglow;
cvar_t	*r_dynamic;
cvar_t	*r_lightmap;
cvar_t	*r_showhull;
cvar_t	*gl_round_down;
cvar_t	*r_vbo;
cvar_t	*r_vbo_dlightmode;
cvar_t	*gl_showtextures;
cvar_t	*cl_lightstyle_lerping;

cvar_t	*vid_brightness;
cvar_t	*vid_gamma;
cvar_t	*tracerred;
cvar_t	*tracergreen;
cvar_t	*tracerblue;
cvar_t	*traceralpha;

byte		*r_temppool;

gl_globals_t	tr;
glconfig_t	glConfig;
glstate_t	glState;
glwstate_t	glw_state;

#ifdef XASH_GL_STATIC
#define GL_CALL( x ) #x, NULL
#else
#define GL_CALL( x ) #x, (void**)&p##x
#endif
static dllfunc_t opengl_110funcs[] =
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
	{ NULL					, NULL }
};

static dllfunc_t debugoutputfuncs[] =
{
	{ GL_CALL( glDebugMessageControlARB ) },
	{ GL_CALL( glDebugMessageInsertARB ) },
	{ GL_CALL( glDebugMessageCallbackARB ) },
	{ GL_CALL( glGetDebugMessageLogARB ) },
	{ NULL					, NULL }
};

static dllfunc_t multitexturefuncs[] =
{
	{ GL_CALL( glMultiTexCoord1f ) },
	{ GL_CALL( glMultiTexCoord2f ) },
	{ GL_CALL( glMultiTexCoord3f ) },
	{ GL_CALL( glMultiTexCoord4f ) },
	{ GL_CALL( glActiveTexture ) },
	{ GL_CALL( glActiveTextureARB ) },
	{ GL_CALL( glClientActiveTexture ) },
	{ GL_CALL( glClientActiveTextureARB ) },
	{ NULL					, NULL }
};

static dllfunc_t texture3dextfuncs[] =
{
	{ GL_CALL( glTexImage3D ) },
	{ GL_CALL( glTexSubImage3D ) },
	{ GL_CALL( glCopyTexSubImage3D ) },
	{ NULL					, NULL }
};

static dllfunc_t texturecompressionfuncs[] =
{
	{ GL_CALL( glCompressedTexImage3DARB ) },
	{ GL_CALL( glCompressedTexImage2DARB ) },
	{ GL_CALL( glCompressedTexImage1DARB ) },
	{ GL_CALL( glCompressedTexSubImage3DARB ) },
	{ GL_CALL( glCompressedTexSubImage2DARB ) },
	{ GL_CALL( glCompressedTexSubImage1DARB ) },
	{ GL_CALL( glGetCompressedTexImage ) },
	{ NULL					, NULL }
};

static dllfunc_t vbofuncs[] =
{
	{ GL_CALL( glBindBufferARB ) },
	{ GL_CALL( glDeleteBuffersARB ) },
	{ GL_CALL( glGenBuffersARB ) },
	{ GL_CALL( glIsBufferARB ) },
	{ GL_CALL( glMapBufferARB ) },
	{ GL_CALL( glUnmapBufferARB ) }, // ,
	{ GL_CALL( glBufferDataARB ) },
	{ GL_CALL( glBufferSubDataARB ) },
	{ NULL, NULL }
};

static dllfunc_t drawrangeelementsfuncs[] =
{
{ GL_CALL( glDrawRangeElements ) },
{ NULL, NULL }
};

static dllfunc_t drawrangeelementsextfuncs[] =
{
{ GL_CALL( glDrawRangeElementsEXT ) },
{ NULL, NULL }
};

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
		gEngfuncs.Con_Printf( S_OPENGL_WARN "%s\n", message );
		break;
	case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_ARB:
		gEngfuncs.Con_Printf( S_OPENGL_WARN "%s\n", message );
		break;
	case GL_DEBUG_TYPE_PORTABILITY_ARB:
		gEngfuncs.Con_Reportf( S_OPENGL_WARN "%s\n", message );
		break;
	case GL_DEBUG_TYPE_PERFORMANCE_ARB:
		gEngfuncs.Con_Printf( S_OPENGL_NOTE "%s\n", message );
		break;
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
void GL_SetExtension( int r_ext, int enable )
{
	if( r_ext >= 0 && r_ext < GL_EXTCOUNT )
		glConfig.extension[r_ext] = enable ? GL_TRUE : GL_FALSE;
	else gEngfuncs.Con_Printf( S_ERROR "GL_SetExtension: invalid extension %d\n", r_ext );
}

/*
=================
GL_Support
=================
*/
qboolean GL_Support( int r_ext )
{
	if( r_ext >= 0 && r_ext < GL_EXTCOUNT )
		return glConfig.extension[r_ext] ? true : false;
	gEngfuncs.Con_Printf( S_ERROR "GL_Support: invalid extension %d\n", r_ext );

	return false;
}

/*
=================
GL_MaxTextureUnits
=================
*/
int GL_MaxTextureUnits( void )
{
	if( GL_Support( GL_SHADER_GLSL100_EXT ))
		return Q_min( Q_max( glConfig.max_texture_coords, glConfig.max_teximage_units ), MAX_TEXTURE_UNITS );
	return glConfig.max_texture_units;
}

/*
=================
GL_CheckExtension
=================
*/
qboolean GL_CheckExtension( const char *name, const dllfunc_t *funcs, const char *cvarname, int r_ext )
{
	const dllfunc_t	*func;
	cvar_t		*parm = NULL;
	const char	*extensions_string;

	gEngfuncs.Con_Reportf( "GL_CheckExtension: %s ", name );
	GL_SetExtension( r_ext, true );

	if( cvarname )
	{
		// system config disable extensions
		parm = gEngfuncs.Cvar_Get( cvarname, "1", FCVAR_GLCONFIG, va( CVAR_GLCONFIG_DESCRIPTION, name ));
	}

	if(( parm && !CVAR_TO_BOOL( parm )) || ( !CVAR_TO_BOOL( gl_extensions ) && r_ext != GL_OPENGL_110 ))
	{
		gEngfuncs.Con_Reportf( "- disabled\n" );
		GL_SetExtension( r_ext, false );
		return false; // nothing to process at
	}

	extensions_string = glConfig.extensions_string;

	if(( name[2] == '_' || name[3] == '_' ) && !Q_strstr( extensions_string, name ))
	{
		GL_SetExtension( r_ext, false );	// update render info
		gEngfuncs.Con_Reportf( "- ^1failed\n" );
		return false;
	}

#ifndef XASH_GL_STATIC
	// clear exports
	for( func = funcs; func && func->name; func++ )
		*func->func = NULL;

	for( func = funcs; func && func->name; func++ )
	{
		// functions are cleared before all the extensions are evaluated
		if((*func->func = (void *)gEngfuncs.GL_GetProcAddress( func->name )) == NULL )
			GL_SetExtension( r_ext, false ); // one or more functions are invalid, extension will be disabled
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
void R_RenderInfo_f( void )
{
	gEngfuncs.Con_Printf( "\n" );
	gEngfuncs.Con_Printf( "GL_VENDOR: %s\n", glConfig.vendor_string );
	gEngfuncs.Con_Printf( "GL_RENDERER: %s\n", glConfig.renderer_string );
	gEngfuncs.Con_Printf( "GL_VERSION: %s\n", glConfig.version_string );

	// don't spam about extensions
	gEngfuncs.Con_Reportf( "GL_EXTENSIONS: %s\n", glConfig.extensions_string );

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

#ifdef XASH_GLES
void GL_InitExtensionsGLES( void )
{
	int extid;

	// intialize wrapper type
#ifdef XASH_NANOGL
	glConfig.context = CONTEXT_TYPE_GLES_1_X;
	glConfig.wrapper = GLES_WRAPPER_NANOGL;
#elif defined( XASH_WES )
	glConfig.context = CONTEXT_TYPE_GLES_2_X;
	glConfig.wrapper = GLES_WRAPPER_WES;
#else
#error "unknown gles wrapper"
#endif

	glConfig.hardware_type = GLHW_GENERIC;

	for( extid = GL_OPENGL_110 + 1; extid < GL_EXTCOUNT; extid++ )
	{
		switch( extid )
		{
		case GL_ARB_VERTEX_BUFFER_OBJECT_EXT:
			GL_SetExtension( extid, true );
			break;
		case GL_ARB_MULTITEXTURE:
			GL_SetExtension( extid, true ); // required to be supported by wrapper

			pglGetIntegerv( GL_MAX_TEXTURE_UNITS_ARB, &glConfig.max_texture_units );
			if( glConfig.max_texture_units <= 1 )
			{
				GL_SetExtension( extid, false );
				glConfig.max_texture_units = 1;
			}

			glConfig.max_texture_coords = glConfig.max_teximage_units = glConfig.max_texture_units;
			break;
		case GL_TEXTURE_CUBEMAP_EXT:
			if( GL_CheckExtension( "GL_OES_texture_cube_map", NULL, "gl_texture_cubemap", extid ))
				pglGetIntegerv( GL_MAX_CUBE_MAP_TEXTURE_SIZE_ARB, &glConfig.max_cubemap_size );
			break;
		case GL_ANISOTROPY_EXT:
			glConfig.max_texture_anisotropy = 0.0f;
			if( GL_CheckExtension( "GL_EXT_texture_filter_anisotropic", NULL, "gl_ext_anisotropic_filter", extid ))
				pglGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &glConfig.max_texture_anisotropy );
			break;
		case GL_TEXTURE_LOD_BIAS:
			if( GL_CheckExtension( "GL_EXT_texture_lod_bias", NULL, "gl_texture_mipmap_biasing", extid ))
				pglGetFloatv( GL_MAX_TEXTURE_LOD_BIAS_EXT, &glConfig.max_texture_lod_bias );
			break;
		case GL_ARB_TEXTURE_NPOT_EXT:
			GL_CheckExtension( "GL_OES_texture_npot", NULL, "gl_texture_npot", extid );
			break;
		case GL_DEBUG_OUTPUT:
			if( glw_state.extended )
				GL_CheckExtension( "GL_KHR_debug", NULL, NULL, extid );
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
}
#else
void GL_InitExtensionsBigGL( void )
{
	// intialize wrapper type
	glConfig.context = CONTEXT_TYPE_GL;
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

	// multitexture
	glConfig.max_texture_units = glConfig.max_texture_coords = glConfig.max_teximage_units = 1;
	if( GL_CheckExtension( "GL_ARB_multitexture", multitexturefuncs, "gl_arb_multitexture", GL_ARB_MULTITEXTURE ))
	{
		pglGetIntegerv( GL_MAX_TEXTURE_UNITS_ARB, &glConfig.max_texture_units );
	}

	if( glConfig.max_texture_units == 1 )
		GL_SetExtension( GL_ARB_MULTITEXTURE, false );

	// 3d texture support
	if( GL_CheckExtension( "GL_EXT_texture3D", texture3dextfuncs, "gl_texture_3d", GL_TEXTURE_3D_EXT ))
	{
		pglGetIntegerv( GL_MAX_3D_TEXTURE_SIZE, &glConfig.max_3d_texture_size );

		if( glConfig.max_3d_texture_size < 32 )
		{
			GL_SetExtension( GL_TEXTURE_3D_EXT, false );
			gEngfuncs.Con_Printf( S_ERROR "GL_EXT_texture3D reported bogus GL_MAX_3D_TEXTURE_SIZE, disabled\n" );
		}
	}

	// 2d texture array support
	if( GL_CheckExtension( "GL_EXT_texture_array", texture3dextfuncs, "gl_texture_2d_array", GL_TEXTURE_ARRAY_EXT ))
		pglGetIntegerv( GL_MAX_ARRAY_TEXTURE_LAYERS_EXT, &glConfig.max_2d_texture_layers );

	// cubemaps support
	if( GL_CheckExtension( "GL_ARB_texture_cube_map", NULL, "gl_texture_cubemap", GL_TEXTURE_CUBEMAP_EXT ))
	{
		pglGetIntegerv( GL_MAX_CUBE_MAP_TEXTURE_SIZE_ARB, &glConfig.max_cubemap_size );

		// check for seamless cubemaps too
		GL_CheckExtension( "GL_ARB_seamless_cube_map", NULL, "gl_texture_cubemap_seamless", GL_ARB_SEAMLESS_CUBEMAP );
	}

	GL_CheckExtension( "GL_ARB_texture_non_power_of_two", NULL, "gl_texture_npot", GL_ARB_TEXTURE_NPOT_EXT );
	GL_CheckExtension( "GL_ARB_texture_compression", texturecompressionfuncs, "gl_texture_dxt_compression", GL_TEXTURE_COMPRESSION_EXT );
	if( !GL_CheckExtension( "GL_EXT_texture_edge_clamp", NULL, "gl_clamp_to_edge", GL_CLAMPTOEDGE_EXT ))
		GL_CheckExtension( "GL_SGIS_texture_edge_clamp", NULL, "gl_clamp_to_edge", GL_CLAMPTOEDGE_EXT );

	glConfig.max_texture_anisotropy = 0.0f;
	if( GL_CheckExtension( "GL_EXT_texture_filter_anisotropic", NULL, "gl_texture_anisotropic_filter", GL_ANISOTROPY_EXT ))
		pglGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &glConfig.max_texture_anisotropy );

#if XASH_WIN32 // Win32 only drivers?
	// g-cont. because lodbias it too glitchy on Intel's cards
	if( glConfig.hardware_type != GLHW_INTEL )
#endif
	{
		if( GL_CheckExtension( "GL_EXT_texture_lod_bias", NULL, "gl_texture_mipmap_biasing", GL_TEXTURE_LOD_BIAS ))
			pglGetFloatv( GL_MAX_TEXTURE_LOD_BIAS_EXT, &glConfig.max_texture_lod_bias );
	}

	GL_CheckExtension( "GL_ARB_texture_border_clamp", NULL, NULL, GL_CLAMP_TEXBORDER_EXT );

	GL_CheckExtension( "GL_ARB_depth_texture", NULL, NULL, GL_DEPTH_TEXTURE );
	GL_CheckExtension( "GL_ARB_texture_float", NULL, "gl_texture_float", GL_ARB_TEXTURE_FLOAT_EXT );
	GL_CheckExtension( "GL_ARB_depth_buffer_float", NULL, "gl_texture_depth_float", GL_ARB_DEPTH_FLOAT_EXT );
	GL_CheckExtension( "GL_EXT_gpu_shader4", NULL, NULL, GL_EXT_GPU_SHADER4 ); // don't confuse users
	GL_CheckExtension( "GL_ARB_vertex_buffer_object", vbofuncs, "gl_vertex_buffer_object", GL_ARB_VERTEX_BUFFER_OBJECT_EXT );
	if( GL_CheckExtension( "GL_ARB_shading_language_100", NULL, NULL, GL_SHADER_GLSL100_EXT ))
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
	GL_CheckExtension( "GL_ARB_texture_rectangle", NULL, "gl_texture_rectangle", GL_TEXTURE_2D_RECT_EXT );

	if( !GL_CheckExtension( "glDrawRangeElements", drawrangeelementsfuncs, "gl_drawrangeelements", GL_DRAW_RANGEELEMENTS_EXT ) )
	{
		if( GL_CheckExtension( "glDrawRangeElementsEXT", drawrangeelementsextfuncs,
			"gl_drawrangelements", GL_DRAW_RANGEELEMENTS_EXT ) )
		{
			pglDrawRangeElements = pglDrawRangeElementsEXT;
		}
	}

	// this won't work without extended context
	if( glw_state.extended )
		GL_CheckExtension( "GL_ARB_debug_output", debugoutputfuncs, "gl_debug_output", GL_DEBUG_OUTPUT );
}
#endif

void GL_InitExtensions( void )
{
	GL_OnContextCreated();

	// initialize gl extensions
	GL_CheckExtension( "OpenGL 1.1.0", opengl_110funcs, NULL, GL_OPENGL_110 );

	// get our various GL strings
	glConfig.vendor_string = (const char *)pglGetString( GL_VENDOR );
	glConfig.renderer_string = (const char *)pglGetString( GL_RENDERER );
	glConfig.version_string = (const char *)pglGetString( GL_VERSION );
	glConfig.extensions_string = (const char *)pglGetString( GL_EXTENSIONS );
	gEngfuncs.Con_Reportf( "^3Video^7: %s\n", glConfig.renderer_string );

#ifdef XASH_GLES
	GL_InitExtensionsGLES();
#else
	GL_InitExtensionsBigGL();
#endif

	pglGetIntegerv( GL_MAX_TEXTURE_SIZE, &glConfig.max_2d_texture_size );
	if( glConfig.max_2d_texture_size <= 0 ) glConfig.max_2d_texture_size = 256;

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

	if( GL_Support( GL_TEXTURE_2D_RECT_EXT ))
		pglGetIntegerv( GL_MAX_RECTANGLE_TEXTURE_SIZE_EXT, &glConfig.max_2d_rectangle_size );

	gEngfuncs.Cvar_Get( "gl_max_size", va( "%i", glConfig.max_2d_texture_size ), 0, "opengl texture max dims" );
	gEngfuncs.Cvar_Set( "gl_anisotropy", va( "%f", bound( 0, gl_texture_anisotropy->value, glConfig.max_texture_anisotropy )));

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
}

//=======================================================================

/*
=================
GL_InitCommands
=================
*/
void GL_InitCommands( void )
{
	r_speeds = gEngfuncs.Cvar_Get( "r_speeds", "0", FCVAR_ARCHIVE, "shows renderer speeds" );
	r_fullbright = gEngfuncs.Cvar_Get( "r_fullbright", "0", FCVAR_CHEAT, "disable lightmaps, get fullbright for entities" );
	r_norefresh = gEngfuncs.Cvar_Get( "r_norefresh", "0", 0, "disable 3D rendering (use with caution)" );
	r_lighting_extended = gEngfuncs.Cvar_Get( "r_lighting_extended", "1", FCVAR_ARCHIVE, "allow to get lighting from world and bmodels" );
	r_lighting_modulate = gEngfuncs.Cvar_Get( "r_lighting_modulate", "0.6", FCVAR_ARCHIVE, "lightstyles modulate scale" );
	r_lighting_ambient = gEngfuncs.Cvar_Get( "r_lighting_ambient", "0.3", FCVAR_ARCHIVE, "map ambient lighting scale" );
	r_novis = gEngfuncs.Cvar_Get( "r_novis", "0", 0, "ignore vis information (perfomance test)" );
	r_nocull = gEngfuncs.Cvar_Get( "r_nocull", "0", 0, "ignore frustrum culling (perfomance test)" );
	r_detailtextures = gEngfuncs.Cvar_Get( "r_detailtextures", "1", FCVAR_ARCHIVE, "enable detail textures support, use '2' for autogenerate detail.txt" );
	r_lockpvs = gEngfuncs.Cvar_Get( "r_lockpvs", "0", FCVAR_CHEAT, "lockpvs area at current point (pvs test)" );
	r_lockfrustum = gEngfuncs.Cvar_Get( "r_lockfrustum", "0", FCVAR_CHEAT, "lock frustrum area at current point (cull test)" );
	r_dynamic = gEngfuncs.Cvar_Get( "r_dynamic", "1", FCVAR_ARCHIVE, "allow dynamic lighting (dlights, lightstyles)" );
	r_traceglow = gEngfuncs.Cvar_Get( "r_traceglow", "1", FCVAR_ARCHIVE, "cull flares behind models" );
	r_lightmap = gEngfuncs.Cvar_Get( "r_lightmap", "0", FCVAR_CHEAT, "lightmap debugging tool" );
	r_drawentities = gEngfuncs.Cvar_Get( "r_drawentities", "1", FCVAR_CHEAT, "render entities" );
	r_decals = gEngfuncs.pfnGetCvarPointer( "r_decals", 0 );
	r_showhull = gEngfuncs.pfnGetCvarPointer( "r_showhull", 0 );

	gl_extensions = gEngfuncs.Cvar_Get( "gl_allow_extensions", "1", FCVAR_GLCONFIG, "allow gl_extensions" );
	gl_texture_nearest = gEngfuncs.Cvar_Get( "gl_texture_nearest", "0", FCVAR_ARCHIVE, "disable texture filter" );
	gl_lightmap_nearest = gEngfuncs.Cvar_Get( "gl_lightmap_nearest", "0", FCVAR_ARCHIVE, "disable lightmap filter" );
	gl_check_errors = gEngfuncs.Cvar_Get( "gl_check_errors", "1", FCVAR_ARCHIVE, "ignore video engine errors" );
	gl_vsync = gEngfuncs.pfnGetCvarPointer( "gl_vsync", 0 );
	gl_detailscale = gEngfuncs.Cvar_Get( "gl_detailscale", "4.0", FCVAR_ARCHIVE, "default scale applies while auto-generate list of detail textures" );
	gl_texture_anisotropy = gEngfuncs.Cvar_Get( "gl_anisotropy", "8", FCVAR_ARCHIVE, "textures anisotropic filter" );
	gl_texture_lodbias =  gEngfuncs.Cvar_Get( "gl_texture_lodbias", "0.0", FCVAR_ARCHIVE, "LOD bias for mipmapped textures (perfomance|quality)" );
	gl_keeptjunctions = gEngfuncs.Cvar_Get( "gl_keeptjunctions", "1", FCVAR_ARCHIVE, "removing tjuncs causes blinking pixels" );
	gl_emboss_scale = gEngfuncs.Cvar_Get( "gl_emboss_scale", "0", FCVAR_ARCHIVE|FCVAR_LATCH, "fake bumpmapping scale" );
	gl_showtextures = gEngfuncs.pfnGetCvarPointer( "r_showtextures", 0 );
	gl_finish = gEngfuncs.Cvar_Get( "gl_finish", "0", FCVAR_ARCHIVE, "use glFinish instead of glFlush" );
	gl_nosort = gEngfuncs.Cvar_Get( "gl_nosort", "0", FCVAR_ARCHIVE, "disable sorting of translucent surfaces" );
	gl_clear = gEngfuncs.pfnGetCvarPointer( "gl_clear", 0 );
	gl_test = gEngfuncs.Cvar_Get( "gl_test", "0", 0, "engine developer cvar for quick testing new features" );
	gl_wireframe = gEngfuncs.Cvar_Get( "gl_wireframe", "0", FCVAR_ARCHIVE|FCVAR_SPONLY, "show wireframe overlay" );
	gl_msaa = gEngfuncs.Cvar_Get( "gl_msaa", "1", FCVAR_ARCHIVE, "enable or disable multisample anti-aliasing" );
	gl_stencilbits = gEngfuncs.Cvar_Get( "gl_stencilbits", "8", FCVAR_GLCONFIG, "pixelformat stencil bits (0 - auto)" );
	gl_round_down = gEngfuncs.Cvar_Get( "gl_round_down", "2", FCVAR_GLCONFIG, "round texture sizes to nearest POT value" );
	// these cvar not used by engine but some mods requires this
	gl_polyoffset = gEngfuncs.Cvar_Get( "gl_polyoffset", "2.0", FCVAR_ARCHIVE, "polygon offset for decals" );

	// make sure gl_vsync is checked after vid_restart
	SetBits( gl_vsync->flags, FCVAR_CHANGED );

	vid_gamma = gEngfuncs.pfnGetCvarPointer( "gamma", 0 );
	vid_brightness = gEngfuncs.pfnGetCvarPointer( "brightness", 0 );

	tracerred = gEngfuncs.Cvar_Get( "tracerred", "0.8", 0, "tracer red component weight ( 0 - 1.0 )" );
	tracergreen = gEngfuncs.Cvar_Get( "tracergreen", "0.8", 0, "tracer green component weight ( 0 - 1.0 )" );
	tracerblue = gEngfuncs.Cvar_Get( "tracerblue", "0.4", 0, "tracer blue component weight ( 0 - 1.0 )" );
	traceralpha = gEngfuncs.Cvar_Get( "traceralpha", "0.5", 0, "tracer alpha amount ( 0 - 1.0 )" );

	cl_lightstyle_lerping = gEngfuncs.pfnGetCvarPointer( "cl_lightstyle_lerping", 0 );

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
	const char *def = "1";
	const char *dlightmode = "1";
	int flags = FCVAR_ARCHIVE;
	qboolean disable = false;

	// some bad GLES1 implementations breaks dlights completely
	if( glConfig.max_texture_units < 3 )
		disable = true;

#ifdef XASH_MOBILE_PLATFORM
	// VideoCore4 drivers have a problem with mixing VBO and client arrays
	// Disable it, as there is no suitable workaround here
	if( Q_stristr( glConfig.renderer_string, "VideoCore IV" ) || Q_stristr( glConfig.renderer_string, "vc4" ) )
		disable = true;

	// dlightmode 1 is not too much tested on android
	// so better to left it off
	dlightmode = "0";
#endif

	if( disable )
	{
		// do not keep in config unless dev > 3 and enabled
		flags = 0;
		def = "0";
	}

	r_vbo = gEngfuncs.Cvar_Get( "r_vbo", def, flags, "draw world using VBO" );
	r_vbo_dlightmode = gEngfuncs.Cvar_Get( "r_vbo_dlightmode", dlightmode, FCVAR_ARCHIVE, "vbo dlight rendering mode(0-1)" );

	// check if enabled manually
	if( CVAR_TO_BOOL(r_vbo) )
		r_vbo->flags |= FCVAR_ARCHIVE;
}

/*
=================
GL_RemoveCommands
=================
*/
void GL_RemoveCommands( void )
{
	gEngfuncs.Cmd_RemoveCommand( "r_info" );
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

	// create the window and set up the context
	if( !gEngfuncs.R_Init_Video( REF_GL )) // request GL context
	{
		GL_RemoveCommands();
		gEngfuncs.R_Free_Video();

		gEngfuncs.Host_Error( "Can't initialize video subsystem\nProbably driver was not installed" );
		return false;
	}

	r_temppool = Mem_AllocPool( "Render Zone" );

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

	Mem_FreePool( &r_temppool );

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

	if( !CVAR_TO_BOOL( gl_check_errors ))
		return;

	if(( err = pglGetError( )) == GL_NO_ERROR )
		return;

	// gEngfuncs.Con_Printf( S_OPENGL_ERROR "%s (called at %s:%i)\n", GL_ErrorString( err ), filename, fileline );
}

void GL_SetupAttributes( int safegl )
{
	int context_flags = 0; // REFTODO!!!!!
	int samples = 0;

#ifdef XASH_GLES
	gEngfuncs.GL_SetAttribute( REF_GL_CONTEXT_PROFILE_MASK, REF_GL_CONTEXT_PROFILE_ES );
	gEngfuncs.GL_SetAttribute( REF_GL_CONTEXT_EGL, 1 );
#ifdef XASH_NANOGL
	gEngfuncs.GL_SetAttribute( REF_GL_CONTEXT_MAJOR_VERSION, 1 );
	gEngfuncs.GL_SetAttribute( REF_GL_CONTEXT_MINOR_VERSION, 1 );
#elif defined( XASH_WES ) || defined( XASH_REGAL )
	gEngfuncs.GL_SetAttribute( REF_GL_CONTEXT_MAJOR_VERSION, 2 );
	gEngfuncs.GL_SetAttribute( REF_GL_CONTEXT_MINOR_VERSION, 0 );
#endif
#else // GL1.x
	if( gEngfuncs.Sys_CheckParm( "-glcore" ))
	{
		SetBits( context_flags, FCONTEXT_CORE_PROFILE );

		gEngfuncs.GL_SetAttribute( REF_GL_CONTEXT_PROFILE_MASK, REF_GL_CONTEXT_PROFILE_CORE );
	}
	else
	{
		gEngfuncs.GL_SetAttribute( REF_GL_CONTEXT_PROFILE_MASK, REF_GL_CONTEXT_PROFILE_COMPATIBILITY );
	}
#endif // XASH_GLES

	if( gEngfuncs.Sys_CheckParm( "-gldebug" ) )
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
		gEngfuncs.GL_SetAttribute( REF_GL_STENCIL_SIZE, gl_stencilbits->value );

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
		switch( (int)gEngfuncs.pfnGetCvarFloat( "gl_wgl_msaa_samples" ))
		{
		case 2:
		case 4:
		case 8:
		case 16:
			samples = gEngfuncs.pfnGetCvarFloat( "gl_wgl_msaa_samples" );
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
		gEngfuncs.Cvar_Set( "gl_wgl_msaa_samples", "0" );
	}
}

void wes_init( const char *gles2 );
int nanoGL_Init( void );

void GL_OnContextCreated( void )
{
	int colorBits[3];
#ifdef XASH_NANOGL
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

#ifdef XASH_WES
	wes_init( "" );
#endif
}

