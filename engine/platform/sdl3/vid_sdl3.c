/*
vid_sdl3 - SDL3 vid component
Copyright (C) 2025 Xash3D FWGS contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "xash3d_types.h"
#include "platform_sdl3.h"
#include "vid_common.h"
#include "ref_common.h"
#include "xash3d_mathlib.h"
#include "common.h"
#include <SDL3/SDL_system.h>

static struct
{
	int prev_width;
	int prev_height;

	vidmode_t *vidmodes;
	int num_vidmodes;
} vid_state = { 640, 480 };

static void GL_SetupAttributes( ref_safegl_context_t safegl )
{
	SDL_GL_ResetAttributes();

	ref.dllFuncs.GL_SetupAttributes( safegl );
}

static void VID_SetWindowIcon( SDL_Window *hWnd )
{
	char iconpath[MAX_STRING];

	Q_strncpy( iconpath, GI->iconpath, sizeof( iconpath ));
	COM_ReplaceExtension( iconpath, ".tga", sizeof( iconpath ));

	rgbdata_t *icon = FS_LoadImage( iconpath, NULL, 0 );

	if( !icon )
		return;

	SDL_Surface *surface = SDL_CreateSurfaceFrom( icon->width, icon->height, SDL_PIXELFORMAT_RGBA8888, icon->buffer, 4 * icon->width );

	FS_FreeImage( icon );

	if( !surface )
	{
		Con_PrintSDLError( "SDL_CreateSurfaceFrom" );
		return;
	}

	if( !SDL_SetWindowIcon( hWnd, surface ))
		Con_PrintSDLError( "SDL_SetWindowIcon" );

	SDL_DestroySurface( surface );
}

static SDL_Window *VID_CreateWindowWithSafeGL( const char *title, SDL_Rect *rect, Uint32 flags )
{
	for( ; glw_state.safe < SAFE_LAST; glw_state.safe++ )
	{
		if( glw_state.safe == SAFE_NOMSAA && !gl_msaa_samples.value )
			continue;

		// choose attributes with select safegl level
		GL_SetupAttributes( glw_state.safe );

		SDL_Window *hWnd = SDL_CreateWindow( title, rect->w, rect->h, flags );

		// stop if window creation was successful
		if( hWnd )
			return hWnd;

		Con_Printf( S_ERROR "%s: couldn't create '%s' with safegl level %d: %s\n", __func__, title, glw_state.safe, SDL_GetError( ));

		glw_state.safe++;
	}

	return NULL;
}

static void VID_SaveWindowSize( SDL_Window *hWnd, int width, int height )
{
	qboolean maximized = FBitSet( SDL_GetWindowFlags( hWnd ), SDL_WINDOW_MAXIMIZED );
	int render_w;
	int render_h;

	if( !SDL_GetWindowSizeInPixels( hWnd, &render_w, &render_h ))
	{
		render_w = width;
		render_h = height;

		Con_PrintSDLError( "SDL_GetWindowSizeInPixels" );
	}

	VID_SetDisplayTransform( &render_w, &render_h );
	R_SaveVideoMode( width, height, render_w, render_h, maximized );
}

static qboolean VID_CreateWindow( const int input_width, const int input_height, window_mode_t window_mode )
{
	SDL_Rect rect = { SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, input_width, input_height };
	Uint32 flags = SDL_WINDOW_RESIZABLE;

	if( !glw_state.software )
		SetBits( flags, SDL_WINDOW_OPENGL );

	// probe true fullscreen first, if it fails, try borderless next
	if( window_mode == WINDOW_MODE_FULLSCREEN )
	{
		SDL_DisplayID display = SDL_GetPrimaryDisplay();
		SDL_DisplayMode dm;

		if( SDL_GetClosestFullscreenDisplayMode( display, rect.w, rect.h, 0.0f, true, &dm ))
		{
			SetBits( flags, SDL_WINDOW_FULLSCREEN );
			rect.w = dm.w;
			rect.h = dm.h;
		}
		else
		{
			Con_PrintSDLError( "SDL_GetClosestFullscreenDisplayMode" );

			window_mode = WINDOW_MODE_BORDERLESS;
		}
	}

	// try borderless mode
	if( window_mode == WINDOW_MODE_BORDERLESS )
	{
		SDL_DisplayID display = SDL_GetPrimaryDisplay();
		const SDL_DisplayMode *dm;

		dm = SDL_GetDesktopDisplayMode( display );

		if( dm )
		{
			SetBits( flags, SDL_WINDOW_FULLSCREEN );
			rect.w = dm->w;
			rect.h = dm->h;
		}
		else
		{
			Con_PrintSDLError( "SDL_GetDesktopDisplayMode" );
			window_mode = WINDOW_MODE_WINDOWED;
		}
	}

	// finally, revert to windowed mode
	if( window_mode == WINDOW_MODE_WINDOWED )
	{
		// no-op
	}

	host.hWnd = VID_CreateWindowWithSafeGL( GI->title, &rect, flags );

	if( !host.hWnd )
		return false;

	VID_SetWindowIcon( host.hWnd );

	if( glw_state.software )
	{
		// no support yet
		return false;
	}
	else
	{
		glw_state.context = SDL_GL_CreateContext( host.hWnd );

		if( !glw_state.context )
		{
			Con_PrintSDLError( "SDL_GL_CreateContext" );
			return false;
		}

		if( !SDL_GL_MakeCurrent( host.hWnd, glw_state.context ))
		{
			Con_PrintSDLError( "SDL_GL_MakeCurrent" );
			SDL_GL_DestroyContext( glw_state.context );
			glw_state.context = NULL;

			return false;
		}
	}

	// update window size
	SDL_GetWindowSize( host.hWnd, &rect.w, &rect.h );
	VID_SaveWindowSize( host.hWnd, rect.w, rect.h );

	// update the window mode
	if( window_mode != WINDOW_MODE_WINDOWED )
		Cvar_DirectSetValue( &vid_fullscreen, SDL_GetWindowFullscreenMode( host.hWnd ) != NULL ? WINDOW_MODE_FULLSCREEN : WINDOW_MODE_BORDERLESS );

	return true;
}

static void R_InitVideoModes( void )
{
	SDL_DisplayID display = SDL_GetDisplayForWindow( host.hWnd );
	int count;
	SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes( display, &count );

	if( !modes )
	{
		Con_PrintSDLError( "SDL_GetFullscreenDisplayModes" );
		return;
	}

	vid_state.num_vidmodes = 0;
	vid_state.vidmodes = Mem_Malloc( host.mempool, count * sizeof( *vid_state.vidmodes ));

	for( int i = 0; i < count; i++ )
	{
		int w = modes[i]->w;
		int h = modes[i]->h;

		if( w < VID_MIN_WIDTH || h < VID_MIN_HEIGHT )
			continue;

		int j;

		for( j = 0; j < vid_state.num_vidmodes; j++ )
		{
			if( w == vid_state.vidmodes[j].width && h == vid_state.vidmodes[j].height )
				break;
		}

		if( j != vid_state.num_vidmodes )
			continue;

		j = vid_state.num_vidmodes++;

		vid_state.vidmodes[j].width = w;
		vid_state.vidmodes[j].height = h;

		char buf[MAX_VA_STRING];
		Q_snprintf( buf, sizeof( buf ), "%dx%d", w, h );

		vid_state.vidmodes[j].desc = copystring( buf );
	}

	SDL_free( modes );
}

void Platform_Minimize_f( void )
{
	if( host.hWnd )
		SDL_MinimizeWindow( host.hWnd );
}

void GL_UpdateSwapInterval( void )
{
	if( FBitSet( gl_vsync.flags, FCVAR_CHANGED ))
	{
		ClearBits( gl_vsync.flags, FCVAR_CHANGED );

		if( !SDL_GL_SetSwapInterval( gl_vsync.value ))
			Con_PrintSDLError( "SDL_GL_SetSwapInterval" );
	}
}

qboolean R_Init_Video( const int type )
{
	qboolean retval = false;

	if( Sys_CheckParm( "-egl" ))
		Con_Printf( S_WARN "%s: -egl option is deprecated\n", __func__ );

	switch( type )
	{
	case REF_SOFTWARE:
		glw_state.software = true;
		break;
	case REF_GL:
	{
		string safe;

		if( !glw_state.safe && Sys_GetParmFromCmdLine( "-safegl", safe ))
			glw_state.safe = bound( SAFE_NO, Q_atoi( safe ), SAFE_DONTCARE );

		if( !SDL_GL_LoadLibrary( NULL ))
		{
			Con_Reportf( S_ERROR "Couldn't initialize OpenGL: %s\n", SDL_GetError( ));
			return false;
		}

		break;
	}
	default:
		Host_Error( "Can't initialize unknown context type %d!\n", type );
		break;
	}

	retval = VID_SetMode();
	if( !retval )
		return false;

	switch( type )
	{
	case REF_SOFTWARE:
		break;
	case REF_GL:
		ref.dllFuncs.GL_InitExtensions();
		break;
	}

	R_InitVideoModes();

	host.renderinfo_changed = false;

	return true;
}

void R_Free_Video( void )
{
	SDL_GL_DestroyContext( glw_state.context );
	glw_state.context = NULL;

	SDL_DestroyWindow( host.hWnd );
	host.hWnd = NULL;

	refState.window_mode = WINDOW_MODE_WINDOWED;

	Mem_Free( vid_state.vidmodes );
	vid_state.vidmodes = NULL;
	vid_state.num_vidmodes = 0;

	ref.dllFuncs.GL_ClearExtensions();
}

int GL_SetAttribute( int attr, int val )
{
	switch( attr )
	{
#define MAP_REF_API_ATTRIBUTE_TO_SDL( name ) case REF_##name: return SDL_GL_SetAttribute( SDL_##name, val );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_RED_SIZE );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_GREEN_SIZE );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_BLUE_SIZE );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_ALPHA_SIZE );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_DOUBLEBUFFER );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_DEPTH_SIZE );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_STENCIL_SIZE );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_MULTISAMPLEBUFFERS );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_MULTISAMPLESAMPLES );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_ACCELERATED_VISUAL );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_CONTEXT_MAJOR_VERSION );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_CONTEXT_MINOR_VERSION );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_CONTEXT_FLAGS );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_SHARE_WITH_CURRENT_CONTEXT );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_FRAMEBUFFER_SRGB_CAPABLE );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_CONTEXT_PROFILE_MASK );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_CONTEXT_RELEASE_BEHAVIOR );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_CONTEXT_RESET_NOTIFICATION );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_CONTEXT_NO_ERROR );
#undef MAP_REF_API_ATTRIBUTE_TO_SDL
	}

	return -1;
}

int GL_GetAttribute( int attr, int *val )
{
	switch( attr )
	{
#define MAP_REF_API_ATTRIBUTE_TO_SDL( name ) case REF_##name: return SDL_GL_GetAttribute( SDL_##name, val );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_RED_SIZE );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_GREEN_SIZE );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_BLUE_SIZE );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_ALPHA_SIZE );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_DOUBLEBUFFER );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_DEPTH_SIZE );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_STENCIL_SIZE );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_MULTISAMPLEBUFFERS );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_MULTISAMPLESAMPLES );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_ACCELERATED_VISUAL );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_CONTEXT_MAJOR_VERSION );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_CONTEXT_MINOR_VERSION );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_CONTEXT_FLAGS );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_SHARE_WITH_CURRENT_CONTEXT );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_FRAMEBUFFER_SRGB_CAPABLE );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_CONTEXT_PROFILE_MASK );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_CONTEXT_RELEASE_BEHAVIOR );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_CONTEXT_RESET_NOTIFICATION );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_CONTEXT_NO_ERROR );
#undef MAP_REF_API_ATTRIBUTE_TO_SDL
	}

	return 0;
}

void GL_SwapBuffers( void )
{
	SDL_GL_SwapWindow( host.hWnd );
}

void *GL_GetProcAddress( const char *name )
{
	return SDL_GL_GetProcAddress( name );
}

qboolean SW_CreateBuffer( int width, int height, uint *stride, uint *bpp, uint *r, uint *g, uint *b )
{
	// TODO
	return false;
}

void *SW_LockBuffer( void )
{
	// TODO
	return NULL;
}

void SW_UnlockBuffer( void )
{
	// TODO
}

ref_window_type_t R_GetWindowHandle( void **handle, ref_window_type_t type )
{
	if( type == REF_WINDOW_TYPE_SDL3 )
	{
		if( handle )
			*handle = host.hWnd;
		return REF_WINDOW_TYPE_SDL3;
	}

	if( type == REF_WINDOW_TYPE_SDL2 )
		return REF_WINDOW_TYPE_NULL;

	const char *driver = SDL_GetCurrentVideoDriver();

	if( !driver )
	{
		Con_PrintSDLError( "SDL_GetCurrentVideoDriver" );
		return REF_WINDOW_TYPE_NULL;
	}

	SDL_PropertiesID pid = SDL_GetWindowProperties( host.hWnd );

	if( !pid )
	{
		Con_PrintSDLError( "SDL_GetWindowProperties" );
		return REF_WINDOW_TYPE_NULL;
	}

	if( !Q_strcmp( driver, "windows" ))
	{
		if( !type || type == REF_WINDOW_TYPE_WIN32 )
		{
			if( handle )
				*handle = SDL_GetPointerProperty( pid, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL );
			return REF_WINDOW_TYPE_WIN32;
		}
	}
	else if( !Q_strcmp( driver, "x11" ))
	{
		if( !type || type == REF_WINDOW_TYPE_X11 )
		{
			if( handle )
				*handle = SDL_GetPointerProperty( pid, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL );
			return REF_WINDOW_TYPE_X11;
		}
	}
	else if( !Q_strcmp( driver, "wayland" ))
	{
		if( !type || type == REF_WINDOW_TYPE_WAYLAND )
		{
			if( handle )
				*handle = SDL_GetPointerProperty( pid, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, NULL );
			return REF_WINDOW_TYPE_WAYLAND;
		}
	}
	else if( !Q_strcmp( driver, "cocoa" ))
	{
		if( !type || type == REF_WINDOW_TYPE_MACOS )
		{
			if( handle )
				*handle = SDL_GetPointerProperty( pid, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL );
			return REF_WINDOW_TYPE_MACOS;
		}
	}

	*handle = NULL;
	return REF_WINDOW_TYPE_NULL;
}

int R_MaxVideoModes( void )
{
	return vid_state.num_vidmodes;
}

struct vidmode_s *R_GetVideoMode( int num )
{
	if( !vid_state.vidmodes || num < 0 || num >= R_MaxVideoModes( ))
		return NULL;

	return &vid_state.vidmodes[num];
}

qboolean VID_SetMode( void )
{
	window_mode_t window_mode;
	int screen_width = Cvar_VariableInteger( "width" );
	int screen_height = Cvar_VariableInteger( "height" );
	rserr_t err;

	if( screen_width < VID_MIN_WIDTH || screen_height < VID_MIN_HEIGHT )
	{
		screen_width = vid_state.prev_width;
		screen_width = vid_state.prev_height;
	}

#if XASH_MOBILE_PLATFORM
	if( Q_strcmp( vid_fullscreen.string, DEFAULT_FULLSCREEN ))
	{
		Cvar_DirectSet( &vid_fullscreen, DEFAULT_FULLSCREEN );
		Con_Reportf( S_ERROR "%s: windowed unavailable on this platform\n", __func__ );
	}
#endif

	window_mode = bound( 0, vid_fullscreen.value, WINDOW_MODE_COUNT - 1 );
	SetBits( gl_vsync.flags, FCVAR_CHANGED );

	err = R_ChangeDisplaySettings( screen_width, screen_height, window_mode );

	switch( err )
	{
	case rserr_ok:
		vid_state.prev_width = screen_width;
		vid_state.prev_height = screen_height;

		return true;
		break;
	case rserr_invalid_fullscreen:
		Cvar_DirectSetValue( &vid_fullscreen, WINDOW_MODE_WINDOWED );
		Sys_Warn( "%s: fullscreen unavailable in this mode!", __func__ );

		// try same but windowed
		err = R_ChangeDisplaySettings( screen_width, screen_height, WINDOW_MODE_WINDOWED );
		if( err == rserr_ok )
			return true;
		break;
	case rserr_invalid_mode:
		Sys_Warn( "%s: invalid mode, engine will run in %dx%d", __func__, vid_state.prev_width, vid_state.prev_height );
		break;
	}

	// revert to last successful mode
	err = R_ChangeDisplaySettings( vid_state.prev_width, vid_state.prev_height, WINDOW_MODE_WINDOWED );
	if( err != rserr_ok )
	{
		Sys_Warn( "%s: could not revert to safe mode!", __func__ );
		return false;
	}

	return true;
}

rserr_t R_ChangeDisplaySettings( int width, int height, window_mode_t window_mode )
{
	if( !host.hWnd )
	{
		// create window if not exists
		if( !VID_CreateWindow( width, height, window_mode ))
			return rserr_invalid_mode;
	}
	else
	{
		// TODO: change current window mode
		return rserr_invalid_fullscreen;
	}

	refState.desktopBitsPixel = 24; // TODO: figure this out
	refState.window_mode = window_mode;

	return rserr_ok;
}
