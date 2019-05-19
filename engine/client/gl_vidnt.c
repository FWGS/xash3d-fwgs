/*
gl_vidnt.c - NT GL vid component
Copyright (C) 2010 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "common.h"
#include "client.h"
#include "gl_local.h"
#include "mod_local.h"
#include "input.h"

#define VID_AUTOMODE		"-1"
#define VID_DEFAULTMODE		3.0f
#define DISP_CHANGE_BADDUALVIEW	-6 // MSVC 6.0 doesn't
#define num_vidmodes		ARRAYSIZE( vidmode )
#define WINDOW_STYLE		(WS_OVERLAPPED|WS_BORDER|WS_SYSMENU|WS_CAPTION|WS_VISIBLE)
#define WINDOW_EX_STYLE		(0)
#define WINDOW_NAME			"Xash3D Window" // Half-Life
#define FCONTEXT_DEBUG_ARB		BIT( 0 )

convar_t	*gl_extensions;
convar_t	*gl_texture_anisotropy;
convar_t	*gl_texture_lodbias;
convar_t	*gl_texture_nearest;
convar_t	*gl_lightmap_nearest;
convar_t	*gl_wgl_msaa_samples;
convar_t	*gl_keeptjunctions;
convar_t	*gl_emboss_scale;
convar_t	*gl_showtextures;
convar_t	*gl_detailscale;
convar_t	*gl_check_errors;
convar_t	*gl_round_down;
convar_t	*gl_polyoffset;
convar_t	*gl_wireframe;
convar_t	*gl_finish;
convar_t	*gl_nosort;
convar_t	*gl_vsync;
convar_t	*gl_clear;
convar_t	*gl_test;
convar_t	*gl_msaa;

convar_t	*window_xpos;
convar_t	*window_ypos;
convar_t	*r_speeds;
convar_t	*r_fullbright;
convar_t	*r_norefresh;
convar_t	*r_lighting_extended;
convar_t	*r_lighting_modulate;
convar_t	*r_lighting_ambient;
convar_t	*r_detailtextures;
convar_t	*r_drawentities;
convar_t	*r_adjust_fov;
convar_t	*r_showtree;
convar_t	*r_decals;
convar_t	*r_novis;
convar_t	*r_nocull;
convar_t	*r_lockpvs;
convar_t	*r_lockfrustum;
convar_t	*r_traceglow;
convar_t	*r_dynamic;
convar_t	*r_lightmap;

convar_t	*vid_displayfrequency;
convar_t	*vid_fullscreen;
convar_t	*vid_brightness;
convar_t	*vid_gamma;
convar_t	*vid_mode;

byte		*r_temppool;

ref_globals_t	tr;
glconfig_t	glConfig;
glstate_t		glState;
glwstate_t	glw_state;
static HWND	hWndFake;
static HDC	hDCFake;
static HGLRC	hGLRCFake;
static int	context_flags;

typedef enum
{
	rserr_ok,
	rserr_invalid_fullscreen,
	rserr_invalid_mode,
	rserr_unknown
} rserr_t;

typedef struct vidmode_s
{
	const char	*desc;
	int		width; 
	int		height;
	qboolean		wideScreen;
} vidmode_t;

vidmode_t vidmode[] =
{
{ "640 x 480",		640,	480,	false	},
{ "800 x 600",		800,	600,	false	},
{ "960 x 720",		960,	720,	false	},
{ "1024 x 768",		1024,	768,	false	},
{ "1152 x 864",		1152,	864,	false	},
{ "1280 x 800",		1280,	800,	false	},
{ "1280 x 960",		1280,	960,	false	},
{ "1280 x 1024",		1280,	1024,	false	},
{ "1600 x 1200",		1600,	1200,	false	},
{ "2048 x 1536",		2048,	1536,	false	},
{ "800 x 480 (wide)",	800,	480,	true	},
{ "856 x 480 (wide)",	856,	480,	true	},
{ "960 x 540 (wide)",	960,	540,	true	},
{ "1024 x 576 (wide)",	1024,	576,	true	},
{ "1024 x 600 (wide)",	1024,	600,	true	},
{ "1280 x 720 (wide)",	1280,	720,	true	},
{ "1360 x 768 (wide)",	1360,	768,	true	},
{ "1366 x 768 (wide)",	1366,	768,	true	},
{ "1440 x 900 (wide)",	1440,	900,	true	},
{ "1680 x 1050 (wide)",	1680,	1050,	true	},
{ "1920 x 1080 (wide)",	1920,	1080,	true	},
{ "1920 x 1200 (wide)",	1920,	1200,	true	},
{ "2560 x 1440 (wide)",	2560,	1440,	true	},
{ "2560 x 1600 (wide)",	2560,	1600,	true	},
{ "1600 x 900 (wide)",	1600,	 900,	true	},
{ "3840 x 2160 (wide)",	3840,	2160,	true	},
};

static dllfunc_t opengl_110funcs[] =
{
{ "glClearColor"				, (void **)&pglClearColor },
{ "glClear"				, (void **)&pglClear },
{ "glAlphaFunc"				, (void **)&pglAlphaFunc },
{ "glBlendFunc"				, (void **)&pglBlendFunc },
{ "glCullFace"				, (void **)&pglCullFace },
{ "glDrawBuffer"				, (void **)&pglDrawBuffer },
{ "glReadBuffer"				, (void **)&pglReadBuffer },
{ "glAccum"				, (void **)&pglAccum },
{ "glEnable"				, (void **)&pglEnable },
{ "glDisable"				, (void **)&pglDisable },
{ "glEnableClientState"			, (void **)&pglEnableClientState },
{ "glDisableClientState"			, (void **)&pglDisableClientState },
{ "glGetBooleanv"				, (void **)&pglGetBooleanv },
{ "glGetDoublev"				, (void **)&pglGetDoublev },
{ "glGetFloatv"				, (void **)&pglGetFloatv },
{ "glGetIntegerv"				, (void **)&pglGetIntegerv },
{ "glGetError"				, (void **)&pglGetError },
{ "glGetString"				, (void **)&pglGetString },
{ "glFinish"				, (void **)&pglFinish },
{ "glFlush"				, (void **)&pglFlush },
{ "glClearDepth"				, (void **)&pglClearDepth },
{ "glDepthFunc"				, (void **)&pglDepthFunc },
{ "glDepthMask"				, (void **)&pglDepthMask },
{ "glDepthRange"				, (void **)&pglDepthRange },
{ "glFrontFace"				, (void **)&pglFrontFace },
{ "glDrawElements"				, (void **)&pglDrawElements },
{ "glDrawArrays"				, (void **)&pglDrawArrays },
{ "glColorMask"				, (void **)&pglColorMask },
{ "glIndexPointer"				, (void **)&pglIndexPointer },
{ "glVertexPointer"				, (void **)&pglVertexPointer },
{ "glNormalPointer"				, (void **)&pglNormalPointer },
{ "glColorPointer"				, (void **)&pglColorPointer },
{ "glTexCoordPointer"			, (void **)&pglTexCoordPointer },
{ "glArrayElement"				, (void **)&pglArrayElement },
{ "glColor3f"				, (void **)&pglColor3f },
{ "glColor3fv"				, (void **)&pglColor3fv },
{ "glColor4f"				, (void **)&pglColor4f },
{ "glColor4fv"				, (void **)&pglColor4fv },
{ "glColor3ub"				, (void **)&pglColor3ub },
{ "glColor4ub"				, (void **)&pglColor4ub },
{ "glColor4ubv"				, (void **)&pglColor4ubv },
{ "glTexCoord1f"				, (void **)&pglTexCoord1f },
{ "glTexCoord2f"				, (void **)&pglTexCoord2f },
{ "glTexCoord3f"				, (void **)&pglTexCoord3f },
{ "glTexCoord4f"				, (void **)&pglTexCoord4f },
{ "glTexCoord1fv"				, (void **)&pglTexCoord1fv },
{ "glTexCoord2fv"				, (void **)&pglTexCoord2fv },
{ "glTexCoord3fv"				, (void **)&pglTexCoord3fv },
{ "glTexCoord4fv"				, (void **)&pglTexCoord4fv },
{ "glTexGenf"				, (void **)&pglTexGenf },
{ "glTexGenfv"				, (void **)&pglTexGenfv },
{ "glTexGeni"				, (void **)&pglTexGeni },
{ "glVertex2f"				, (void **)&pglVertex2f },
{ "glVertex3f"				, (void **)&pglVertex3f },
{ "glVertex3fv"				, (void **)&pglVertex3fv },
{ "glNormal3f"				, (void **)&pglNormal3f },
{ "glNormal3fv"				, (void **)&pglNormal3fv },
{ "glBegin"				, (void **)&pglBegin },
{ "glEnd"					, (void **)&pglEnd },
{ "glLineWidth"				, (void**)&pglLineWidth },
{ "glPointSize"				, (void**)&pglPointSize },
{ "glMatrixMode"				, (void **)&pglMatrixMode },
{ "glOrtho"				, (void **)&pglOrtho },
{ "glRasterPos2f"				, (void **) &pglRasterPos2f },
{ "glFrustum"				, (void **)&pglFrustum },
{ "glViewport"				, (void **)&pglViewport },
{ "glPushMatrix"				, (void **)&pglPushMatrix },
{ "glPopMatrix"				, (void **)&pglPopMatrix },
{ "glPushAttrib"				, (void **)&pglPushAttrib },
{ "glPopAttrib"				, (void **)&pglPopAttrib },
{ "glLoadIdentity"				, (void **)&pglLoadIdentity },
{ "glLoadMatrixd"				, (void **)&pglLoadMatrixd },
{ "glLoadMatrixf"				, (void **)&pglLoadMatrixf },
{ "glMultMatrixd"				, (void **)&pglMultMatrixd },
{ "glMultMatrixf"				, (void **)&pglMultMatrixf },
{ "glRotated"				, (void **)&pglRotated },
{ "glRotatef"				, (void **)&pglRotatef },
{ "glScaled"				, (void **)&pglScaled },
{ "glScalef"				, (void **)&pglScalef },
{ "glTranslated"				, (void **)&pglTranslated },
{ "glTranslatef"				, (void **)&pglTranslatef },
{ "glReadPixels"				, (void **)&pglReadPixels },
{ "glDrawPixels"				, (void **)&pglDrawPixels },
{ "glStencilFunc"				, (void **)&pglStencilFunc },
{ "glStencilMask"				, (void **)&pglStencilMask },
{ "glStencilOp"				, (void **)&pglStencilOp },
{ "glClearStencil"				, (void **)&pglClearStencil },
{ "glIsEnabled"				, (void **)&pglIsEnabled },
{ "glIsList"				, (void **)&pglIsList },
{ "glIsTexture"				, (void **)&pglIsTexture },
{ "glTexEnvf"				, (void **)&pglTexEnvf },
{ "glTexEnvfv"				, (void **)&pglTexEnvfv },
{ "glTexEnvi"				, (void **)&pglTexEnvi },
{ "glTexParameterf"				, (void **)&pglTexParameterf },
{ "glTexParameterfv"			, (void **)&pglTexParameterfv },
{ "glTexParameteri"				, (void **)&pglTexParameteri },
{ "glHint"				, (void **)&pglHint },
{ "glPixelStoref"				, (void **)&pglPixelStoref },
{ "glPixelStorei"				, (void **)&pglPixelStorei },
{ "glGenTextures"				, (void **)&pglGenTextures },
{ "glDeleteTextures"			, (void **)&pglDeleteTextures },
{ "glBindTexture"				, (void **)&pglBindTexture },
{ "glTexImage1D"				, (void **)&pglTexImage1D },
{ "glTexImage2D"				, (void **)&pglTexImage2D },
{ "glTexSubImage1D"				, (void **)&pglTexSubImage1D },
{ "glTexSubImage2D"				, (void **)&pglTexSubImage2D },
{ "glCopyTexImage1D"			, (void **)&pglCopyTexImage1D },
{ "glCopyTexImage2D"			, (void **)&pglCopyTexImage2D },
{ "glCopyTexSubImage1D"			, (void **)&pglCopyTexSubImage1D },
{ "glCopyTexSubImage2D"			, (void **)&pglCopyTexSubImage2D },
{ "glScissor"				, (void **)&pglScissor },
{ "glGetTexImage"				, (void **)&pglGetTexImage },
{ "glGetTexEnviv"				, (void **)&pglGetTexEnviv },
{ "glPolygonOffset"				, (void **)&pglPolygonOffset },
{ "glPolygonMode"				, (void **)&pglPolygonMode },
{ "glPolygonStipple"			, (void **)&pglPolygonStipple },
{ "glClipPlane"				, (void **)&pglClipPlane },
{ "glGetClipPlane"				, (void **)&pglGetClipPlane },
{ "glShadeModel"				, (void **)&pglShadeModel },
{ "glGetTexLevelParameteriv"			, (void **)&pglGetTexLevelParameteriv },
{ "glGetTexLevelParameterfv"			, (void **)&pglGetTexLevelParameterfv },
{ "glFogfv"				, (void **)&pglFogfv },
{ "glFogf"				, (void **)&pglFogf },
{ "glFogi"				, (void **)&pglFogi },
{ NULL					, NULL }
};

static dllfunc_t debugoutputfuncs[] =
{
{ "glDebugMessageControlARB"			, (void **)&pglDebugMessageControlARB },
{ "glDebugMessageInsertARB"			, (void **)&pglDebugMessageInsertARB },
{ "glDebugMessageCallbackARB"			, (void **)&pglDebugMessageCallbackARB },
{ "glGetDebugMessageLogARB"			, (void **)&pglGetDebugMessageLogARB },
{ NULL					, NULL }
};

static dllfunc_t multitexturefuncs[] =
{
{ "glMultiTexCoord1fARB"			, (void **)&pglMultiTexCoord1f },
{ "glMultiTexCoord2fARB"			, (void **)&pglMultiTexCoord2f },
{ "glMultiTexCoord3fARB"			, (void **)&pglMultiTexCoord3f },
{ "glMultiTexCoord4fARB"			, (void **)&pglMultiTexCoord4f },
{ "glActiveTextureARB"			, (void **)&pglActiveTexture },
{ "glActiveTextureARB"			, (void **)&pglActiveTextureARB },
{ "glClientActiveTextureARB"			, (void **)&pglClientActiveTexture },
{ "glClientActiveTextureARB"			, (void **)&pglClientActiveTextureARB },
{ NULL					, NULL }
};

static dllfunc_t texture3dextfuncs[] =
{
{ "glTexImage3DEXT"	  			, (void **)&pglTexImage3D },
{ "glTexSubImage3DEXT"			, (void **)&pglTexSubImage3D },
{ "glCopyTexSubImage3DEXT"			, (void **)&pglCopyTexSubImage3D },
{ NULL					, NULL }
};

static dllfunc_t texturecompressionfuncs[] =
{
{ "glCompressedTexImage3DARB"			, (void **)&pglCompressedTexImage3DARB },
{ "glCompressedTexImage2DARB"			, (void **)&pglCompressedTexImage2DARB },
{ "glCompressedTexImage1DARB"			, (void **)&pglCompressedTexImage1DARB },
{ "glCompressedTexSubImage3DARB"		, (void **)&pglCompressedTexSubImage3DARB },
{ "glCompressedTexSubImage2DARB"		, (void **)&pglCompressedTexSubImage2DARB },
{ "glCompressedTexSubImage1DARB"		, (void **)&pglCompressedTexSubImage1DARB },
{ "glGetCompressedTexImageARB"		, (void **)&pglGetCompressedTexImage },
{ NULL					, NULL }
};

static dllfunc_t wgl_funcs[] =
{
{ "wglSwapBuffers"				, (void **)&pwglSwapBuffers },
{ "wglCreateContext"			, (void **)&pwglCreateContext },
{ "wglDeleteContext"			, (void **)&pwglDeleteContext },
{ "wglMakeCurrent"				, (void **)&pwglMakeCurrent },
{ "wglGetCurrentContext"			, (void **)&pwglGetCurrentContext },
{ NULL					, NULL }
};

static dllfunc_t wglproc_funcs[] =
{
{ "wglGetProcAddress"			, (void **)&pwglGetProcAddress },
{ NULL, NULL }
};

static dllfunc_t wglswapintervalfuncs[] =
{
{ "wglSwapIntervalEXT"			, (void **)&pwglSwapIntervalEXT },
{ NULL, NULL }
};

static dllfunc_t wglgetextensionsstring[] =
{
{ "wglGetExtensionsStringEXT"			, (void **)&pwglGetExtensionsStringEXT },
{ NULL, NULL }
};

dll_info_t opengl_dll = { "opengl32.dll", wgl_funcs, true };

/*
========================
DebugCallback

For ARB_debug_output
========================
*/
static void CALLBACK GL_DebugOutput( GLuint source, GLuint type, GLuint id, GLuint severity, GLint length, const GLcharARB *message, GLvoid *userParam )
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
	default:
		if( host_developer.value < DEV_EXTENDED )
			return;
		Con_Printf( S_OPENGL_NOTE "%s\n", message );
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
	else Con_Printf( S_ERROR "GL_SetExtension: invalid extension %d\n", r_ext );
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
	Con_Printf( S_ERROR "GL_Support: invalid extension %d\n", r_ext );

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
GL_GetProcAddress
=================
*/
void *GL_GetProcAddress( const char *name )
{
	void	*p = NULL;

	if( pwglGetProcAddress != NULL )
		p = (void *)pwglGetProcAddress( name );
	if( !p ) p = (void *)Sys_GetProcAddress( &opengl_dll, name );

	return p;
}

/*
=================
GL_CheckExtension
=================
*/
void GL_CheckExtension( const char *name, const dllfunc_t *funcs, const char *cvarname, int r_ext )
{
	const dllfunc_t	*func;
	convar_t		*parm = NULL;
	const char	*extensions_string;

	Con_Reportf( "GL_CheckExtension: %s ", name );
	GL_SetExtension( r_ext, true );

	if( cvarname )
	{
		// system config disable extensions
		parm = Cvar_Get( cvarname, "1", FCVAR_GLCONFIG, va( CVAR_GLCONFIG_DESCRIPTION, name ));
          }

	if(( parm && !CVAR_TO_BOOL( parm )) || ( !CVAR_TO_BOOL( gl_extensions ) && r_ext != GL_OPENGL_110 ))
	{
		Con_Reportf( "- disabled\n" );
		GL_SetExtension( r_ext, false );
		return; // nothing to process at
	}

	extensions_string = glConfig.extensions_string; 

	if( name[0] == 'W' && name[1] == 'G' && name[2] == 'L' && glConfig.wgl_extensions_string != NULL )
		extensions_string = glConfig.wgl_extensions_string;

	if(( name[2] == '_' || name[3] == '_' ) && !Q_strstr( extensions_string, name ))
	{
		GL_SetExtension( r_ext, false );	// update render info
		Con_Reportf( "- ^1failed\n" );
		return;
	}

	// clear exports
	for( func = funcs; func && func->name; func++ )
		*func->func = NULL;

	for( func = funcs; func && func->name != NULL; func++ )
	{
		// functions are cleared before all the extensions are evaluated
		if((*func->func = (void *)GL_GetProcAddress( func->name )) == NULL )
			GL_SetExtension( r_ext, false ); // one or more functions are invalid, extension will be disabled
	}

	if( GL_Support( r_ext ))
		Con_Reportf( "- ^2enabled\n" );
	else Con_Reportf( "- ^1failed\n" );
}

/*
===============
GL_UpdateSwapInterval
===============
*/
void GL_UpdateSwapInterval( void )
{
	// disable VSync while level is loading
	if( cls.state < ca_active )
	{
		if( pwglSwapIntervalEXT != NULL )
			pwglSwapIntervalEXT( 0 );
		SetBits( gl_vsync->flags, FCVAR_CHANGED );
	}
	else if( FBitSet( gl_vsync->flags, FCVAR_CHANGED ))
	{
		ClearBits( gl_vsync->flags, FCVAR_CHANGED );

		if( pwglSwapIntervalEXT != NULL )
			pwglSwapIntervalEXT( bound( -1, (int)gl_vsync->value, 1 ));
	}
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

	if( Sys_CheckParm( "-gldebug" ))
		SetBits( context_flags, FCONTEXT_DEBUG_ARB );

	// init draw stack
	tr.draw_list = &tr.draw_stack[0];
	tr.draw_stack_pos = 0;
}

/*
===============
GL_ContextError
===============
*/
static void GL_ContextError( void )
{
	DWORD error = GetLastError();

	if( error == ( 0xc0070000|ERROR_INVALID_VERSION_ARB ))
		Con_Printf( S_ERROR "Unsupported OpenGL context version (%s).\n", "2.0" );
	else if( error == ( 0xc0070000|ERROR_INVALID_PROFILE_ARB ))
		Con_Printf( S_ERROR "Unsupported OpenGL profile (%s).\n", "compat" );
	else if( error == ( 0xc0070000|ERROR_INVALID_OPERATION ))
		Con_Printf( S_ERROR "wglCreateContextAttribsARB returned invalid operation.\n" );
	else if( error == ( 0xc0070000|ERROR_DC_NOT_FOUND ))
		Con_Printf( S_ERROR "wglCreateContextAttribsARB returned dc not found.\n" );
	else if( error == ( 0xc0070000|ERROR_INVALID_PIXEL_FORMAT ))
		Con_Printf( S_ERROR "wglCreateContextAttribsARB returned dc not found.\n" );
	else if( error == ( 0xc0070000|ERROR_NO_SYSTEM_RESOURCES ))
		Con_Printf( S_ERROR "wglCreateContextAttribsARB ran out of system resources.\n" );
	else if( error == ( 0xc0070000|ERROR_INVALID_PARAMETER ))
		Con_Printf( S_ERROR "wglCreateContextAttribsARB reported invalid parameter.\n" );
	else Con_Printf( S_ERROR "Unknown error creating an OpenGL (%s) Context.\n", "2.0" );
}

/*
=================
GL_CreateContext
=================
*/
qboolean GL_CreateContext( void )
{
	HGLRC	hBaseRC;
	int	profile_mask;
	int	arb_flags;

	glw_state.extended = false;

	if(!( glw_state.hGLRC = pwglCreateContext( glw_state.hDC )))
		return GL_DeleteContext();

	if(!( pwglMakeCurrent( glw_state.hDC, glw_state.hGLRC )))
		return GL_DeleteContext();

	if( !context_flags ) // debug bit kill the perfomance
		return true;

	pwglCreateContextAttribsARB = GL_GetProcAddress( "wglCreateContextAttribsARB" );

	profile_mask = WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB;

	if( FBitSet( context_flags, FCONTEXT_DEBUG_ARB ))
		arb_flags = WGL_CONTEXT_DEBUG_BIT_ARB;
	else arb_flags = 0;

	if( pwglCreateContextAttribsARB != NULL )
	{
		int attribs[] =
		{
		WGL_CONTEXT_MAJOR_VERSION_ARB, 2,
		WGL_CONTEXT_MINOR_VERSION_ARB, 0,
		WGL_CONTEXT_FLAGS_ARB, arb_flags,         
		WGL_CONTEXT_PROFILE_MASK_ARB, profile_mask,
		0
		};

		hBaseRC = glw_state.hGLRC; // backup
		glw_state.hGLRC = NULL;

		if( !( glw_state.hGLRC = pwglCreateContextAttribsARB( glw_state.hDC, NULL, attribs )))
		{
			glw_state.hGLRC = hBaseRC;
			GL_ContextError();
			return true; // just use old context
		}

		if(!( pwglMakeCurrent( glw_state.hDC, glw_state.hGLRC )))
		{
			pwglDeleteContext( glw_state.hGLRC );
			glw_state.hGLRC = hBaseRC;
			GL_ContextError();
			return true;
		}

		Con_Reportf( "GL_CreateContext: using extended context\n" );
		pwglDeleteContext( hBaseRC );	// release first context
		glw_state.extended = true;
	}

	return true;
}

/*
=================
GL_UpdateContext
=================
*/
qboolean GL_UpdateContext( void )
{
	if(!( pwglMakeCurrent( glw_state.hDC, glw_state.hGLRC )))
		return GL_DeleteContext();

	return true;
}

/*
=================
GL_DeleteContext

always return false
=================
*/
qboolean GL_DeleteContext( void )
{
	if( pwglMakeCurrent )
		pwglMakeCurrent( NULL, NULL );

	if( glw_state.hGLRC )
	{
		if( pwglDeleteContext )
			pwglDeleteContext( glw_state.hGLRC );
		glw_state.hGLRC = NULL;
	}

	if( glw_state.hDC )
	{
		ReleaseDC( host.hWnd, glw_state.hDC );
		glw_state.hDC = NULL;
	}

	return false;
}

/*
=================
VID_ChoosePFD
=================
*/
static int VID_ChoosePFD( PIXELFORMATDESCRIPTOR *pfd, int colorBits, int alphaBits, int depthBits, int stencilBits )
{
	if( pwglChoosePixelFormat != NULL )
	{
		UINT	numPixelFormats;
		int	pixelFormat = 0;
		int	attribs[24];

		attribs[0] = WGL_ACCELERATION_ARB;
		attribs[1] = WGL_FULL_ACCELERATION_ARB;
		attribs[2] = WGL_DRAW_TO_WINDOW_ARB;
		attribs[3] = TRUE;
		attribs[4] = WGL_SUPPORT_OPENGL_ARB;
		attribs[5] = TRUE;
		attribs[6] = WGL_DOUBLE_BUFFER_ARB;
		attribs[7] = TRUE;
		attribs[8] = WGL_PIXEL_TYPE_ARB;
		attribs[9] = WGL_TYPE_RGBA_ARB;
		attribs[10] = WGL_COLOR_BITS_ARB;
		attribs[11] = colorBits;
		attribs[12] = WGL_ALPHA_BITS_ARB;
		attribs[13] = alphaBits;
		attribs[14] = WGL_DEPTH_BITS_ARB;
		attribs[15] = depthBits;
		attribs[16] = WGL_STENCIL_BITS_ARB;
		attribs[17] = stencilBits;
		attribs[18] = WGL_SAMPLE_BUFFERS_ARB;
		attribs[19] = TRUE;
		attribs[20] = WGL_SAMPLES_ARB;
		attribs[21] = bound( 1, (int)gl_wgl_msaa_samples->value, 16 );
		attribs[22] = 0;
		attribs[23] = 0;

		pwglChoosePixelFormat( glw_state.hDC, attribs, NULL, 1, &pixelFormat, &numPixelFormats );

		if( pixelFormat )
		{
			attribs[0] = WGL_SAMPLES_ARB;
			pwglGetPixelFormatAttribiv( glw_state.hDC, pixelFormat, 0, 1, attribs, &glConfig.max_multisamples );
			if( glConfig.max_multisamples <= 1 ) Con_DPrintf( S_WARN "MSAA is not allowed\n" );

			return pixelFormat;
		}
	}

	// fallback: fill out the PFD
	pfd->nSize = sizeof (PIXELFORMATDESCRIPTOR);
	pfd->nVersion = 1;
	pfd->dwFlags = PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;
	pfd->iPixelType = PFD_TYPE_RGBA;

	pfd->cColorBits = colorBits;
	pfd->cRedBits = 0;
	pfd->cRedShift = 0;
	pfd->cGreenBits = 0;
	pfd->cGreenShift = 0;
	pfd->cBlueBits = 0;
	pfd->cBlueShift = 0;	// wow! Blue Shift %)

	pfd->cAlphaBits = alphaBits;
	pfd->cAlphaShift = 0;

	pfd->cAccumBits = 0;
	pfd->cAccumRedBits = 0;
	pfd->cAccumGreenBits = 0;
	pfd->cAccumBlueBits = 0;
	pfd->cAccumAlphaBits= 0;

	pfd->cDepthBits = depthBits;
	pfd->cStencilBits = stencilBits;

	pfd->cAuxBuffers = 0;
	pfd->iLayerType = PFD_MAIN_PLANE;
	pfd->bReserved = 0;

	pfd->dwLayerMask = 0;
	pfd->dwVisibleMask = 0;
	pfd->dwDamageMask = 0;

	// count PFDs
	return ChoosePixelFormat( glw_state.hDC, pfd );
}

/*
=================
VID_StartupGamma
=================
*/
void VID_StartupGamma( void )
{
	BuildGammaTable( vid_gamma->value, vid_brightness->value );
	Con_Reportf( "VID_StartupGamma: gamma %g brightness %g\n", vid_gamma->value, vid_brightness->value );
	ClearBits( vid_brightness->flags, FCVAR_CHANGED );
	ClearBits( vid_gamma->flags, FCVAR_CHANGED );
}

/*
=================
VID_InitDefaultResolution
=================
*/
void VID_InitDefaultResolution( void )
{
	// we need to have something valid here
	// until video subsystem initialized
	glState.width = 640;
	glState.height = 480;
}

/*
=================
VID_GetModeString
=================
*/
const char *VID_GetModeString( int vid_mode )
{
	if( vid_mode >= 0 && vid_mode < num_vidmodes )
		return vidmode[vid_mode].desc;
	return NULL; // out of bounds
}

/*
=================
VID_DestroyFakeWindow
=================
*/
void VID_DestroyFakeWindow( void )
{
	if( hGLRCFake )
	{
		pwglMakeCurrent( NULL, NULL );
		pwglDeleteContext( hGLRCFake );
		hGLRCFake = NULL;
	}

	if( hDCFake )
	{
		ReleaseDC( hWndFake, hDCFake );
		hDCFake = NULL;
	}

	if( hWndFake )
	{
		DestroyWindow( hWndFake );
		UnregisterClass( "TestWindow", host.hInst );
		hWndFake = NULL;
	}
}

/*
=================
VID_CreateFakeWindow
=================
*/
void VID_CreateFakeWindow( void )
{
	WNDCLASSEX		wndClass;
	PIXELFORMATDESCRIPTOR	pfd;
	int			pixelFormat;

	// MSAA disabled
	if( !CVAR_TO_BOOL( gl_wgl_msaa_samples ))
		return;

	memset( &wndClass, 0, sizeof( WNDCLASSEX ));
	hGLRCFake = NULL;
	hWndFake = NULL;
	hDCFake = NULL;

	// register the window class
	wndClass.cbSize = sizeof( WNDCLASSEX );
	wndClass.lpfnWndProc = DefWindowProc;
	wndClass.hInstance = host.hInst;
	wndClass.lpszClassName = "TestWindow";

	if( !RegisterClassEx( &wndClass ))
		return;

	// Create the fake window
	if(( hWndFake = CreateWindowEx( 0, "TestWindow", "Xash3D", 0, 0, 0, 100, 100, NULL, NULL, wndClass.hInstance, NULL )) == NULL )
	{
		UnregisterClass( "TestWindow", wndClass.hInstance );
		return;
	}

	// Get a DC for the fake window
	if(( hDCFake = GetDC( hWndFake )) == NULL )
	{
		VID_DestroyFakeWindow();
		return;
	}

	// Choose a pixel format
	memset( &pfd, 0, sizeof( PIXELFORMATDESCRIPTOR ));

	pfd.nSize = sizeof( PIXELFORMATDESCRIPTOR );
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.iLayerType = PFD_MAIN_PLANE;
	pfd.cColorBits = 32;
	pfd.cAlphaBits = 8;
	pfd.cDepthBits = 24;
	pfd.cStencilBits = 8;

	if(( pixelFormat = ChoosePixelFormat( hDCFake, &pfd )) == 0 )
	{
		VID_DestroyFakeWindow();
		return;
	}

	// Set the pixel format
	if( !SetPixelFormat( hDCFake, pixelFormat, &pfd ))
	{
		VID_DestroyFakeWindow();
		return;
	}

	// Create the fake GL context
	if(( hGLRCFake = pwglCreateContext( hDCFake )) == NULL )
	{
		VID_DestroyFakeWindow();
		return;
	}

	// Make the fake GL context current
	if( !pwglMakeCurrent( hDCFake, hGLRCFake ))
	{
		VID_DestroyFakeWindow();
		return;
	}

	// We only need these function pointers if available
	pwglGetPixelFormatAttribiv = GL_GetProcAddress( "wglGetPixelFormatAttribivARB" );
	pwglChoosePixelFormat = GL_GetProcAddress( "wglChoosePixelFormatARB" );

	// destroy now it's no longer needed
	VID_DestroyFakeWindow();
}

/*
=================
GL_SetPixelformat
=================
*/
qboolean GL_SetPixelformat( void )
{
	PIXELFORMATDESCRIPTOR	PFD;
	int			colorBits = 32;
	int			alphaBits = 8;
	int			stencilBits = 8;
	int			pixelFormat = 0;
	int			depthBits = 24;

	if(( glw_state.hDC = GetDC( host.hWnd )) == NULL )
		return false;

	if( glw_state.desktopBitsPixel < 32 )
	{
		// clear alphabits in case we in 16-bit mode
		colorBits = glw_state.desktopBitsPixel;
		alphaBits = 0;
	}
	else
	{
		// no reason to trying enable MSAA on a highcolor
		VID_CreateFakeWindow();
	}

	// choose a pixel format
	pixelFormat = VID_ChoosePFD( &PFD, colorBits, alphaBits, depthBits, stencilBits );

	if( !pixelFormat )
	{
		// try again with default color/depth/stencil
		pixelFormat = VID_ChoosePFD( &PFD, colorBits, 0, depthBits, 0 );

		if( !pixelFormat )
		{
			Con_Printf( S_ERROR "GL_SetPixelformat: failed to find an appropriate PIXELFORMAT\n" );
			return false;
		}
	}

	// set the pixel format
	if( !SetPixelFormat( glw_state.hDC, pixelFormat, &PFD ))
	{
		Con_Printf( S_ERROR "GL_SetPixelformat: failed\n" );
		return false;
	}

	DescribePixelFormat( glw_state.hDC, pixelFormat, sizeof( PIXELFORMATDESCRIPTOR ), &PFD );

	if( PFD.dwFlags & PFD_GENERIC_FORMAT )
	{
		if( PFD.dwFlags & PFD_GENERIC_ACCELERATED )
		{
			Con_Reportf( "VID_ChoosePFD: using Generic MCD acceleration\n" );
		}
		else
		{
			Con_Printf( S_ERROR "GL_SetPixelformat: no hardware acceleration found\n" );
			return false;
		}
	}
	else
	{
		Con_Reportf( "VID_ChoosePFD: using hardware acceleration\n" );
	}

	glConfig.color_bits = PFD.cColorBits;
	glConfig.alpha_bits = PFD.cAlphaBits;
	glConfig.depth_bits = PFD.cDepthBits;
	glConfig.stencil_bits = PFD.cStencilBits;

	if( PFD.cStencilBits != 0 )
		glState.stencilEnabled = true;
	else glState.stencilEnabled = false;

	// print out PFD specifics 
	Con_Reportf( "PixelFormat: color: %d-bit, Z-Buffer: %d-bit, stencil: %d-bit\n", PFD.cColorBits, PFD.cDepthBits, PFD.cStencilBits );

	return true;
}

/*
=================
R_SaveVideoMode
=================
*/
void R_SaveVideoMode( int vid_mode )
{
	int	mode = bound( 0, vid_mode, num_vidmodes ); // check range

	glState.width = vidmode[mode].width;
	glState.height = vidmode[mode].height;
	glState.wideScreen = vidmode[mode].wideScreen;
	Cvar_FullSet( "width", va( "%i", glState.width ), FCVAR_READ_ONLY );
	Cvar_FullSet( "height", va( "%i", glState.height ), FCVAR_READ_ONLY );
	Cvar_SetValue( "vid_mode", mode ); // merge if it out of bounds

	Con_Reportf( "Set: %s [%dx%d]\n", vidmode[mode].desc, vidmode[mode].width, vidmode[mode].height );
}

/*
=================
R_DescribeVIDMode
=================
*/
qboolean R_DescribeVIDMode( int width, int height )
{
	int	i;

	for( i = 0; i < sizeof( vidmode ) / sizeof( vidmode[0] ); i++ )
	{
		if( vidmode[i].width == width && vidmode[i].height == height )
		{
			// found specified mode
			Cvar_SetValue( "vid_mode", i );
			return true;
		}
	}

	return false;
}

/*
=================
VID_CreateWindow
=================
*/
qboolean VID_CreateWindow( int width, int height, qboolean fullscreen )
{
	int		x = 0, y = 0, w, h;
	int		stylebits = WINDOW_STYLE;
	int		exstyle = WINDOW_EX_STYLE;
	static string	wndname;
	HWND		window;
	RECT		rect;	
	WNDCLASS		wc;

	Q_strncpy( wndname, GI->title, sizeof( wndname ));

	// register the frame class
	wc.style         = CS_OWNDC|CS_NOCLOSE;
	wc.lpfnWndProc   = (WNDPROC)IN_WndProc;
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = 0;
	wc.hInstance     = host.hInst;
	wc.hCursor       = LoadCursor( NULL, IDC_ARROW );
	wc.hbrBackground = (void *)COLOR_3DSHADOW;
	wc.lpszClassName = WINDOW_NAME;
	wc.lpszMenuName  = 0;
	wc.hIcon         = 0;

	// find the icon file in the filesystem
	if( FS_FileExists( GI->iconpath, true ))
	{
		if( FS_GetDiskPath( GI->iconpath, true ))
		{
			string	localPath;
			Q_snprintf( localPath, sizeof( localPath ), "%s/%s", GI->gamedir, GI->iconpath );
			wc.hIcon = LoadImage( NULL, localPath, IMAGE_ICON, 0, 0, LR_LOADFROMFILE|LR_DEFAULTSIZE );
		}
		else Con_Printf( "Extract %s from pak if you want to see it.\n", GI->iconpath );
	}

	// couldn't loaded for some reasons? use default
	if( !wc.hIcon ) wc.hIcon = LoadIcon( host.hInst, MAKEINTRESOURCE( 101 ));

	if( !RegisterClass( &wc ))
	{ 
		Con_Printf( S_ERROR "VID_CreateWindow: couldn't register window class %s\n" WINDOW_NAME );
		return false;
	}

	if( fullscreen )
	{
		stylebits = WS_POPUP|WS_VISIBLE;
		exstyle = WS_EX_TOPMOST;
	}

	rect.left = 0;
	rect.top = 0;
	rect.right  = width;
	rect.bottom = height;

	AdjustWindowRect( &rect, stylebits, FALSE );
	w = rect.right - rect.left;
	h = rect.bottom - rect.top;

#if 0
	RECT WindowRect;
	unsigned WindowHeight;
	HWND WindowHandle;

	WindowHandle = FindWindow("Shell_TrayWnd", NULL);
	GetWindowRect(WindowHandle, &WindowRect);
	WindowHeight = WindowRect.bottom - WindowRect.top;
#endif
	if( !fullscreen )
	{
		x = window_xpos->value;
		y = window_ypos->value;

		// adjust window coordinates if necessary 
		// so that the window is completely on screen
		if( x < 0 ) x = 0;
		if( y < 0 ) y = 0;

		if( Cvar_VariableInteger( "vid_mode" ) != glConfig.prev_mode )
		{
			// adjust window in the screen size
			if( x + w > glw_state.desktopWidth )
				x = ( glw_state.desktopWidth - w );

			if( y + h > glw_state.desktopHeight )
				y = ( glw_state.desktopHeight - h );
		}
	}

	window = CreateWindowEx( exstyle, WINDOW_NAME, wndname, stylebits, x, y, w, h, NULL, NULL, host.hInst, NULL );

	if( host.hWnd != window )
	{
		// make sure what CreateWindowEx call the IN_WndProc
		Con_Printf( S_WARN "VID_CreateWindow: bad hWnd for '%s'\n", wndname );
	}

	if( !host.hWnd ) 
	{
		// host.hWnd must be filled in IN_WndProc
		Con_Printf( S_ERROR "VID_CreateWindow: couldn't create '%s'\n", wndname );
		return false;
	}

	ShowWindow( host.hWnd, SW_SHOW );
	UpdateWindow( host.hWnd );

	// init all the gl stuff for the window
	if( !GL_SetPixelformat( ))
	{
		ShowWindow( host.hWnd, SW_HIDE );
		DestroyWindow( host.hWnd );
		host.hWnd = NULL;

		UnregisterClass( WINDOW_NAME, host.hInst );
		Con_Printf( S_ERROR "OpenGL driver not installed\n" );

		return false;
	}

	if( !glw_state.initialized )
	{
		if( !GL_CreateContext( ))
			return false;

		VID_StartupGamma();
	}
	else
	{
		if( !GL_UpdateContext( ))
			return false;		
	}

	SetForegroundWindow( host.hWnd );
	SetFocus( host.hWnd );

	return true;
}

/*
=================
VID_DestroyWindow
=================
*/
void VID_DestroyWindow( void )
{
	if( pwglMakeCurrent )
		pwglMakeCurrent( NULL, NULL );

	if( glw_state.hDC )
	{
		ReleaseDC( host.hWnd, glw_state.hDC );
		glw_state.hDC = NULL;
	}

	if( host.hWnd )
	{
		DestroyWindow ( host.hWnd );
		host.hWnd = NULL;
	}

	UnregisterClass( WINDOW_NAME, host.hInst );

	if( glState.fullScreen )
	{
		ChangeDisplaySettings( 0, 0 );
		glState.fullScreen = false;
	}
}

/*
=================
R_ChangeDisplaySettings
=================
*/
rserr_t R_ChangeDisplaySettings( int vid_mode, qboolean fullscreen )
{
	int	width, height;
	int	cds_result;
	HDC	hDC;
	
	R_SaveVideoMode( vid_mode );

	width = glState.width;
	height = glState.height;

	// check our desktop attributes
	hDC = GetDC( GetDesktopWindow( ));
	glw_state.desktopBitsPixel = GetDeviceCaps( hDC, BITSPIXEL );
	glw_state.desktopWidth = GetDeviceCaps( hDC, HORZRES );
	glw_state.desktopHeight = GetDeviceCaps( hDC, VERTRES );
	ReleaseDC( GetDesktopWindow(), hDC );

	// destroy the existing window
	if( host.hWnd ) VID_DestroyWindow();

	// do a CDS if needed
	if( fullscreen )
	{
		DEVMODE	dm;

		memset( &dm, 0, sizeof( dm ));
		dm.dmSize = sizeof( dm );
		dm.dmPelsWidth = width;
		dm.dmPelsHeight = height;
		dm.dmFields = DM_PELSWIDTH|DM_PELSHEIGHT;

		if( vid_displayfrequency->value > 0 )
		{
			if( vid_displayfrequency->value < 60 ) Cvar_SetValue( "vid_displayfrequency", 60 );
			if( vid_displayfrequency->value > 100 ) Cvar_SetValue( "vid_displayfrequency", 100 );

			dm.dmFields |= DM_DISPLAYFREQUENCY;
			dm.dmDisplayFrequency = vid_displayfrequency->value;
		}

		cds_result = ChangeDisplaySettings( &dm, CDS_FULLSCREEN );

		if( cds_result == DISP_CHANGE_SUCCESSFUL )
		{
			glState.fullScreen = true;

			if( !VID_CreateWindow( width, height, true ))
				return rserr_invalid_mode;
			return rserr_ok;
		}
		else if( cds_result == DISP_CHANGE_BADDUALVIEW )
		{
			dm.dmPelsWidth = width * 2;
			dm.dmPelsHeight = height;
			dm.dmFields = DM_PELSWIDTH|DM_PELSHEIGHT;

			// our first CDS failed, so maybe we're running on some weird dual monitor system 
			if( ChangeDisplaySettings( &dm, CDS_FULLSCREEN ) != DISP_CHANGE_SUCCESSFUL )
			{
				ChangeDisplaySettings( 0, 0 );
				glState.fullScreen = false;
				if( !VID_CreateWindow( width, height, false ))
					return rserr_invalid_mode;
				return rserr_invalid_fullscreen;
			}
			else
			{
				if( !VID_CreateWindow( width, height, true ))
					return rserr_invalid_mode;
				glState.fullScreen = true;
				return rserr_ok;
			}
		}
		else
		{
			int	freq_specified = 0;

			if( vid_displayfrequency->value > 0 )
			{
				// clear out custom frequency
				freq_specified = vid_displayfrequency->value;
				Cvar_SetValue( "vid_displayfrequency", 0.0f );
				dm.dmFields &= ~DM_DISPLAYFREQUENCY;
				dm.dmDisplayFrequency = 0;
			}

			// our first CDS failed, so maybe we're running with too high displayfrequency
			if( ChangeDisplaySettings( &dm, CDS_FULLSCREEN ) != DISP_CHANGE_SUCCESSFUL )
			{
				ChangeDisplaySettings( 0, 0 );
				glState.fullScreen = false;
				if( !VID_CreateWindow( width, height, false ))
					return rserr_invalid_mode;
				return rserr_invalid_fullscreen;
			}
			else
			{
				if( !VID_CreateWindow( width, height, true ))
					return rserr_invalid_mode;

				if( freq_specified )
					Con_Printf( S_ERROR "VID_SetMode: display frequency %i Hz is not supported\n", freq_specified );
				glState.fullScreen = true;
				return rserr_ok;
			}
		}
	}
	else
	{
		ChangeDisplaySettings( 0, 0 );
		glState.fullScreen = false;
		if( !VID_CreateWindow( width, height, false ))
			return rserr_invalid_mode;
	}

	return rserr_ok;
}

/*
==================
VID_SetMode

Set the described video mode
==================
*/
qboolean VID_SetMode( void )
{
	qboolean	fullscreen;
	rserr_t	err;

	if( vid_mode->value == -1 )	// trying to get resolution automatically by default
	{
		HDC	hDCScreen = GetDC( NULL );
		int	iScreenWidth = GetDeviceCaps( hDCScreen, HORZRES );
		int	iScreenHeight = GetDeviceCaps( hDCScreen, VERTRES );

		ReleaseDC( NULL, hDCScreen );

		if( R_DescribeVIDMode( iScreenWidth, iScreenHeight ))
		{
			Con_Reportf( "found specified vid mode %i [%ix%i]\n", (int)vid_mode->value, iScreenWidth, iScreenHeight );
			Cvar_SetValue( "fullscreen", 1 );
		}
		else
		{
			Con_Reportf( "failed to set specified vid mode [%ix%i]\n", iScreenWidth, iScreenHeight );
			Cvar_SetValue( "vid_mode", VID_DEFAULTMODE );
		}
	}

	fullscreen = vid_fullscreen->value;
	SetBits( gl_vsync->flags, FCVAR_CHANGED );

	if(( err = R_ChangeDisplaySettings( vid_mode->value, fullscreen )) == rserr_ok )
	{
		glConfig.prev_mode = vid_mode->value;
	}
	else
	{
		if( err == rserr_invalid_fullscreen )
		{
			Cvar_SetValue( "fullscreen", 0 );
			Con_Printf( S_ERROR "VID_SetMode: fullscreen unavailable in this mode\n" );
			if(( err = R_ChangeDisplaySettings( vid_mode->value, false )) == rserr_ok )
				return true;
		}
		else if( err == rserr_invalid_mode )
		{
			Con_Printf( S_ERROR "VID_SetMode: invalid mode\n" );
			Cvar_SetValue( "vid_mode", glConfig.prev_mode );
		}

		// try setting it back to something safe
		if(( err = R_ChangeDisplaySettings( glConfig.prev_mode, false )) != rserr_ok )
		{
			Con_Printf( S_ERROR "VID_SetMode: could not revert to safe mode\n" );
			return false;
		}
	}

	return true;
}

/*
==================
VID_CheckChanges

check vid modes and fullscreen
==================
*/
void VID_CheckChanges( void )
{
	if( FBitSet( cl_allow_levelshots->flags, FCVAR_CHANGED ))
          {
		GL_FreeTexture( cls.loadingBar );
		SCR_RegisterTextures(); // reload 'lambda' image
		ClearBits( cl_allow_levelshots->flags, FCVAR_CHANGED );
          }
 
	if( host.renderinfo_changed )
	{
		if( !VID_SetMode( ))
		{
			Sys_Error( "Can't re-initialize video subsystem\n" );
		}
		else
		{
			host.renderinfo_changed = false;
			SCR_VidInit(); // tell the client.dll what vid_mode has changed
		}
	}
}

/*
==================
R_Init_OpenGL
==================
*/
qboolean R_Init_OpenGL( void )
{
	Sys_LoadLibrary( &opengl_dll );	// load opengl32.dll

	if( !opengl_dll.link )
		return false;

	if( context_flags || CVAR_TO_BOOL( gl_wgl_msaa_samples ))
		GL_CheckExtension( "OpenGL Internal ProcAddress", wglproc_funcs, NULL, GL_WGL_PROCADDRESS );

	return VID_SetMode();
}

/*
==================
R_Free_OpenGL
==================
*/
void R_Free_OpenGL( void )
{
	GL_DeleteContext ();

	VID_DestroyWindow ();

	Sys_FreeLibrary( &opengl_dll );

	// now all extensions are disabled
	memset( glConfig.extension, 0, sizeof( glConfig.extension ));
	glw_state.initialized = false;
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
	Con_Printf( "\n" );
	Con_Printf( "GL_VENDOR: %s\n", glConfig.vendor_string );
	Con_Printf( "GL_RENDERER: %s\n", glConfig.renderer_string );
	Con_Printf( "GL_VERSION: %s\n", glConfig.version_string );

	// don't spam about extensions
	if( host_developer.value >= DEV_EXTENDED )
	{
		Con_Printf( "GL_EXTENSIONS: %s\n", glConfig.extensions_string );

		if( glConfig.wgl_extensions_string != NULL )
			Con_Printf( "\nWGL_EXTENSIONS: %s\n", glConfig.wgl_extensions_string );
	}

	Con_Printf( "GL_MAX_TEXTURE_SIZE: %i\n", glConfig.max_2d_texture_size );
	
	if( GL_Support( GL_ARB_MULTITEXTURE ))
		Con_Printf( "GL_MAX_TEXTURE_UNITS_ARB: %i\n", glConfig.max_texture_units );
	if( GL_Support( GL_TEXTURE_CUBEMAP_EXT ))
		Con_Printf( "GL_MAX_CUBE_MAP_TEXTURE_SIZE_ARB: %i\n", glConfig.max_cubemap_size );
	if( GL_Support( GL_ANISOTROPY_EXT ))
		Con_Printf( "GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT: %.1f\n", glConfig.max_texture_anisotropy );
	if( GL_Support( GL_TEXTURE_2D_RECT_EXT ))
		Con_Printf( "GL_MAX_RECTANGLE_TEXTURE_SIZE: %i\n", glConfig.max_2d_rectangle_size );
	if( GL_Support( GL_TEXTURE_ARRAY_EXT ))
		Con_Printf( "GL_MAX_ARRAY_TEXTURE_LAYERS_EXT: %i\n", glConfig.max_2d_texture_layers );
	if( GL_Support( GL_SHADER_GLSL100_EXT ))
	{
		Con_Printf( "GL_MAX_TEXTURE_COORDS_ARB: %i\n", glConfig.max_texture_coords );
		Con_Printf( "GL_MAX_TEXTURE_IMAGE_UNITS_ARB: %i\n", glConfig.max_teximage_units );
		Con_Printf( "GL_MAX_VERTEX_UNIFORM_COMPONENTS_ARB: %i\n", glConfig.max_vertex_uniforms );
		Con_Printf( "GL_MAX_VERTEX_ATTRIBS_ARB: %i\n", glConfig.max_vertex_attribs );
	}

	Con_Printf( "\n" );
	Con_Printf( "MODE: %s\n", vidmode[(int)vid_mode->value].desc );
	Con_Printf( "\n" );
	Con_Printf( "VERTICAL SYNC: %s\n", gl_vsync->value ? "enabled" : "disabled" );
	Con_Printf( "Color %d bits, Alpha %d bits, Depth %d bits, Stencil %d bits\n", glConfig.color_bits,
		glConfig.alpha_bits, glConfig.depth_bits, glConfig.stencil_bits );
}

//=======================================================================

/*
=================
GL_InitCommands
=================
*/
void GL_InitCommands( void )
{
	// system screen width and height (don't suppose for change from console at all)
	r_speeds = Cvar_Get( "r_speeds", "0", FCVAR_ARCHIVE, "shows renderer speeds" );
	r_fullbright = Cvar_Get( "r_fullbright", "0", FCVAR_CHEAT, "disable lightmaps, get fullbright for entities" );
	r_norefresh = Cvar_Get( "r_norefresh", "0", 0, "disable 3D rendering (use with caution)" );
	r_lighting_extended = Cvar_Get( "r_lighting_extended", "1", FCVAR_ARCHIVE, "allow to get lighting from bmodels too" );
	r_lighting_modulate = Cvar_Get( "r_lighting_modulate", "0.6", FCVAR_ARCHIVE, "lightstyles modulate scale" );
	r_lighting_ambient = Cvar_Get( "r_lighting_ambient", "0.3", FCVAR_ARCHIVE, "map ambient lighting scale" );
	r_adjust_fov = Cvar_Get( "r_adjust_fov", "1", FCVAR_ARCHIVE, "making FOV adjustment for wide-screens" );
	r_novis = Cvar_Get( "r_novis", "0", 0, "ignore vis information (perfomance test)" );
	r_nocull = Cvar_Get( "r_nocull", "0", 0, "ignore frustrum culling (perfomance test)" );
	r_detailtextures = Cvar_Get( "r_detailtextures", "1", FCVAR_ARCHIVE, "enable detail textures support, use '2' for autogenerate detail.txt" );
	r_lockpvs = Cvar_Get( "r_lockpvs", "0", FCVAR_CHEAT, "lockpvs area at current point (pvs test)" );
	r_lockfrustum = Cvar_Get( "r_lockfrustum", "0", FCVAR_CHEAT, "lock frustrum area at current point (cull test)" );
	r_dynamic = Cvar_Get( "r_dynamic", "1", FCVAR_ARCHIVE, "allow dynamic lighting (dlights, lightstyles)" );
	r_traceglow = Cvar_Get( "r_traceglow", "1", FCVAR_ARCHIVE, "cull flares behind models" );
	r_lightmap = Cvar_Get( "r_lightmap", "0", FCVAR_CHEAT, "lightmap debugging tool" );
	r_drawentities = Cvar_Get( "r_drawentities", "1", FCVAR_CHEAT|FCVAR_ARCHIVE, "render entities" );
	r_decals = Cvar_Get( "r_decals", "4096", FCVAR_ARCHIVE, "sets the maximum number of decals" );
	r_showtree = Cvar_Get( "r_showtree", "0", FCVAR_ARCHIVE, "build the graph of visible BSP tree" );
	window_xpos = Cvar_Get( "_window_xpos", "130", FCVAR_RENDERINFO, "window position by horizontal" );
	window_ypos = Cvar_Get( "_window_ypos", "48", FCVAR_RENDERINFO, "window position by vertical" );

	gl_extensions = Cvar_Get( "gl_allow_extensions", "1", FCVAR_GLCONFIG, "allow gl_extensions" );			
	gl_wgl_msaa_samples = Cvar_Get( "gl_wgl_msaa_samples", "4", FCVAR_GLCONFIG, "enable multisample anti-aliasing" );
	gl_texture_nearest = Cvar_Get( "gl_texture_nearest", "0", FCVAR_ARCHIVE, "disable texture filter" );
	gl_lightmap_nearest = Cvar_Get( "gl_lightmap_nearest", "0", FCVAR_ARCHIVE, "disable lightmap filter" );
	gl_check_errors = Cvar_Get( "gl_check_errors", "1", FCVAR_ARCHIVE, "ignore video engine errors" );
	gl_vsync = Cvar_Get( "gl_vsync", "0", FCVAR_ARCHIVE,  "enable vertical syncronization" );
	gl_detailscale = Cvar_Get( "gl_detailscale", "4.0", FCVAR_ARCHIVE, "default scale applies while auto-generate list of detail textures" );
	gl_texture_anisotropy = Cvar_Get( "gl_anisotropy", "8", FCVAR_ARCHIVE, "textures anisotropic filter" );
	gl_texture_lodbias =  Cvar_Get( "gl_texture_lodbias", "0.0", FCVAR_ARCHIVE, "LOD bias for mipmapped textures (perfomance|quality)" );
	gl_keeptjunctions = Cvar_Get( "gl_keeptjunctions", "1", FCVAR_ARCHIVE, "removing tjuncs causes blinking pixels" ); 
	gl_emboss_scale = Cvar_Get( "gl_emboss_scale", "0", FCVAR_ARCHIVE|FCVAR_LATCH, "fake bumpmapping scale" ); 
	gl_showtextures = Cvar_Get( "r_showtextures", "0", FCVAR_CHEAT, "show all uploaded textures" );
	gl_finish = Cvar_Get( "gl_finish", "0", FCVAR_ARCHIVE, "use glFinish instead of glFlush" );
	gl_nosort = Cvar_Get( "gl_nosort", "0", FCVAR_ARCHIVE, "disable sorting of translucent surfaces" );
	gl_clear = Cvar_Get( "gl_clear", "0", FCVAR_ARCHIVE, "clearing screen after each frame" );
	gl_test = Cvar_Get( "gl_test", "0", 0, "engine developer cvar for quick testing new features" );
	gl_wireframe = Cvar_Get( "gl_wireframe", "0", FCVAR_ARCHIVE|FCVAR_SPONLY, "show wireframe overlay" );
	gl_round_down = Cvar_Get( "gl_round_down", "2", FCVAR_RENDERINFO, "round texture sizes to nearest POT value" );
	gl_msaa = Cvar_Get( "gl_msaa", "1", FCVAR_ARCHIVE, "enable multi sample anti-aliasing" );

	// these cvar not used by engine but some mods requires this
	gl_polyoffset = Cvar_Get( "gl_polyoffset", "2.0", FCVAR_ARCHIVE, "polygon offset for decals" );
 
	// make sure gl_vsync is checked after vid_restart
	SetBits( gl_vsync->flags, FCVAR_CHANGED );

	vid_gamma = Cvar_Get( "gamma", "2.5", FCVAR_ARCHIVE, "gamma amount" );
	vid_brightness = Cvar_Get( "brightness", "0.0", FCVAR_ARCHIVE, "brighntess factor" );
	vid_mode = Cvar_Get( "vid_mode", VID_AUTOMODE, FCVAR_RENDERINFO|FCVAR_VIDRESTART, "display resolution mode" );
	vid_fullscreen = Cvar_Get( "fullscreen", "0", FCVAR_RENDERINFO|FCVAR_VIDRESTART, "enable fullscreen mode" );
	vid_displayfrequency = Cvar_Get ( "vid_displayfrequency", "0", FCVAR_RENDERINFO|FCVAR_VIDRESTART, "fullscreen refresh rate" );

	Cmd_AddCommand( "r_info", R_RenderInfo_f, "display renderer info" );

	// give initial OpenGL configuration
	host.apply_opengl_config = true;
	Cbuf_AddText( "exec opengl.cfg\n" );
	Cbuf_Execute();
	host.apply_opengl_config = false;

	// apply actual video mode to window
	Cbuf_AddText( "exec video.cfg\n" );
	Cbuf_Execute();
}

/*
=================
GL_RemoveCommands
=================
*/
void GL_RemoveCommands( void )
{
	Cmd_RemoveCommand( "r_info");
}

/*
=================
GL_InitExtensions
=================
*/
void GL_InitExtensions( void )
{
	// initialize gl extensions
	GL_CheckExtension( "OpenGL 1.1.0", opengl_110funcs, NULL, GL_OPENGL_110 );

	// get our various GL strings
	glConfig.vendor_string = pglGetString( GL_VENDOR );
	glConfig.renderer_string = pglGetString( GL_RENDERER );
	glConfig.version_string = pglGetString( GL_VERSION );
	glConfig.extensions_string = pglGetString( GL_EXTENSIONS );
	Con_Printf( "^3Video:^7 %s\n", glConfig.renderer_string );

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

	// initalize until base opengl functions loaded (old-context)
	if( !context_flags && !CVAR_TO_BOOL( gl_wgl_msaa_samples ))
		GL_CheckExtension( "OpenGL Internal ProcAddress", wglproc_funcs, NULL, GL_WGL_PROCADDRESS );

	// windows-specific extensions
	GL_CheckExtension( "WGL Extensions String", wglgetextensionsstring, NULL, GL_WGL_EXTENSIONS );

	if( pwglGetExtensionsStringEXT != NULL )
		glConfig.wgl_extensions_string = pwglGetExtensionsStringEXT();
	else glConfig.wgl_extensions_string = NULL;

	// initalize until base opengl functions loaded
	GL_CheckExtension( "WGL_EXT_swap_control", wglswapintervalfuncs, NULL, GL_WGL_SWAPCONTROL );

	// multitexture
	glConfig.max_texture_units = glConfig.max_texture_coords = glConfig.max_teximage_units = 1;
	GL_CheckExtension( "GL_ARB_multitexture", multitexturefuncs, "gl_arb_multitexture", GL_ARB_MULTITEXTURE );

	if( GL_Support( GL_ARB_MULTITEXTURE ))
		pglGetIntegerv( GL_MAX_TEXTURE_UNITS_ARB, &glConfig.max_texture_units );

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
	GL_CheckExtension( "GL_ARB_texture_compression", texturecompressionfuncs, "gl_texture_dxt_compression", GL_TEXTURE_COMPRESSION_EXT );
	GL_CheckExtension( "GL_EXT_texture_edge_clamp", NULL, NULL, GL_CLAMPTOEDGE_EXT );

	if( !GL_Support( GL_CLAMPTOEDGE_EXT ))
		GL_CheckExtension( "GL_SGIS_texture_edge_clamp", NULL, NULL, GL_CLAMPTOEDGE_EXT );

	glConfig.max_texture_anisotropy = 0.0f;
	GL_CheckExtension( "GL_EXT_texture_filter_anisotropic", NULL, "gl_texture_anisotropic_filter", GL_ANISOTROPY_EXT );

	if( GL_Support( GL_ANISOTROPY_EXT ))
		pglGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &glConfig.max_texture_anisotropy );

	// g-cont. because lodbias it too glitchy on Intel's cards
	if( glConfig.hardware_type != GLHW_INTEL )
		GL_CheckExtension( "GL_EXT_texture_lod_bias", NULL, "gl_texture_mipmap_biasing", GL_TEXTURE_LOD_BIAS );

	if( GL_Support( GL_TEXTURE_LOD_BIAS ))
		pglGetFloatv( GL_MAX_TEXTURE_LOD_BIAS_EXT, &glConfig.max_texture_lod_bias );

	GL_CheckExtension( "GL_ARB_texture_border_clamp", NULL, NULL, GL_CLAMP_TEXBORDER_EXT );

	GL_CheckExtension( "GL_ARB_depth_texture", NULL, NULL, GL_DEPTH_TEXTURE );
	GL_CheckExtension( "GL_ARB_texture_float", NULL, "gl_texture_float", GL_ARB_TEXTURE_FLOAT_EXT );
	GL_CheckExtension( "GL_ARB_depth_buffer_float", NULL, "gl_texture_depth_float", GL_ARB_DEPTH_FLOAT_EXT );
	GL_CheckExtension( "GL_EXT_gpu_shader4", NULL, NULL, GL_EXT_GPU_SHADER4 ); // don't confuse users
	GL_CheckExtension( "GL_ARB_shading_language_100", NULL, NULL, GL_SHADER_GLSL100_EXT );

	// this won't work without extended context
	if( glw_state.extended )
		GL_CheckExtension( "GL_ARB_debug_output", debugoutputfuncs, "gl_debug_output", GL_DEBUG_OUTPUT );

	// rectangle textures support
	GL_CheckExtension( "GL_ARB_texture_rectangle", NULL, "gl_texture_rectangle", GL_TEXTURE_2D_RECT_EXT );

	if( GL_Support( GL_SHADER_GLSL100_EXT ))
	{
		pglGetIntegerv( GL_MAX_TEXTURE_COORDS_ARB, &glConfig.max_texture_coords );
		pglGetIntegerv( GL_MAX_TEXTURE_IMAGE_UNITS_ARB, &glConfig.max_teximage_units );

		// check for hardware skinning
		pglGetIntegerv( GL_MAX_VERTEX_UNIFORM_COMPONENTS_ARB, &glConfig.max_vertex_uniforms );
		pglGetIntegerv( GL_MAX_VERTEX_ATTRIBS_ARB, &glConfig.max_vertex_attribs );

		if( glConfig.hardware_type == GLHW_RADEON && glConfig.max_vertex_uniforms > 512 )
			glConfig.max_vertex_uniforms /= 4; // radeon returns not correct info
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

	Cvar_Get( "gl_max_size", va( "%i", glConfig.max_2d_texture_size ), 0, "opengl texture max dims" );

	// MCD has buffering issues
	if( Q_stristr( glConfig.renderer_string, "gdi" ))
		Cvar_SetValue( "gl_finish", 1 );

	Cvar_Set( "gl_anisotropy", va( "%f", bound( 0, gl_texture_anisotropy->value, glConfig.max_texture_anisotropy )));

	if( GL_Support( GL_TEXTURE_COMPRESSION_EXT ))
		Image_AddCmdFlags( IL_DDS_HARDWARE );

	// enable gldebug if allowed
	if( GL_Support( GL_DEBUG_OUTPUT ))
	{
		pglDebugMessageCallbackARB( GL_DebugOutput, NULL );

		// force everything to happen in the main thread instead of in a separate driver thread
		pglEnable( GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB );

		// enable all the low priority messages
		pglDebugMessageControlARB( GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_LOW_ARB, 0, NULL, true );
	}

	tr.framecount = tr.visframecount = 1;
	glw_state.initialized = true;
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
	if( !R_Init_OpenGL( ))
	{
		GL_RemoveCommands();
		R_Free_OpenGL();

		Sys_Error( "Can't initialize video subsystem\nProbably driver was not installed" );
		return false;
	}

	host.renderinfo_changed = false;
	r_temppool = Mem_AllocPool( "Render Zone" );

	GL_InitExtensions();
	GL_SetDefaults();
	R_InitImages();
	R_SpriteInit();
	R_StudioInit();
	R_AliasInit();
	R_ClearDecals();
	R_ClearScene();

	// initialize screen
	SCR_Init();

	return true;
}

/*
===============
R_Shutdown
===============
*/
void R_Shutdown( void )
{
	model_t	*mod;
	int	i;

	if( !glw_state.initialized )
		return;

	// release SpriteTextures
	for( i = 1, mod = clgame.sprites; i < MAX_CLIENT_SPRITES; i++, mod++ )
	{
		if( !mod->name[0] ) continue;
		Mod_UnloadSpriteModel( mod );
	}
	memset( clgame.sprites, 0, sizeof( clgame.sprites ));

	GL_RemoveCommands();
	R_ShutdownImages();

	Mem_FreePool( &r_temppool );

	// shut down OS specific OpenGL stuff like contexts, etc.
	R_Free_OpenGL();
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

	Con_Printf( S_OPENGL_ERROR "%s (called at %s:%i)\n", GL_ErrorString( err ), filename, fileline );
}