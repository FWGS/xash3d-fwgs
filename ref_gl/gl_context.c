/*
vid_sdl.c - SDL vid component
Copyright (C) 2018 a1batross

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "gl_local.h"
#include "gl_export.h"

#define GL_CALL( x ) #x, (void**)&p##x
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
	{ NULL, NULL}
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
		Con_Printf( S_OPENGL_ERROR "%s\n", message );
		break;
	case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_ARB:
		Con_Printf( S_OPENGL_WARN "%s\n", message );
		break;
	case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_ARB:
		Con_Printf( S_OPENGL_WARN "%s\n", message );
		break;
	case GL_DEBUG_TYPE_PORTABILITY_ARB:
		if( host_developer.value < DEV_EXTENDED )
			return;
		Con_Printf( S_OPENGL_WARN "%s\n", message );
		break;
	case GL_DEBUG_TYPE_PERFORMANCE_ARB:
		Con_Printf( S_OPENGL_NOTE "%s\n", message );
		break;
	case GL_DEBUG_TYPE_OTHER_ARB:
	default:	Con_Printf( S_OPENGL_NOTE "%s\n", message );
		break;
	}
}

#ifdef XASH_GLES
void GL_InitExtensionsGLES( void )
{
	// intialize wrapper type
#ifdef XASH_NANOGL
	glConfig.context = CONTEXT_TYPE_GLES_1_X;
	glConfig.wrapper = GLES_WRAPPER_NANOGL;
#elif defined( XASH_WES )
	glConfig.context = CONTEXT_TYPE_GLES_2_X;
	glConfig.wrapper = GLES_WRAPPER_WES;
#endif

	glConfig.hardware_type = GLHW_GENERIC;

	// initalize until base opengl functions loaded
	GL_SetExtension( GL_DRAW_RANGEELEMENTS_EXT, true );
	GL_SetExtension( GL_ARB_MULTITEXTURE, true );
	pglGetIntegerv( GL_MAX_TEXTURE_UNITS_ARB, &glConfig.max_texture_units );
	glConfig.max_texture_coords = glConfig.max_texture_units = 4;

	GL_SetExtension( GL_ENV_COMBINE_EXT, true );
	GL_SetExtension( GL_DOT3_ARB_EXT, true );
	GL_SetExtension( GL_TEXTURE_3D_EXT, false );
	GL_SetExtension( GL_SGIS_MIPMAPS_EXT, true ); // gles specs
	GL_SetExtension( GL_ARB_VERTEX_BUFFER_OBJECT_EXT, true ); // gles specs

	// hardware cubemaps
	GL_CheckExtension( "GL_OES_texture_cube_map", NULL, "gl_texture_cubemap", GL_TEXTURECUBEMAP_EXT );

	if( GL_Support( GL_TEXTURECUBEMAP_EXT ))
		pglGetIntegerv( GL_MAX_CUBE_MAP_TEXTURE_SIZE_ARB, &glConfig.max_cubemap_size );

	GL_SetExtension( GL_ARB_SEAMLESS_CUBEMAP, false );

	GL_SetExtension( GL_EXT_POINTPARAMETERS, false );
	GL_CheckExtension( "GL_OES_texture_npot", NULL, "gl_texture_npot", GL_ARB_TEXTURE_NPOT_EXT );

	GL_SetExtension( GL_TEXTURE_COMPRESSION_EXT, false );
	GL_SetExtension( GL_CUSTOM_VERTEX_ARRAY_EXT, false );
	GL_SetExtension( GL_CLAMPTOEDGE_EXT, true ); // by gles1 specs
	GL_SetExtension( GL_ANISOTROPY_EXT, false );
	GL_SetExtension( GL_TEXTURE_LODBIAS, false );
	GL_SetExtension( GL_CLAMP_TEXBORDER_EXT, false );
	GL_SetExtension( GL_BLEND_MINMAX_EXT, false );
	GL_SetExtension( GL_BLEND_SUBTRACT_EXT, false );
	GL_SetExtension( GL_SEPARATESTENCIL_EXT, false );
	GL_SetExtension( GL_STENCILTWOSIDE_EXT, false );
	GL_SetExtension( GL_TEXTURE_ENV_ADD_EXT,false  );
	GL_SetExtension( GL_SHADER_OBJECTS_EXT, false );
	GL_SetExtension( GL_SHADER_GLSL100_EXT, false );
	GL_SetExtension( GL_VERTEX_SHADER_EXT,false );
	GL_SetExtension( GL_FRAGMENT_SHADER_EXT, false );
	GL_SetExtension( GL_SHADOW_EXT, false );
	GL_SetExtension( GL_ARB_DEPTH_FLOAT_EXT, false );
	GL_SetExtension( GL_OCCLUSION_QUERIES_EXT,false );
	GL_CheckExtension( "GL_OES_depth_texture", NULL, "gl_depthtexture", GL_DEPTH_TEXTURE );

	glConfig.texRectangle = glConfig.max_2d_rectangle_size = 0; // no rectangle

	Cvar_FullSet( "gl_allow_mirrors", "0", CVAR_READ_ONLY); // No support for GLES

}
#else
void GL_InitExtensionsBigGL()
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
	GL_CheckExtension( "GL_ARB_multitexture", multitexturefuncs, "gl_arb_multitexture", GL_ARB_MULTITEXTURE );

	if( GL_Support( GL_ARB_MULTITEXTURE ))
	{
		pglGetIntegerv( GL_MAX_TEXTURE_UNITS_ARB, &glConfig.max_texture_units );
	}

	if( glConfig.max_texture_units == 1 )
		GL_SetExtension( GL_ARB_MULTITEXTURE, false );

	// 3d texture support
	GL_CheckExtension( "GL_EXT_texture3D", texture3dextfuncs, "gl_texture_3d", GL_TEXTURE_3D_EXT );

	if( GL_Support( GL_TEXTURE_3D_EXT ))
	{
		pglGetIntegerv( GL_MAX_3D_TEXTURE_SIZE, &glConfig.max_3d_texture_size );

		if( glConfig.max_3d_texture_size < 32 )
		{
			GL_SetExtension( GL_TEXTURE_3D_EXT, false );
			Con_Printf( S_ERROR "GL_EXT_texture3D reported bogus GL_MAX_3D_TEXTURE_SIZE, disabled\n" );
		}
	}

	// 2d texture array support
	GL_CheckExtension( "GL_EXT_texture_array", texture3dextfuncs, "gl_texture_2d_array", GL_TEXTURE_ARRAY_EXT );

	if( GL_Support( GL_TEXTURE_ARRAY_EXT ))
		pglGetIntegerv( GL_MAX_ARRAY_TEXTURE_LAYERS_EXT, &glConfig.max_2d_texture_layers );

	// cubemaps support
	GL_CheckExtension( "GL_ARB_texture_cube_map", NULL, "gl_texture_cubemap", GL_TEXTURE_CUBEMAP_EXT );

	if( GL_Support( GL_TEXTURE_CUBEMAP_EXT ))
	{
		pglGetIntegerv( GL_MAX_CUBE_MAP_TEXTURE_SIZE_ARB, &glConfig.max_cubemap_size );

		// check for seamless cubemaps too
		GL_CheckExtension( "GL_ARB_seamless_cube_map", NULL, "gl_texture_cubemap_seamless", GL_ARB_SEAMLESS_CUBEMAP );
	}

	GL_CheckExtension( "GL_ARB_texture_non_power_of_two", NULL, "gl_texture_npot", GL_ARB_TEXTURE_NPOT_EXT );
	GL_CheckExtension( "GL_ARB_texture_compression", texturecompressionfuncs, "gl_dds_hardware_support", GL_TEXTURE_COMPRESSION_EXT );
	GL_CheckExtension( "GL_EXT_texture_edge_clamp", NULL, "gl_clamp_to_edge", GL_CLAMPTOEDGE_EXT );
	if( !GL_Support( GL_CLAMPTOEDGE_EXT ))
		GL_CheckExtension( "GL_SGIS_texture_edge_clamp", NULL, "gl_clamp_to_edge", GL_CLAMPTOEDGE_EXT );

	glConfig.max_texture_anisotropy = 0.0f;
	GL_CheckExtension( "GL_EXT_texture_filter_anisotropic", NULL, "gl_ext_anisotropic_filter", GL_ANISOTROPY_EXT );
	if( GL_Support( GL_ANISOTROPY_EXT ))
		pglGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &glConfig.max_texture_anisotropy );

#ifdef _WIN32 // Win32 only drivers?
	// g-cont. because lodbias it too glitchy on Intel's cards
	if( glConfig.hardware_type != GLHW_INTEL )
#endif
		GL_CheckExtension( "GL_EXT_texture_lod_bias", NULL, "gl_texture_mipmap_biasing", GL_TEXTURE_LOD_BIAS );

	if( GL_Support( GL_TEXTURE_LOD_BIAS ))
		pglGetFloatv( GL_MAX_TEXTURE_LOD_BIAS_EXT, &glConfig.max_texture_lod_bias );

	GL_CheckExtension( "GL_ARB_texture_border_clamp", NULL, "gl_ext_texborder_clamp", GL_CLAMP_TEXBORDER_EXT );
	GL_CheckExtension( "GL_ARB_depth_texture", NULL, "gl_depthtexture", GL_DEPTH_TEXTURE );
	GL_CheckExtension( "GL_ARB_texture_float", NULL, "gl_arb_texture_float", GL_ARB_TEXTURE_FLOAT_EXT );
	GL_CheckExtension( "GL_ARB_depth_buffer_float", NULL, "gl_arb_depth_float", GL_ARB_DEPTH_FLOAT_EXT );
	GL_CheckExtension( "GL_EXT_gpu_shader4", NULL, NULL, GL_EXT_GPU_SHADER4 ); // don't confuse users
	GL_CheckExtension( "GL_ARB_shading_language_100", NULL, "gl_glslprogram", GL_SHADER_GLSL100_EXT );
	GL_CheckExtension( "GL_ARB_vertex_buffer_object", vbofuncs, "gl_vertex_buffer_object", GL_ARB_VERTEX_BUFFER_OBJECT_EXT );

	// rectangle textures support
	GL_CheckExtension( "GL_ARB_texture_rectangle", NULL, "gl_texture_rectangle", GL_TEXTURE_2D_RECT_EXT );

	// this won't work without extended context
	if( glw_state.extended )
		GL_CheckExtension( "GL_ARB_debug_output", debugoutputfuncs, "gl_debug_output", GL_DEBUG_OUTPUT );

	if( GL_Support( GL_SHADER_GLSL100_EXT ))
	{
		pglGetIntegerv( GL_MAX_TEXTURE_COORDS_ARB, &glConfig.max_texture_coords );
		pglGetIntegerv( GL_MAX_TEXTURE_IMAGE_UNITS_ARB, &glConfig.max_teximage_units );

		// check for hardware skinning
		pglGetIntegerv( GL_MAX_VERTEX_UNIFORM_COMPONENTS_ARB, &glConfig.max_vertex_uniforms );
		pglGetIntegerv( GL_MAX_VERTEX_ATTRIBS_ARB, &glConfig.max_vertex_attribs );

#ifdef _WIN32 // Win32 only drivers?
		if( glConfig.hardware_type == GLHW_RADEON && glConfig.max_vertex_uniforms > 512 )
			glConfig.max_vertex_uniforms /= 4; // radeon returns not correct info
#endif
	}
	else
	{
		// just get from multitexturing
		glConfig.max_texture_coords = glConfig.max_teximage_units = glConfig.max_texture_units;
	}

	pglGetIntegerv( GL_MAX_TEXTURE_SIZE, &glConfig.max_2d_texture_size );
	if( glConfig.max_2d_texture_size <= 0 ) glConfig.max_2d_texture_size = 256;

	if( GL_Support( GL_TEXTURE_2D_RECT_EXT ))
		pglGetIntegerv( GL_MAX_RECTANGLE_TEXTURE_SIZE_EXT, &glConfig.max_2d_rectangle_size );

#ifndef XASH_GL_STATIC
	// enable gldebug if allowed
	if( GL_Support( GL_DEBUG_OUTPUT ))
	{
		if( host_developer.value )
		{
			Con_Reportf( "Installing GL_DebugOutput...\n");
			pglDebugMessageCallbackARB( GL_DebugOutput, NULL );

			// force everything to happen in the main thread instead of in a separate driver thread
			pglEnable( GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB );
		}

		// enable all the low priority messages
		pglDebugMessageControlARB( GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_LOW_ARB, 0, NULL, true );
	}
#endif
}
#endif

void GL_InitExtensions( void )
{
	// initialize gl extensions
	GL_CheckExtension( "OpenGL 1.1.0", opengl_110funcs, NULL, GL_OPENGL_110 );

	// get our various GL strings
	glConfig.vendor_string = pglGetString( GL_VENDOR );
	glConfig.renderer_string = pglGetString( GL_RENDERER );
	glConfig.version_string = pglGetString( GL_VERSION );
	glConfig.extensions_string = pglGetString( GL_EXTENSIONS );
	Con_Reportf( "^3Video^7: %s\n", glConfig.renderer_string );

#ifdef XASH_GLES
	GL_InitExtensionsGLES();
#else
	GL_InitExtensionsBigGL();
#endif

	if( GL_Support( GL_TEXTURE_2D_RECT_EXT ))
		pglGetIntegerv( GL_MAX_RECTANGLE_TEXTURE_SIZE_EXT, &glConfig.max_2d_rectangle_size );

	Cvar_Get( "gl_max_size", va( "%i", glConfig.max_2d_texture_size ), 0, "opengl texture max dims" );
	Cvar_Set( "gl_anisotropy", va( "%f", bound( 0, gl_texture_anisotropy->value, glConfig.max_texture_anisotropy )));

	if( GL_Support( GL_TEXTURE_COMPRESSION_EXT ))
		Image_AddCmdFlags( IL_DDS_HARDWARE );

	// MCD has buffering issues
#ifdef _WIN32
	if( Q_strstr( glConfig.renderer_string, "gdi" ))
		Cvar_SetValue( "gl_finish", 1 );
#endif

	tr.framecount = tr.visframecount = 1;
	glw_state.initialized = true;
}

void GL_ClearExtensions( void )
{
	// now all extensions are disabled
	memset( glConfig.extension, 0, sizeof( glConfig.extension ));
	glw_state.initialized = false;
}

