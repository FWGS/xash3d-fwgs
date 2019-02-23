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
#include <SDL.h>
#include "common.h"
#include "client.h"
#include "mod_local.h"
#include "input.h"
#include "vid_common.h"
#include "platform/sdl/events.h"

static vidmode_t *vidmodes = NULL;
static int num_vidmodes = 0;
static int context_flags = 0;
static void GL_SetupAttributes( void );

int R_MaxVideoModes( void )
{
	return num_vidmodes;
}

vidmode_t *R_GetVideoMode( int num )
{
	if( !vidmodes || num < 0 || num >= R_MaxVideoModes() )
	{
		return NULL;
	}

	return vidmodes + num;
}

static void R_InitVideoModes( void )
{
	int displayIndex = 0; // TODO: handle multiple displays somehow
	int i, modes;

	num_vidmodes = 0;
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
				Con_Reportf( "SetDPIAwareness: Success\n" );
				bSuccess = TRUE;
			}
			else if( hResult == E_INVALIDARG ) Con_Reportf( "SetDPIAwareness: Invalid argument\n" );
			else if( hResult == E_ACCESSDENIED ) Con_Reportf( "SetDPIAwareness: Access Denied\n" );
		}
		else Con_Reportf( "SetDPIAwareness: Can't get SetProcessDpiAwareness\n" );
		FreeLibrary( hModule );
	}
	else Con_Reportf( "SetDPIAwareness: Can't load shcore.dll\n" );


	if( !bSuccess )
	{
		Con_Reportf( "SetDPIAwareness: Trying SetProcessDPIAware...\n" );

		if( ( hModule = LoadLibrary( "user32.dll" ) ) )
		{
			if( ( pSetProcessDPIAware = ( void* )GetProcAddress( hModule, "SetProcessDPIAware" ) ) )
			{
				// I hope SDL don't handle WM_DPICHANGED message
				BOOL hResult = pSetProcessDPIAware();

				if( hResult )
				{
					Con_Reportf( "SetDPIAwareness: Success\n" );
					bSuccess = TRUE;
				}
				else Con_Reportf( "SetDPIAwareness: fail\n" );
			}
			else Con_Reportf( "SetDPIAwareness: Can't get SetProcessDPIAware\n" );
			FreeLibrary( hModule );
		}
		else Con_Reportf( "SetDPIAwareness: Can't load user32.dll\n" );
	}
}
#endif


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
		Con_Reportf( S_ERROR  "Error: GL_GetProcAddress failed for %s\n", name );
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
		SDL_GL_SetSwapInterval( gl_vsync->value );
		SetBits( gl_vsync->flags, FCVAR_CHANGED );
	}
	else if( FBitSet( gl_vsync->flags, FCVAR_CHANGED ))
	{
		ClearBits( gl_vsync->flags, FCVAR_CHANGED );

		if( SDL_GL_SetSwapInterval( gl_vsync->value ) )
			Con_Reportf( S_ERROR  "SDL_GL_SetSwapInterval: %s\n", SDL_GetError( ) );
	}
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
		Con_Reportf( S_ERROR "GL_CreateContext: %s\n", SDL_GetError());
		return GL_DeleteContext();
	}

	SDL_GL_GetAttribute( SDL_GL_RED_SIZE, &colorBits[0] );
	SDL_GL_GetAttribute( SDL_GL_GREEN_SIZE, &colorBits[1] );
	SDL_GL_GetAttribute( SDL_GL_BLUE_SIZE, &colorBits[2] );
	glContext.color_bits = colorBits[0] + colorBits[1] + colorBits[2];

	SDL_GL_GetAttribute( SDL_GL_ALPHA_SIZE, &glContext.alpha_bits );
	SDL_GL_GetAttribute( SDL_GL_DEPTH_SIZE, &glContext.depth_bits );
	SDL_GL_GetAttribute( SDL_GL_STENCIL_SIZE, &glContext.stencil_bits );
	/// move to ref
	//vidState.stencilEnabled = glContext.stencil_bits ? true : false;

	SDL_GL_GetAttribute( SDL_GL_MULTISAMPLESAMPLES, &glContext.msaasamples );

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
		Con_Reportf( S_ERROR "GL_UpdateContext: %s\n", SDL_GetError());
		return GL_DeleteContext();
	}

	return true;
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

	Con_Reportf( "Got closest display mode: %ix%i@%i\n", got.w, got.h, got.refresh_rate);

	if( SDL_SetWindowDisplayMode( host.hWnd, &got) == -1 )
		return false;

	if( SDL_SetWindowFullscreen( host.hWnd, SDL_WINDOW_FULLSCREEN) == -1 )
		return false;

	SDL_SetWindowBordered( host.hWnd, SDL_FALSE );
	//SDL_SetWindowPosition( host.hWnd, 0, 0 );
	SDL_SetWindowGrab( host.hWnd, SDL_TRUE );
	SDL_SetWindowSize( host.hWnd, got.w, got.h );

	SDL_GL_GetDrawableSize( host.hWnd, &got.w, &got.h );

	R_SaveVideoMode( got.w, got.h );
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
		xpos = Cvar_VariableInteger( "_window_xpos" );
		ypos = Cvar_VariableInteger( "_window_ypos" );
		if( xpos < 0 ) xpos = SDL_WINDOWPOS_CENTERED;
		if( ypos < 0 ) ypos = SDL_WINDOWPOS_CENTERED;
	}
	else
	{
		wndFlags |= SDL_WINDOW_FULLSCREEN | SDL_WINDOW_BORDERLESS | SDL_WINDOW_INPUT_GRABBED;
		xpos = ypos = 0;
	}

	host.hWnd = SDL_CreateWindow( wndname, xpos, ypos, width, height, wndFlags );

	if( !host.hWnd )
	{
		Con_Reportf( S_ERROR "VID_CreateWindow: couldn't create '%s': %s\n", wndname, SDL_GetError());

		// skip some attribs in hope that context creating will not fail
		if( glw_state.safe >= SAFE_NO )
		{
			if( !gl_wgl_msaa_samples->value && glw_state.safe + 1 == SAFE_NOMSAA )
				glw_state.safe += 2; // no need to skip msaa, if we already disabled it
			else glw_state.safe++;
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
	R_SaveVideoMode( width, height );

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

	if( vidState.fullScreen )
	{
		vidState.fullScreen = false;
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
		Con_Reportf( "Creating an extended GL context for debug...\n" );
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

	if( glw_state.safe > SAFE_DONTCARE )
	{
		glw_state.safe = -1; // can't retry anymore, can only shutdown engine
		return;
	}

	Msg( "Trying safe opengl mode %d\n", glw_state.safe );

	if( glw_state.safe == SAFE_DONTCARE )
		return;

	SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );

	if( glw_state.safe < SAFE_NOACC )
		SDL_GL_SetAttribute( SDL_GL_ACCELERATED_VISUAL, 1 );

	Msg( "bpp %d\n", glw_state.desktopBitsPixel );

	/// ref context attribs api
//	if( glw_state.safe < SAFE_NOSTENCIL )
//		SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, gl_stencilbits->value );

	if( glw_state.safe < SAFE_NOALPHA )
		SDL_GL_SetAttribute( SDL_GL_ALPHA_SIZE, 8 );

	if( glw_state.safe < SAFE_NODEPTH )
		SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 24 );
	else
		SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 8 );

	if( glw_state.safe < SAFE_NOCOLOR )
	{
		if( glw_state.desktopBitsPixel >= 24 )
		{
			SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 8 );
			SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 8 );
			SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 8 );
		}
		else if( glw_state.desktopBitsPixel >= 16 )
		{
			SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 5 );
			SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 6 );
			SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 5 );
		}
		else
		{
			SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 3 );
			SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 3 );
			SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 2 );
		}
	}

	if( glw_state.safe < SAFE_NOMSAA )
	{
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
			SDL_GL_SetAttribute( SDL_GL_MULTISAMPLEBUFFERS, 1 );
			SDL_GL_SetAttribute( SDL_GL_MULTISAMPLESAMPLES, samples );

			glContext.max_multisamples = samples;
		}
		else
		{
			SDL_GL_SetAttribute( SDL_GL_MULTISAMPLEBUFFERS, 0 );
			SDL_GL_SetAttribute( SDL_GL_MULTISAMPLESAMPLES, 0 );

			glContext.max_multisamples = 0;
		}
	}
	else
	{
		Cvar_Set( "gl_wgl_msaa_samples", "0" );
	}
}

#ifndef EGL_LIB
#define EGL_LIB NULL
#endif

/*
==================
R_Init_Video
==================
*/
qboolean R_Init_Video( void )
{
	SDL_DisplayMode displayMode;
	string safe;
	qboolean retval;

	SDL_GetCurrentDisplayMode(0, &displayMode);
	glw_state.desktopBitsPixel = SDL_BITSPERPIXEL(displayMode.format);
	glw_state.desktopWidth = displayMode.w;
	glw_state.desktopHeight = displayMode.h;

	if( !glw_state.safe && Sys_GetParmFromCmdLine( "-safegl", safe ) )
		glw_state.safe = bound( SAFE_NO, Q_atoi( safe ), SAFE_DONTCARE );

	GL_SetupAttributes();

	if( SDL_GL_LoadLibrary( EGL_LIB ) )
	{
		Con_Reportf( S_ERROR  "Couldn't initialize OpenGL: %s\n", SDL_GetError());
		return false;
	}

	R_InitVideoModes();

	// must be initialized before creating window
#ifdef _WIN32
	WIN_SetDPIAwareness();
#endif

	if( !(retval = VID_SetMode()) )
	{
		return retval;
	}

	ref.dllFuncs.GL_InitExtensions();

	return true;
}

rserr_t R_ChangeDisplaySettings( int width, int height, qboolean fullscreen )
{
	SDL_DisplayMode displayMode;

	SDL_GetCurrentDisplayMode( 0, &displayMode );

	Con_Reportf( "R_ChangeDisplaySettings: Setting video mode to %dx%d %s\n", width, height, fullscreen ? "fullscreen" : "windowed" );

	// check our desktop attributes
	glw_state.desktopBitsPixel = SDL_BITSPERPIXEL( displayMode.format );
	glw_state.desktopWidth = displayMode.w;
	glw_state.desktopHeight = displayMode.h;

	vidState.fullScreen = fullscreen;

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
		R_SaveVideoMode( width, height );
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
		vidState.prev_width = iScreenWidth;
		vidState.prev_height = iScreenHeight;
	}
	else
	{
		if( err == rserr_invalid_fullscreen )
		{
			Cvar_SetValue( "fullscreen", 0 );
			Con_Reportf( S_ERROR  "VID_SetMode: fullscreen unavailable in this mode\n" );
			Sys_Warn("fullscreen unavailable in this mode!");
			if(( err = R_ChangeDisplaySettings( iScreenWidth, iScreenHeight, false )) == rserr_ok )
				return true;
		}
		else if( err == rserr_invalid_mode )
		{
			Con_Reportf( S_ERROR  "VID_SetMode: invalid mode\n" );
			Sys_Warn( "invalid mode" );
		}

		// try setting it back to something safe
		if(( err = R_ChangeDisplaySettings( vidState.prev_width, vidState.prev_height, false )) != rserr_ok )
		{
			Con_Reportf( S_ERROR  "VID_SetMode: could not revert to safe mode\n" );
			Sys_Warn("could not revert to safe mode!");
			return false;
		}
	}
	return true;
}

/*
==================
R_Free_Video
==================
*/
void R_Free_Video( void )
{
	GL_DeleteContext ();

	VID_DestroyWindow ();

	R_FreeVideoModes();

	ref.dllFuncs.GL_ClearExtensions();
}

#endif // XASH_DEDICATED
