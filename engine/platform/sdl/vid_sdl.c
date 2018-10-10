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
#ifndef XASH_DEDICATED

#include "common.h"
#include "client.h"
#include "gl_local.h"
#include "mod_local.h"
#include "input.h"
#include "vid_common.h"
#include <SDL.h>

static vidmode_t *vidmodes = NULL;
static int num_vidmodes = 0;
static int context_flags = 0;

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

static void GL_SetupAttributes( void );

int R_MaxVideoModes( void )
{
	return num_vidmodes;
}

vidmode_t R_GetVideoMode( int num )
{
	static vidmode_t error = { NULL };

	if( !vidmodes || num < 0 || num > R_MaxVideoModes() )
	{
		error.width = glState.width;
		error.height = glState.height;
		return error;
	}

	return vidmodes[num];
}

static void R_InitVideoModes( void )
{
	int displayIndex = 0; // TODO: handle multiple displays somehow
	int i, modes;

	modes = SDL_GetNumDisplayModes( displayIndex );

	if( !modes )
		return;

	vidmodes = Mem_Malloc( host.mempool, modes * sizeof( vidmode_t ) );

	for( i = 0; i < modes; i++ )
	{
		int j;
		qboolean skip = false;
		SDL_DisplayMode mode;

		if( SDL_GetDisplayMode( displayIndex, i, &mode ) )
		{
			Msg( "SDL_GetDisplayMode: %s\n", SDL_GetError() );
			continue;
		}

		if( mode.w < VID_MIN_WIDTH || mode.h < VID_MIN_HEIGHT )
			continue;

		for( j = 0; j < num_vidmodes; j++ )
		{
			if( mode.w == vidmodes[j].width &&
				mode.h == vidmodes[j].height )
			{
				skip = true;
				break;
			}
		}
		if( j != num_vidmodes )
			continue;

		vidmodes[num_vidmodes].width = mode.w;
		vidmodes[num_vidmodes].height = mode.h;
		vidmodes[num_vidmodes].desc = copystring( va( "%ix%i", mode.w, mode.h ));

		num_vidmodes++;
	}
}

static void R_FreeVideoModes( void )
{
	int i;

	for( i = 0; i < num_vidmodes; i++ )
		Mem_Free( (char*)vidmodes[i].desc );
	Mem_Free( vidmodes );

	vidmodes = NULL;
}

#ifdef WIN32
typedef enum _XASH_DPI_AWARENESS
{
	XASH_DPI_UNAWARE = 0,
	XASH_SYSTEM_DPI_AWARE = 1,
	XASH_PER_MONITOR_DPI_AWARE = 2
} XASH_DPI_AWARENESS;

static void WIN_SetDPIAwareness( void )
{
	HMODULE hModule;
	HRESULT ( __stdcall *pSetProcessDpiAwareness )( XASH_DPI_AWARENESS );
	BOOL ( __stdcall *pSetProcessDPIAware )( void );
	BOOL bSuccess = FALSE;

	if( ( hModule = LoadLibrary( "shcore.dll" ) ) )
	{
		if( ( pSetProcessDpiAwareness = (void*)GetProcAddress( hModule, "SetProcessDpiAwareness" ) ) )
		{
			// I hope SDL don't handle WM_DPICHANGED message
			HRESULT hResult = pSetProcessDpiAwareness( XASH_SYSTEM_DPI_AWARE );

			if( hResult == S_OK )
			{
				MsgDev( D_NOTE, "SetDPIAwareness: Success\n" );
				bSuccess = TRUE;
			}
			else if( hResult == E_INVALIDARG ) MsgDev( D_NOTE, "SetDPIAwareness: Invalid argument\n" );
			else if( hResult == E_ACCESSDENIED ) MsgDev( D_NOTE, "SetDPIAwareness: Access Denied\n" );
		}
		else MsgDev( D_NOTE, "SetDPIAwareness: Can't get SetProcessDpiAwareness\n" );
		FreeLibrary( hModule );
	}
	else MsgDev( D_NOTE, "SetDPIAwareness: Can't load shcore.dll\n" );


	if( !bSuccess )
	{
		MsgDev( D_NOTE, "SetDPIAwareness: Trying SetProcessDPIAware...\n" );

		if( ( hModule = LoadLibrary( "user32.dll" ) ) )
		{
			if( ( pSetProcessDPIAware = ( void* )GetProcAddress( hModule, "SetProcessDPIAware" ) ) )
			{
				// I hope SDL don't handle WM_DPICHANGED message
				BOOL hResult = pSetProcessDPIAware();

				if( hResult )
				{
					MsgDev( D_NOTE, "SetDPIAwareness: Success\n" );
					bSuccess = TRUE;
				}
				else MsgDev( D_NOTE, "SetDPIAwareness: fail\n" );
			}
			else MsgDev( D_NOTE, "SetDPIAwareness: Can't get SetProcessDPIAware\n" );
			FreeLibrary( hModule );
		}
		else MsgDev( D_NOTE, "SetDPIAwareness: Can't load user32.dll\n" );
	}
}
#endif

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

/*
=================
GL_GetProcAddress
=================
*/
void *GL_GetProcAddress( const char *name )
{
#if defined( XASH_NANOGL )
	void *func = nanoGL_GetProcAddress(name);
#else
	void *func = SDL_GL_GetProcAddress(name);
#endif

	if( !func )
	{
		MsgDev( D_ERROR, "Error: GL_GetProcAddress failed for %s\n", name );
	}

	return func;
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
		if( SDL_GL_SetSwapInterval( gl_vsync->value ) )
			MsgDev( D_ERROR, "SDL_GL_SetSwapInterval: %s\n", SDL_GetError( ) );
		SetBits( gl_vsync->flags, FCVAR_CHANGED );
	}
	else if( FBitSet( gl_vsync->flags, FCVAR_CHANGED ))
	{
		ClearBits( gl_vsync->flags, FCVAR_CHANGED );

		if( SDL_GL_SetSwapInterval( gl_vsync->value ) )
			MsgDev( D_ERROR, "SDL_GL_SetSwapInterval: %s\n", SDL_GetError( ) );
	}
}

/*
=================
GL_CreateContext
=================
*/
qboolean GL_CreateContext( void )
{
	int colorBits[3];
#ifdef XASH_NANOGL
	nanoGL_Init();
#endif

	if( ( glw_state.context = SDL_GL_CreateContext( host.hWnd ) ) == NULL)
	{
		MsgDev(D_ERROR, "GL_CreateContext: %s\n", SDL_GetError());
		return GL_DeleteContext();
	}

	SDL_GL_GetAttribute( SDL_GL_RED_SIZE, &colorBits[0] );
	SDL_GL_GetAttribute( SDL_GL_GREEN_SIZE, &colorBits[1] );
	SDL_GL_GetAttribute( SDL_GL_BLUE_SIZE, &colorBits[2] );
	glConfig.color_bits = colorBits[0] + colorBits[1] + colorBits[2];

	SDL_GL_GetAttribute( SDL_GL_ALPHA_SIZE, &glConfig.alpha_bits );
	SDL_GL_GetAttribute( SDL_GL_DEPTH_SIZE, &glConfig.depth_bits );
	SDL_GL_GetAttribute( SDL_GL_STENCIL_SIZE, &glConfig.stencil_bits );
	glState.stencilEnabled = glConfig.stencil_bits ? true : false;

	SDL_GL_GetAttribute( SDL_GL_MULTISAMPLESAMPLES, &glConfig.msaasamples );

#ifdef XASH_WES
	void wes_init();
	wes_init();
#endif

	return true;
}

/*
=================
GL_UpdateContext
=================
*/
qboolean GL_UpdateContext( void )
{
	if( SDL_GL_MakeCurrent( host.hWnd, glw_state.context ))
	{
		MsgDev(D_ERROR, "GL_UpdateContext: %s\n", SDL_GetError());
		return GL_DeleteContext();
	}

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
	if( glw_state.context )
	{
		SDL_GL_DeleteContext(glw_state.context);
		glw_state.context = NULL;
	}

	return false;
}

qboolean VID_SetScreenResolution( int width, int height )
{
	SDL_DisplayMode want, got;
	Uint32 wndFlags = 0;
	static string wndname;

	if( vid_highdpi->value ) wndFlags |= SDL_WINDOW_ALLOW_HIGHDPI;
	Q_strncpy( wndname, GI->title, sizeof( wndname ));

	want.w = width;
	want.h = height;
	want.driverdata = NULL;
	want.format = want.refresh_rate = 0; // don't care

	if( !SDL_GetClosestDisplayMode(0, &want, &got) )
		return false;

	MsgDev(D_NOTE, "Got closest display mode: %ix%i@%i\n", got.w, got.h, got.refresh_rate);

	if( SDL_SetWindowDisplayMode( host.hWnd, &got) == -1 )
		return false;

	if( SDL_SetWindowFullscreen( host.hWnd, SDL_WINDOW_FULLSCREEN) == -1 )
		return false;

	SDL_SetWindowBordered( host.hWnd, SDL_FALSE );
	//SDL_SetWindowPosition( host.hWnd, 0, 0 );
	SDL_SetWindowGrab( host.hWnd, SDL_TRUE );
	SDL_SetWindowSize( host.hWnd, got.w, got.h );

	SDL_GL_GetDrawableSize( host.hWnd, &got.w, &got.h );

	R_ChangeDisplaySettingsFast( got.w, got.h );
	return true;
}

void VID_RestoreScreenResolution( void )
{
	if( !Cvar_VariableInteger("fullscreen") )
	{
		SDL_SetWindowBordered( host.hWnd, SDL_TRUE );
		SDL_SetWindowGrab( host.hWnd, SDL_FALSE );
	}
	else
	{
		SDL_MinimizeWindow( host.hWnd );
		SDL_SetWindowFullscreen( host.hWnd, 0 );
	}
}

#if defined(_WIN32) && !defined(XASH_64BIT) // ICO support only for Win32
#include "SDL_syswm.h"
static void WIN_SetWindowIcon( HICON ico )
{
	SDL_SysWMinfo wminfo;

	if( !ico )
		return;

	if( SDL_GetWindowWMInfo( host.hWnd, &wminfo ) )
	{
		SetClassLong( wminfo.info.win.window, GCL_HICON, (LONG)ico );
	}
}
#endif

/*
=================
VID_CreateWindow
=================
*/
qboolean VID_CreateWindow( int width, int height, qboolean fullscreen )
{
	static string	wndname;
	Uint32 wndFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_MOUSE_FOCUS;
	rgbdata_t *icon = NULL;
	qboolean iconLoaded = false;
	char iconpath[MAX_STRING];
	int xpos, ypos;

	if( vid_highdpi->value ) wndFlags |= SDL_WINDOW_ALLOW_HIGHDPI;
	Q_strncpy( wndname, GI->title, sizeof( wndname ));

	if( !fullscreen )
	{
		wndFlags |= SDL_WINDOW_RESIZABLE;
		xpos = max( 0, Cvar_VariableInteger( "_window_xpos" ) );
		ypos = max( 0, Cvar_VariableInteger( "_window_ypos" ) );
	}
	else
	{
		wndFlags |= SDL_WINDOW_FULLSCREEN | SDL_WINDOW_BORDERLESS | SDL_WINDOW_INPUT_GRABBED;
		xpos = ypos = 0;
	}

	host.hWnd = SDL_CreateWindow( wndname, xpos, ypos, width, height, wndFlags );

	if( !host.hWnd )
	{
		MsgDev( D_ERROR, "VID_CreateWindow: couldn't create '%s': %s\n", wndname, SDL_GetError());

		// remove MSAA, if it present, because
		// window creating may fail on GLX visual choose
		if( gl_wgl_msaa_samples->value || glw_state.safe >= 0 )
		{
			Cvar_Set( "gl_wgl_msaa_samples", "0" );
			glw_state.safe++;
			GL_SetupAttributes(); // re-choose attributes

			// try again
			return VID_CreateWindow( width, height, fullscreen );
		}
		return false;
	}

	if( fullscreen )
	{
		if( !VID_SetScreenResolution( width, height ) )
		{
			return false;
		}
	}
	else
	{
		VID_RestoreScreenResolution();
	}

#if defined(_WIN32) && !defined(XASH_64BIT) // ICO support only for Win32
	if( FS_FileExists( GI->iconpath, true ) )
	{
		HICON ico;
		char	localPath[MAX_PATH];

		Q_snprintf( localPath, sizeof( localPath ), "%s/%s", GI->gamefolder, GI->iconpath );
		ico = (HICON)LoadImage( NULL, localPath, IMAGE_ICON, 0, 0, LR_LOADFROMFILE|LR_DEFAULTSIZE );

		if( ico )
		{
			iconLoaded = true;
			WIN_SetWindowIcon( ico );
		}
	}
#endif // _WIN32 && !XASH_64BIT

	if( !iconLoaded )
	{
		Q_strcpy( iconpath, GI->iconpath );
		COM_StripExtension( iconpath );
		COM_DefaultExtension( iconpath, ".tga" );

		icon = FS_LoadImage( iconpath, NULL, 0 );

		if( icon )
		{
			SDL_Surface *surface = SDL_CreateRGBSurfaceFrom( icon->buffer,
				icon->width, icon->height, 32, 4 * icon->width,
				0x000000ff, 0x0000ff00, 0x00ff0000,	0xff000000 );

			if( surface )
			{
				SDL_SetWindowIcon( host.hWnd, surface );
				SDL_FreeSurface( surface );
				iconLoaded = true;
			}

			FS_FreeImage( icon );
		}
	}

#if defined(_WIN32) && !defined(XASH_64BIT) // ICO support only for Win32
	if( !iconLoaded )
	{
		WIN_SetWindowIcon( LoadIcon( host.hInst, MAKEINTRESOURCE( 101 ) ) );
		iconLoaded = true;
	}
#endif

	SDL_ShowWindow( host.hWnd );
	if( !glw_state.initialized )
	{
		if( !GL_CreateContext( ))
			return false;

		VID_StartupGamma();
	}

	if( !GL_UpdateContext( ))
		return false;

	SDL_GL_GetDrawableSize( host.hWnd, &width, &height );
	R_ChangeDisplaySettingsFast( width, height );

	return true;
}

/*
=================
VID_DestroyWindow
=================
*/
void VID_DestroyWindow( void )
{
	GL_DeleteContext();

	VID_RestoreScreenResolution();
	if( host.hWnd )
	{
		SDL_DestroyWindow ( host.hWnd );
		host.hWnd = NULL;
	}

	if( glState.fullScreen )
	{
		glState.fullScreen = false;
	}
}

/*
==================
GL_SetupAttributes
==================
*/
static void GL_SetupAttributes( void )
{
	int samples;

#if !defined(_WIN32)
	SDL_SetHint( "SDL_VIDEO_X11_XRANDR", "1" );
	SDL_SetHint( "SDL_VIDEO_X11_XVIDMODE", "1" );
#endif

	SDL_GL_ResetAttributes();


#ifdef XASH_GLES
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES );
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_EGL, 1 );

#ifdef XASH_NANOGL
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 1 );
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 1 );
#elif defined( XASH_WES ) || defined( XASH_REGAL )
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 2 );
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 0 );
#endif

#else // GL1.x
#ifndef XASH_GL_STATIC
	if( Sys_CheckParm( "-gldebug" ) )
	{
		MsgDev( D_NOTE, "Creating an extended GL context for debug...\n" );
		SetBits( context_flags, FCONTEXT_DEBUG_ARB );
		SDL_GL_SetAttribute( SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG );
		glw_state.extended = true;
	}
#endif // XASH_GL_STATIC
	if( Sys_CheckParm( "-glcore" ))
	{
		SetBits( context_flags, FCONTEXT_CORE_PROFILE );

		SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE );
	}
	else
	{
		SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY );
	}
#endif // XASH_GLES

	SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );

	if( glw_state.safe > SAFE_DONTCARE )
	{
		glw_state.safe = -1;
		return;
	}

	if( glw_state.safe > SAFE_NO )
		Msg("Trying safe opengl mode %d\n", glw_state.safe );

	if( glw_state.safe >= SAFE_NOACC )
		SDL_GL_SetAttribute( SDL_GL_ACCELERATED_VISUAL, 1 );

	Msg ("bpp %d\n", glw_state.desktopBitsPixel );

	if( glw_state.safe < SAFE_NODEPTH )
		SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 24 );
	else if( glw_state.safe < 5 )
		SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 8 );


	if( glw_state.safe < SAFE_NOATTRIB )
	{
		if( glw_state.desktopBitsPixel >= 24 )
		{
			if( glw_state.desktopBitsPixel == 32 )
				SDL_GL_SetAttribute( SDL_GL_ALPHA_SIZE, 8 );

			SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 8 );
			SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 8 );
			SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 8 );
		}
		else
		{
			SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 5 );
			SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 6 );
			SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 5 );
		}
	}

	if( glw_state.safe >= SAFE_DONTCARE )
		return;

	SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, gl_stencilbits->value );

	switch( (int)gl_wgl_msaa_samples->value )
	{
	case 2:
	case 4:
	case 8:
	case 16:
		samples = gl_wgl_msaa_samples->value;
		break;
	default:
		samples = 0; // don't use, because invalid parameter is passed
	}

	if( samples )
	{
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, samples);

		glConfig.max_multisamples = samples;
	}
	else
	{
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);

		glConfig.max_multisamples = 0;
	}
}

#ifndef EGL_LIB
#define EGL_LIB NULL
#endif

/*
==================
R_Init_OpenGL
==================
*/
qboolean R_Init_OpenGL( void )
{
	SDL_DisplayMode displayMode;
	string safe;

	SDL_GetCurrentDisplayMode(0, &displayMode);
	glw_state.desktopBitsPixel = SDL_BITSPERPIXEL(displayMode.format);
	glw_state.desktopWidth = displayMode.w;
	glw_state.desktopHeight = displayMode.h;

	if( !glw_state.safe && Sys_GetParmFromCmdLine( "-safegl", safe ) )
	{
		glw_state.safe = Q_atoi( safe );
		if( glw_state.safe < SAFE_NOACC || glw_state.safe > SAFE_DONTCARE  )
			glw_state.safe = SAFE_DONTCARE;
	}

	if( glw_state.safe < SAFE_NO || glw_state.safe > SAFE_DONTCARE  )
		return false;

	GL_SetupAttributes();

	if( SDL_GL_LoadLibrary( EGL_LIB ) )
	{
		MsgDev( D_ERROR, "Couldn't initialize OpenGL: %s\n", SDL_GetError());
		return false;
	}

	R_InitVideoModes();

	// must be initialized before creating window
#ifdef _WIN32
	WIN_SetDPIAwareness();
#endif

	return VID_SetMode();
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
			MsgDev( D_NOTE, "Installing GL_DebugOutput...\n");
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
	MsgDev( D_INFO, "^3Video^7: %s\n", glConfig.renderer_string );

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

/*
==================
R_ChangeDisplaySettingsFast

Change window size fastly to custom values, without setting vid mode
==================
*/
void R_ChangeDisplaySettingsFast( int width, int height )
{
	R_SaveVideoMode( width, height );

	SCR_VidInit();
}

rserr_t R_ChangeDisplaySettings( int width, int height, qboolean fullscreen )
{
	SDL_DisplayMode displayMode;

	SDL_GetCurrentDisplayMode( 0, &displayMode );

	MsgDev( D_INFO, "R_ChangeDisplaySettings: Setting video mode to %dx%d %s\n", width, height, fullscreen ? "fullscreen" : "windowed" );

	// check our desktop attributes
	glw_state.desktopBitsPixel = SDL_BITSPERPIXEL( displayMode.format );
	glw_state.desktopWidth = displayMode.w;
	glw_state.desktopHeight = displayMode.h;

	glState.fullScreen = fullscreen;

	if( !host.hWnd )
	{
		if( !VID_CreateWindow( width, height, fullscreen ) )
			return rserr_invalid_mode;
	}
	else if( fullscreen )
	{
		if( !VID_SetScreenResolution( width, height ) )
			return rserr_invalid_fullscreen;
	}
	else
	{
		VID_RestoreScreenResolution();
		if( SDL_SetWindowFullscreen( host.hWnd, 0 ) )
			return rserr_invalid_fullscreen;
		SDL_RestoreWindow( host.hWnd );
#if SDL_VERSION_ATLEAST( 2, 0, 5 )
		SDL_SetWindowResizable( host.hWnd, true );
#endif
		SDL_SetWindowBordered( host.hWnd, true );
		SDL_SetWindowSize( host.hWnd, width, height );
		SDL_GL_GetDrawableSize( host.hWnd, &width, &height );
		R_ChangeDisplaySettingsFast( width, height );
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
	qboolean	fullscreen = false;
	int iScreenWidth, iScreenHeight;
	rserr_t	err;

	iScreenWidth = Cvar_VariableInteger( "width" );
	iScreenHeight = Cvar_VariableInteger( "height" );

	if( iScreenWidth < VID_MIN_WIDTH ||
		iScreenHeight < VID_MIN_HEIGHT )	// trying to get resolution automatically by default
	{
#if !defined( DEFAULT_MODE_WIDTH ) || !defined( DEFAULT_MODE_HEIGHT )
		SDL_DisplayMode mode;

		SDL_GetDesktopDisplayMode( 0, &mode );

		iScreenWidth = mode.w;
		iScreenHeight = mode.h;
#else
		iScreenWidth = DEFAULT_MODE_WIDTH;
		iScreenHeight = DEFAULT_MODE_HEIGHT;
#endif

		if( !FBitSet( vid_fullscreen->flags, FCVAR_CHANGED ) )
			Cvar_SetValue( "fullscreen", DEFAULT_FULLSCREEN );
		else
			ClearBits( vid_fullscreen->flags, FCVAR_CHANGED );
	}

	SetBits( gl_vsync->flags, FCVAR_CHANGED );
	fullscreen = Cvar_VariableInteger("fullscreen") != 0;

	if(( err = R_ChangeDisplaySettings( iScreenWidth, iScreenHeight, fullscreen )) == rserr_ok )
	{
		glConfig.prev_width = iScreenWidth;
		glConfig.prev_height = iScreenHeight;
	}
	else
	{
		if( err == rserr_invalid_fullscreen )
		{
			Cvar_SetValue( "fullscreen", 0 );
			MsgDev( D_ERROR, "VID_SetMode: fullscreen unavailable in this mode\n" );
			Sys_Warn("fullscreen unavailable in this mode!");
			if(( err = R_ChangeDisplaySettings( iScreenWidth, iScreenHeight, false )) == rserr_ok )
				return true;
		}
		else if( err == rserr_invalid_mode )
		{
			MsgDev( D_ERROR, "VID_SetMode: invalid mode\n" );
			Sys_Warn( "invalid mode" );
		}

		// try setting it back to something safe
		if(( err = R_ChangeDisplaySettings( glConfig.prev_width, glConfig.prev_height, false )) != rserr_ok )
		{
			MsgDev( D_ERROR, "VID_SetMode: could not revert to safe mode\n" );
			Sys_Warn("could not revert to safe mode!");
			return false;
		}
	}
	return true;
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

	R_FreeVideoModes();

	// now all extensions are disabled
	memset( glConfig.extension, 0, sizeof( glConfig.extension ));
	glw_state.initialized = false;
}

#endif // XASH_DEDICATED
