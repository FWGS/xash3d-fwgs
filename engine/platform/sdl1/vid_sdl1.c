/*
vid_sdl1.c - SDL vid component
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
#include <SDL.h>
#include "common.h"
#include "client.h"
#include "vid_common.h"
#include "platform_sdl1.h"

static vidmode_t *vidmodes = NULL;
static int num_vidmodes = 0;
static void GL_SetupAttributes( void );
struct
{
	int prev_width, prev_height;
} sdlState = { 640, 480 };

struct
{
	int width, height;
	SDL_Surface *surf;
	SDL_Surface *win;
} sw;

void Platform_Minimize_f( void )
{
	SDL_WM_IconifyWindow();
}

qboolean SW_CreateBuffer( int width, int height, uint *stride, uint *bpp, uint *r, uint *g, uint *b )
{
	sw.width = width;
	sw.height = height;
	sw.win = SDL_GetVideoSurface();

	// sdl will create renderer if hw framebuffer unavailiable, so cannot fallback here
	// if it is failed, it is not possible to draw with SDL in REF_SOFTWARE mode
	if( !sw.win )
	{
		Sys_Warn( "failed to initialize software output, try running with -glblit flag" );
		return false;
	}

	*bpp = sw.win->format->BytesPerPixel;
	*r = sw.win->format->Rmask;
	*g = sw.win->format->Gmask;
	*b = sw.win->format->Bmask;
	*stride = sw.win->pitch / sw.win->format->BytesPerPixel;

	return true;
}

void *SW_LockBuffer( void )
{
	sw.win = SDL_GetVideoSurface();

	// prevent buffer overrun
	if( !sw.win || sw.win->w < sw.width || sw.win->h < sw.height  )
		return NULL;

	// real window pixels (x11 shm region, dma buffer, etc)
	// or SDL_Renderer texture if not supported
	SDL_LockSurface( sw.win );
	return sw.win->pixels;
}

void SW_UnlockBuffer( void )
{
	// already blitted
	SDL_UnlockSurface( sw.win );

	SDL_Flip( host.hWnd );
}

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
	char buf[MAX_VA_STRING];
	SDL_Rect **modes;
	int len = 0, i = 0, j;

	modes = SDL_ListModes( NULL, SDL_FULLSCREEN );

	if( !modes || modes == (void*)-1 )
		return;

	for( len = 0; modes[len]; len++ );

	vidmodes = Mem_Malloc( host.mempool, len * sizeof( vidmode_t ) );

	// from smallest to largest
	for( ; i < len; i++ )
	{
		SDL_Rect *mode = modes[len - i - 1];

		for( j = 0; j < num_vidmodes; j++ )
		{
			if( mode->w == vidmodes[j].width &&
				mode->h == vidmodes[j].height )
			{
				break;
			}
		}
		if( j != num_vidmodes )
			continue;

		vidmodes[num_vidmodes].width = mode->w;
		vidmodes[num_vidmodes].height = mode->h;
		Q_snprintf( buf, sizeof( buf ), "%ix%i", mode->w, mode->h );
		vidmodes[num_vidmodes].desc = copystring( buf );

		num_vidmodes++;
	}
}

static void R_FreeVideoModes( void )
{
	int i;

	if( !vidmodes )
		return;

	for( i = 0; i < num_vidmodes; i++ )
		Mem_Free( (char*)vidmodes[i].desc );
	Mem_Free( vidmodes );

	vidmodes = NULL;
}

/*
=================
GL_GetProcAddress
=================
*/
void *GL_GetProcAddress( const char *name )
{
	void *func = SDL_GL_GetProcAddress( name );

	if( !func )
		Con_Reportf( S_ERROR "%s failed for %s\n", __func__, name );

	return func;
}

/*
===============
GL_UpdateSwapInterval
===============
*/
void GL_UpdateSwapInterval( void )
{
}

/*
=================
GL_DeleteContext

always return false
=================
*/
qboolean GL_DeleteContext( void )
{
	return false;
}

/*
=================
GL_CreateContext
=================
*/
static qboolean GL_CreateContext( void )
{
	return true;
}

/*
=================
GL_UpdateContext
=================
*/
static qboolean GL_UpdateContext( void )
{
	return true;
}

void VID_SaveWindowSize( int width, int height, qboolean maximized )
{
	int render_w = width, render_h = height;

	VID_SetDisplayTransform( &render_w, &render_h );
	R_SaveVideoMode( width, height, render_w, render_h, maximized );
}

static qboolean VID_SetScreenResolution( int width, int height, window_mode_t window_mode )
{
	VID_SaveWindowSize( width, height, true );
	return true;
}

void VID_RestoreScreenResolution( void )
{
}

static qboolean VID_CreateWindowWithSafeGL( const char *wndname, int xpos, int ypos, int w, int h, uint32_t flags )
{
	while( glw_state.safe >= SAFE_NO && glw_state.safe < SAFE_LAST )
	{
		host.hWnd = sw.surf = SDL_SetVideoMode( w, h, 16, flags );
		// we have window, exit loop
		if( host.hWnd )
			break;

		Con_Reportf( S_ERROR "%s: couldn't create '%s' with safegl level %d: %s\n", __func__, wndname, glw_state.safe, SDL_GetError());

		glw_state.safe++;

		if( !gl_msaa_samples.value && glw_state.safe == SAFE_NOMSAA )
			glw_state.safe++; // no need to skip msaa, if we already disabled it

		GL_SetupAttributes(); // re-choose attributes

		// try again create window
	}

	// window creation has failed...
	if( glw_state.safe >= SAFE_LAST )
		return false;

	return true;
}

static qboolean RectFitsInDisplay( const SDL_Rect *rect, const SDL_Rect *display )
{
	return rect->x >= display->x
		&& rect->y >= display->y
		&& rect->x + rect->w <= display->x + display->w
		&& rect->y + rect->h <= display->y + display->h;
}
// Function to check if the rectangle fits in any display
static qboolean RectFitsInAnyDisplay( const SDL_Rect *rect, const SDL_Rect *display_rects, int num_displays )
{
	for( int i = 0; i < num_displays; i++ )
	{
		if( RectFitsInDisplay( rect, &display_rects[i] ))
			return true; // Rectangle fits in this display
	}
	return false; // Rectangle does not fit in any display
}

/*
=================
VID_CreateWindow
=================
*/
qboolean VID_CreateWindow( int width, int height, window_mode_t window_mode )
{
	string wndname;
	Uint32 flags = 0;

	Q_strncpy( wndname, GI->title, sizeof( wndname ));

	if( window_mode != WINDOW_MODE_WINDOWED )
		SetBits( flags, SDL_FULLSCREEN|SDL_HWSURFACE );

	if( !glw_state.software )
		SetBits( flags, SDL_OPENGL );

	if( !VID_CreateWindowWithSafeGL( wndname, 0, 0, width, height, flags ))
		return false;

	VID_SaveWindowSize( width, height, false );

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
		host.hWnd = NULL;

	if( refState.fullScreen )
		refState.fullScreen = false;
}

/*
==================
GL_SetupAttributes
==================
*/
static void GL_SetupAttributes( void )
{
	ref.dllFuncs.GL_SetupAttributes( glw_state.safe );
}

void GL_SwapBuffers( void )
{
	SDL_Flip( host.hWnd );
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
#undef MAP_REF_API_ATTRIBUTE_TO_SDL
	}

	return 0;
}

/*
==================
R_Init_Video
==================
*/
qboolean R_Init_Video( const int type )
{
	string safe;
	qboolean retval;

	refState.desktopBitsPixel = 16;

	switch( type )
	{
	case REF_SOFTWARE:
		glw_state.software = true;
		break;
	case REF_GL:
		if( !glw_state.safe && Sys_GetParmFromCmdLine( "-safegl", safe ) )
			glw_state.safe = bound( SAFE_NO, Q_atoi( safe ), SAFE_DONTCARE );

		// refdll can request some attributes
		GL_SetupAttributes( );

		if( SDL_GL_LoadLibrary( NULL ) < 0 )
		{
			Con_Reportf( S_ERROR  "Couldn't initialize OpenGL: %s\n", SDL_GetError());
			return false;
		}
		break;
	default:
		Host_Error( "Can't initialize unknown context type %d!\n", type );
		break;
	}

	if( !(retval = VID_SetMode()) )
	{
		return retval;
	}

	switch( type )
	{
	case REF_GL:
		// refdll also can check extensions
		ref.dllFuncs.GL_InitExtensions();
		break;
	case REF_SOFTWARE:
	default:
		break;
	}

	R_InitVideoModes();

	host.renderinfo_changed = false;

	return true;
}

rserr_t R_ChangeDisplaySettings( int width, int height, window_mode_t window_mode )
{
	refState.fullScreen = window_mode != WINDOW_MODE_WINDOWED;
	Con_Reportf( "%s: Setting video mode to %dx%d %s\n", __func__, width, height, refState.fullScreen ? "fullscreen" : "windowed" );

	if( !host.hWnd )
	{
		if( !VID_CreateWindow( width, height, window_mode ))
			return rserr_invalid_mode;
	}
	else if( refState.fullScreen )
	{
		if( !VID_SetScreenResolution( width, height, window_mode ))
			return rserr_invalid_fullscreen;
	}
	else
	{
		VID_RestoreScreenResolution();
		VID_SaveWindowSize( width, height, true );
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
	int iScreenWidth, iScreenHeight;
	rserr_t	err;
	window_mode_t window_mode;

	iScreenWidth = Cvar_VariableInteger( "width" );
	iScreenHeight = Cvar_VariableInteger( "height" );

	if( iScreenWidth < VID_MIN_WIDTH ||
		iScreenHeight < VID_MIN_HEIGHT )	// trying to get resolution automatically by default
	{
		iScreenWidth = 320;
		iScreenHeight = 240;
	}

	window_mode = bound( 0, vid_fullscreen.value, WINDOW_MODE_COUNT - 1 );
	SetBits( gl_vsync.flags, FCVAR_CHANGED );

	if(( err = R_ChangeDisplaySettings( iScreenWidth, iScreenHeight, window_mode )) == rserr_ok )
	{
		sdlState.prev_width = iScreenWidth;
		sdlState.prev_height = iScreenHeight;
	}
	else
	{
		if( err == rserr_invalid_fullscreen )
		{
			Cvar_DirectSet( &vid_fullscreen, "0" );
			Con_Reportf( S_ERROR "%s: fullscreen unavailable in this mode\n", __func__ );
			Sys_Warn( "fullscreen unavailable in this mode!" );
			if(( err = R_ChangeDisplaySettings( iScreenWidth, iScreenHeight, WINDOW_MODE_WINDOWED )) == rserr_ok )
				return true;
		}
		else if( err == rserr_invalid_mode )
		{
			Con_Reportf( S_ERROR "%s: invalid mode\n", __func__ );
			Sys_Warn( "invalid mode, engine will run in %dx%d", sdlState.prev_width, sdlState.prev_height );
		}

		// try setting it back to something safe
		if(( err = R_ChangeDisplaySettings( sdlState.prev_width, sdlState.prev_height, WINDOW_MODE_WINDOWED )) != rserr_ok )
		{
			Con_Reportf( S_ERROR "%s: could not revert to safe mode\n", __func__ );
			Sys_Warn( "could not revert to safe mode!" );
			return false;
		}
	}

	return true;
}

ref_window_type_t R_GetWindowHandle( void **handle, ref_window_type_t type )
{
	return REF_WINDOW_TYPE_NULL;
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
