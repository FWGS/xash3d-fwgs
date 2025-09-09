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
#include <SDL.h>
#include <SDL_config.h>
#include "common.h"
#include "client.h"
#include "vid_common.h"
#include "platform_sdl2.h"

// include it after because it breaks definitions in net_api.h wtf
#include <SDL_syswm.h>

static vidmode_t *vidmodes = NULL;
static int num_vidmodes = 0;
static void GL_SetupAttributes( void );
struct
{
	int prev_width, prev_height;
} sdlState = { 640, 480 };

struct
{
	SDL_Renderer *renderer;
	SDL_Texture *tex;
	int width, height;
	SDL_Surface *surf;
	SDL_Surface *win;
} sw;

void Platform_Minimize_f( void )
{
	if( host.hWnd )
		SDL_MinimizeWindow( host.hWnd );
}

qboolean SW_CreateBuffer( int width, int height, uint *stride, uint *bpp, uint *r, uint *g, uint *b )
{
	sw.width = width;
	sw.height = height;

	if( sw.renderer )
	{
		unsigned int format = SDL_GetWindowPixelFormat( host.hWnd );
		SDL_RenderSetLogicalSize( sw.renderer, refState.width, refState.height );

		if( sw.tex )
		{
			SDL_DestroyTexture( sw.tex );
			sw.tex = NULL;
		}

		// guess
		if( format == SDL_PIXELFORMAT_UNKNOWN )
		{
			if( refState.desktopBitsPixel == 16 )
				format = SDL_PIXELFORMAT_RGB565;
			else
				format = SDL_PIXELFORMAT_RGBA8888;
		}

		// we can only copy fast 16 or 32 bits
		// SDL_Renderer does not allow zero-copy, so 24 bits will be ineffective
		if( SDL_BYTESPERPIXEL( format ) != 2 && SDL_BYTESPERPIXEL( format ) != 4 )
			format = SDL_PIXELFORMAT_RGBA8888;

		sw.tex = SDL_CreateTexture( sw.renderer, format, SDL_TEXTUREACCESS_STREAMING, width, height );

		// fallback
		if( !sw.tex && format != SDL_PIXELFORMAT_RGBA8888 )
		{
			format = SDL_PIXELFORMAT_RGBA8888;
			sw.tex = SDL_CreateTexture( sw.renderer, format, SDL_TEXTUREACCESS_STREAMING, width, height );
		}

		if( !sw.tex )
		{
			SDL_DestroyRenderer( sw.renderer );
			sw.renderer = NULL;
		}
		else
		{
			void *pixels;
			int pitch;

			if( !SDL_LockTexture( sw.tex, NULL, &pixels, &pitch ))
			{
				int bits;
				uint amask;

				// lock successfull, release
				SDL_UnlockTexture( sw.tex );

				// enough for building blitter tables
				SDL_PixelFormatEnumToMasks( format, &bits, r, g, b, &amask );
				*bpp = SDL_BYTESPERPIXEL( format );
				*stride = pitch / *bpp;

				return true;
			}

			// fallback to surf
			SDL_DestroyTexture( sw.tex );
			sw.tex = NULL;
			SDL_DestroyRenderer( sw.renderer );
			sw.renderer = NULL;
		}
	}

	if( !sw.renderer )
	{
		sw.win = SDL_GetWindowSurface( host.hWnd );

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

		/// TODO: check somehow if ref_soft can handle native format
#if 0
		{
			sw.surf = SDL_CreateRGBSurfaceWithFormat( 0, width, height, 16, SDL_PIXELFORMAT_RGB565 );
			if( !sw.surf )
				Sys_Error( "%s: %s", __func__, SDL_GetError( ));
		}
#endif
		return true;
	}

	// we can't create ref_soft buffer
	return false;
}

void *SW_LockBuffer( void )
{
	if( sw.renderer )
	{
		void *pixels;
		int stride;

		if( SDL_LockTexture(sw.tex, NULL, &pixels, &stride ) < 0 )
			Sys_Error( "%s: %s", __func__, SDL_GetError( ));
		return pixels;
	}

	// ensure it not changed (do we really need this?)
	sw.win = SDL_GetWindowSurface( host.hWnd );
	//if( !sw.win )
		//SDL_GetWindowSurface( host.hWnd );

	// prevent buffer overrun
	if( !sw.win || sw.win->w < sw.width || sw.win->h < sw.height  )
		return NULL;

	if( sw.surf )
	{
		SDL_LockSurface( sw.surf );
		return sw.surf->pixels;
	}
	else
	{
		// real window pixels (x11 shm region, dma buffer, etc)
		// or SDL_Renderer texture if not supported
		SDL_LockSurface( sw.win );
		return sw.win->pixels;
	}
}

void SW_UnlockBuffer( void )
{
	if( sw.renderer )
	{
		SDL_Rect src, dst;
		src.x = src.y = 0;
		src.w = sw.width;
		src.h = sw.height;
		dst = src;
		SDL_UnlockTexture(sw.tex);

		SDL_SetTextureBlendMode(sw.tex, SDL_BLENDMODE_NONE);


		SDL_RenderCopy(sw.renderer, sw.tex, &src, &dst);
		SDL_RenderPresent(sw.renderer);

		return;
		//Con_Printf("%s\n", SDL_GetError());
	}

	// blit if blitting surface availiable
	if( sw.surf )
	{
		SDL_Rect src, dst;
		src.x = src.y = 0;
		src.w = sw.width;
		src.h = sw.height;
		dst = src;
		SDL_UnlockSurface( sw.surf );
		SDL_BlitSurface( sw.surf, &src, sw.win, &dst );
		return;
	}

	// already blitted
	SDL_UnlockSurface( sw.win );

	SDL_UpdateWindowSurface( host.hWnd );
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
#if SDL_VERSION_ATLEAST( 2, 24, 0 )
	SDL_Point point = { window_xpos.value, window_ypos.value };
	int displayIndex = SDL_GetPointDisplayIndex( &point );
#else
	int displayIndex = 0;
#endif
	int i, modes;

	num_vidmodes = 0;
	modes = SDL_GetNumDisplayModes( displayIndex );

	if( !modes )
		return;

	vidmodes = Mem_Malloc( host.mempool, modes * sizeof( vidmode_t ) );

	for( i = 0; i < modes; i++ )
	{
		int j;
		SDL_DisplayMode mode;

		if( SDL_GetDisplayMode( displayIndex, i, &mode ) < 0 )
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
				break;
			}
		}
		if( j != num_vidmodes )
			continue;

		vidmodes[num_vidmodes].width = mode.w;
		vidmodes[num_vidmodes].height = mode.h;
		Q_snprintf( buf, sizeof( buf ), "%ix%i", mode.w, mode.h );
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

#if XASH_WIN32
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

	if( ( hModule = LoadLibraryW( L"shcore.dll" ) ) )
	{
		if( ( pSetProcessDpiAwareness = (void*)GetProcAddress( hModule, "SetProcessDpiAwareness" ) ) )
		{
			// I hope SDL don't handle WM_DPICHANGED message
			HRESULT hResult = pSetProcessDpiAwareness( XASH_SYSTEM_DPI_AWARE );

			if( hResult == S_OK )
			{
				Con_Reportf( "%s: Success\n", __func__ );
				bSuccess = TRUE;
			}
			else if( hResult == E_INVALIDARG ) Con_Reportf( "%s: Invalid argument\n", __func__ );
			else if( hResult == E_ACCESSDENIED ) Con_Reportf( "%s: Access Denied\n", __func__ );
		}
		else Con_Reportf( "%s: Can't get SetProcessDpiAwareness\n", __func__ );
		FreeLibrary( hModule );
	}
	else Con_Reportf( "%s: Can't load shcore.dll\n", __func__ );


	if( !bSuccess )
	{
		Con_Reportf( "%s: Trying SetProcessDPIAware...\n", __func__ );

		if( ( hModule = LoadLibraryW( L"user32.dll" ) ) )
		{
			if( ( pSetProcessDPIAware = ( void* )GetProcAddress( hModule, "SetProcessDPIAware" ) ) )
			{
				// I hope SDL don't handle WM_DPICHANGED message
				BOOL hResult = pSetProcessDPIAware();

				if( hResult )
				{
					Con_Reportf( "%s: Success\n", __func__ );
					bSuccess = TRUE;
				}
				else Con_Reportf( "%s: fail\n", __func__ );
			}
			else Con_Reportf( "%s: Can't get SetProcessDPIAware\n", __func__ );
			FreeLibrary( hModule );
		}
		else Con_Reportf( "%s: Can't load user32.dll\n", __func__ );
	}
}

static qboolean WIN_SetWindowIcon( HICON ico )
{
	SDL_SysWMinfo wminfo;

	SDL_VERSION( &wminfo.version );

	if( SDL_GetWindowWMInfo( host.hWnd, &wminfo ) == SDL_TRUE && wminfo.subsystem == SDL_SYSWM_WINDOWS )
	{
		SendMessage( wminfo.info.win.window, WM_SETICON, ICON_SMALL, (LONG_PTR)ico );
		SendMessage( wminfo.info.win.window, WM_SETICON, ICON_BIG, (LONG_PTR)ico );
		return true;
	}

	Con_Reportf( S_ERROR "%s: %s", __func__, SDL_GetError( ));
	return false;
}
#endif


/*
=================
GL_GetProcAddress
=================
*/
void *GL_GetProcAddress( const char *name )
{
	void *func = SDL_GL_GetProcAddress( name );
#if !SDL_VERSION_ATLEAST( 2, 0, 6 ) && XASH_POSIX
	if( !func && Sys_CheckParm( "-egl" ))
	{
		/*
		 * SDL2 has broken SDL_GL_GetProcAddress until this commit if using egl:
		 * https://github.com/libsdl-org/SDL/commit/466ba57d42d244e80357e9ad3011c50af30ed225
		 * so call eglGetProcAddress directly
		 * */
		static void *(*peglGetProcAddress)( const char * );
		if( !peglGetProcAddress )
		{
			void *lib = dlopen( "libEGL.so", RTLD_NOW );
			if( lib )
				*(void**)&peglGetProcAddress = dlsym( lib, "eglGetProcAddress" );
		}
		if( peglGetProcAddress )
			func = peglGetProcAddress( name );
	}
#endif

#if XASH_PSVITA
	// try to find in main module
	if( !func )
	{
		func = dlsym( NULL, name );
	}
#endif

	if( !func )
	{
		Con_Reportf( S_ERROR "%s failed for %s\n", __func__, name );
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
	if( FBitSet( gl_vsync.flags, FCVAR_CHANGED ))
	{
		ClearBits( gl_vsync.flags, FCVAR_CHANGED );

		if( SDL_GL_SetSwapInterval( gl_vsync.value ) < 0 )
			Con_Reportf( S_ERROR  "SDL_GL_SetSwapInterval: %s\n", SDL_GetError( ));
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
static qboolean GL_CreateContext( void )
{
	if( ( glw_state.context = SDL_GL_CreateContext( host.hWnd ) ) == NULL)
	{
		Con_Reportf( S_ERROR "%s: %s\n", __func__, SDL_GetError( ));
		return GL_DeleteContext();
	}
	return true;
}

/*
=================
GL_UpdateContext
=================
*/
static qboolean GL_UpdateContext( void )
{
	if( SDL_GL_MakeCurrent( host.hWnd, glw_state.context ) < 0 )
	{
		Con_Reportf( S_ERROR "%s: %s\n", __func__, SDL_GetError( ));
		return GL_DeleteContext();
	}
	return true;
}

void VID_SaveWindowSize( int width, int height, qboolean maximized )
{
	int render_w = width, render_h = height;

	if( !glw_state.software )
		SDL_GL_GetDrawableSize( host.hWnd, &render_w, &render_h );
	else
		SDL_RenderSetLogicalSize( sw.renderer, width, height );

	VID_SetDisplayTransform( &render_w, &render_h );
	R_SaveVideoMode( width, height, render_w, render_h, maximized );
}

static qboolean VID_SetScreenResolution( int width, int height, window_mode_t window_mode )
{
	SDL_DisplayMode got;
	Uint32 wndFlags = 0;

	if( vid_highdpi.value )
		SetBits( wndFlags, SDL_WINDOW_ALLOW_HIGHDPI );

	SDL_SetWindowBordered( host.hWnd, SDL_FALSE );

	if( window_mode == WINDOW_MODE_BORDERLESS )
	{
		if( SDL_GetDesktopDisplayMode( 0, &got ) < 0 )
		{
			Con_Printf( S_ERROR "%s: SDL_GetDesktopDisplayMode: %s", __func__, SDL_GetError( ));
			return false;
		}

		if( SDL_SetWindowFullscreen( host.hWnd, SDL_WINDOW_FULLSCREEN_DESKTOP ) < 0 )
		{
			Con_Printf( S_ERROR "%s: SDL_SetWindowFullscreen (borderless): %s", __func__, SDL_GetError( ));
			return false;
		}
	}
	else if( window_mode == WINDOW_MODE_FULLSCREEN )
	{
		SDL_DisplayMode want = { 0 };
		want.w = width;
		want.h = height;

		if( SDL_GetClosestDisplayMode( 0, &want, &got ) == NULL )
		{
			Con_Printf( S_ERROR "%s: SDL_GetClosestDisplayMode: %s", __func__, SDL_GetError( ));
			return false;
		}

		if( got.w != want.w || got.h != want.h )
			Con_Reportf( S_NOTE "Got closest display mode: %ix%i@%i\n", got.w, got.h, got.refresh_rate );

		if( SDL_SetWindowDisplayMode( host.hWnd, &got ) < 0 )
		{
			Con_Printf( S_ERROR "%s: SDL_SetWindowDisplayMode: %s", __func__, SDL_GetError( ));
			return false;
		}

		if( SDL_SetWindowFullscreen( host.hWnd, SDL_WINDOW_FULLSCREEN ) < 0 )
		{
			Con_Printf( S_ERROR "%s: SDL_SetWindowFullscreen (fullscreen): %s", __func__, SDL_GetError( ));
			return false;
		}
	}

	SDL_SetWindowSize( host.hWnd, got.w, got.h );
	VID_SaveWindowSize( got.w, got.h, true );
	return true;
}

void VID_RestoreScreenResolution( void )
{
	// on mobile platform fullscreen is designed to be always on
	// and code below minimizes our window if we're in full screen
	// don't do that on mobile devices
#if !XASH_MOBILE_PLATFORM
	switch((window_mode_t)vid_fullscreen.value )
	{
	case WINDOW_MODE_WINDOWED:
		// TODO: this line is from very old SDL video backend
		// figure out why we need it, because in windowed mode we
		// always have borders
		SDL_SetWindowBordered( host.hWnd, SDL_TRUE );
		break;
	case WINDOW_MODE_BORDERLESS:
		// in borderless fullscreen we don't change screen resolution, so no-op
		break;
	case WINDOW_MODE_FULLSCREEN:
		// TODO: we might want to not minimize window if current desktop mode
		// and window mode are the same
		SDL_MinimizeWindow( host.hWnd );
		SDL_SetWindowFullscreen( host.hWnd, 0 );
		break;
	}
#endif // !XASH_MOBILE_PLATFORM
}

static void VID_SetWindowIcon( SDL_Window *hWnd )
{
	rgbdata_t *icon = NULL;
	char iconpath[MAX_STRING];
#if XASH_WIN32 // ICO support only for Win32
	const char *disk_iconpath = FS_GetDiskPath( GI->iconpath, true );

	if( disk_iconpath )
	{
		int len = MultiByteToWideChar( CP_UTF8, 0, disk_iconpath, -1, NULL, 0 );

		if( len >= 0 )
		{
			wchar_t *path = malloc( len * sizeof( *path ));
			HICON ico;

			MultiByteToWideChar( CP_UTF8, 0, disk_iconpath, -1, path, len );
			ico = (HICON)LoadImageW( NULL, path, IMAGE_ICON, 0, 0, LR_LOADFROMFILE|LR_DEFAULTSIZE );

			free( path );

			if( ico && WIN_SetWindowIcon( ico ))
				return;
		}
	}
#endif // XASH_WIN32

	Q_strncpy( iconpath, GI->iconpath, sizeof( iconpath ));
	COM_ReplaceExtension( iconpath, ".tga", sizeof( iconpath ));
	icon = FS_LoadImage( iconpath, NULL, 0 );

	if( icon )
	{
		SDL_Surface *surface = SDL_CreateRGBSurfaceFrom( icon->buffer,
			icon->width, icon->height, 32, 4 * icon->width,
			0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000 );

		FS_FreeImage( icon );

		if( surface )
		{
			SDL_SetWindowIcon( host.hWnd, surface );
			SDL_FreeSurface( surface );
			return;
		}
	}

#if XASH_WIN32 // ICO support only for Win32
	WIN_SetWindowIcon( LoadIcon( GetModuleHandle( NULL ), MAKEINTRESOURCE( 101 )));
#endif
}

static qboolean VID_CreateWindowWithSafeGL( const char *wndname, int xpos, int ypos, int w, int h, uint32_t flags )
{
	while( glw_state.safe >= SAFE_NO && glw_state.safe < SAFE_LAST )
	{
		host.hWnd = SDL_CreateWindow( wndname, xpos, ypos, w, h, flags );

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
	qboolean maximized = vid_maximized.value != 0.0f;
	Uint32 wndFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_MOUSE_FOCUS;
	int xpos, ypos;
	int num_displays = SDL_GetNumVideoDisplays();
	SDL_Rect rect = { window_xpos.value, window_ypos.value, width, height };

	Q_strncpy( wndname, GI->title, sizeof( wndname ));

	if( vid_highdpi.value )
		SetBits( wndFlags, SDL_WINDOW_ALLOW_HIGHDPI );
	if( !glw_state.software )
		SetBits( wndFlags, SDL_WINDOW_OPENGL );

	if( window_mode == WINDOW_MODE_WINDOWED )
	{
		SDL_Rect *display_rects = ( SDL_Rect * )malloc( num_displays * sizeof( SDL_Rect ));

		SetBits( wndFlags, SDL_WINDOW_RESIZABLE );
		if( maximized )
			SetBits( wndFlags, SDL_WINDOW_MAXIMIZED );

		if( !display_rects )
		{
			Con_Printf( S_ERROR "Failed to allocate memory for display rects!\n" );
			xpos = SDL_WINDOWPOS_UNDEFINED;
			ypos = SDL_WINDOWPOS_UNDEFINED;
		}
		else
		{
			for( int i = 0; i < num_displays; i++ )
			{
				if( SDL_GetDisplayBounds( i, &display_rects[i] ) != 0 )
				{
					Con_Printf( S_ERROR "Failed to get bounds for display %d! SDL_Error: %s\n", i, SDL_GetError());
					display_rects[i] = ( SDL_Rect ){ 0, 0, 0, 0 };
				}
			}
			// Check if the rectangle fits in any display
			if( !RectFitsInAnyDisplay( &rect, display_rects, num_displays ))
			{
				// Rectangle doesn't fit in any display, center it
				xpos = SDL_WINDOWPOS_CENTERED;
				ypos = SDL_WINDOWPOS_CENTERED;
				Con_Printf( S_ERROR "Rectangle does not fit in any display. Centering window.\n" );
			}
			else
			{
				xpos = rect.x;
				ypos = rect.y;
			}
		}
		free( display_rects );
	}
	else
	{
		if( window_mode == WINDOW_MODE_FULLSCREEN )
			// need input grab only in true fullscreen mode
			SetBits( wndFlags, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_INPUT_GRABBED );
		else
			SetBits( wndFlags, SDL_WINDOW_FULLSCREEN_DESKTOP );
		SetBits( wndFlags, SDL_WINDOW_BORDERLESS );
		if ( window_xpos.value < 0 || window_ypos.value < 0 )
		{
			xpos = SDL_WINDOWPOS_UNDEFINED;
			ypos = SDL_WINDOWPOS_UNDEFINED;
		}
		else
		{
			xpos = window_xpos.value;
			ypos = window_ypos.value;
		}
	}

	if( !VID_CreateWindowWithSafeGL( wndname, xpos, ypos, width, height, wndFlags ))
		return false;

	// update window size if it was maximized, just in case
	if( FBitSet( SDL_GetWindowFlags( host.hWnd ), SDL_WINDOW_MAXIMIZED|SDL_WINDOW_FULLSCREEN_DESKTOP ) != 0 )
		SDL_GetWindowSize( host.hWnd, &width, &height );

	if( window_mode != WINDOW_MODE_WINDOWED )
	{
		if( !VID_SetScreenResolution( width, height, window_mode ))
			return false;
	}
	else VID_RestoreScreenResolution();

	VID_SetWindowIcon( host.hWnd );
	SDL_ShowWindow( host.hWnd );

	if( glw_state.software )
	{
		int sdl_renderer = -2;
		char cmd[64];

		if( Sys_GetParmFromCmdLine("-sdl_renderer", cmd ) )
			sdl_renderer = Q_atoi( cmd );

		if( sdl_renderer >= -1 )
		{
			sw.renderer = SDL_CreateRenderer( host.hWnd, sdl_renderer, 0 );
			if( !sw.renderer )
				Con_Printf( S_ERROR "failed to create SDL renderer: %s\n", SDL_GetError() );
			else
			{
				SDL_RendererInfo info;
				SDL_GetRendererInfo( sw.renderer, &info );
				Con_Printf( "SDL_Renderer %s initialized\n", info.name );
			}
		}
	}
	else
	{
		if( !glw_state.initialized )
		{
			while( !GL_CreateContext( ))
			{
				glw_state.safe++;
				if(glw_state.safe > SAFE_DONTCARE)
					return false;
				GL_SetupAttributes(); // re-choose attributes
			}
		}

		if( !GL_UpdateContext( ))
			return false;
	}

	VID_SaveWindowSize( width, height, maximized );

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

	if( refState.fullScreen )
	{
		refState.fullScreen = false;
	}
}

/*
==================
GL_SetupAttributes
==================
*/
static void GL_SetupAttributes( void )
{
	SDL_GL_ResetAttributes();

	ref.dllFuncs.GL_SetupAttributes( glw_state.safe );
}

void GL_SwapBuffers( void )
{
	SDL_GL_SwapWindow( host.hWnd );
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
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_CONTEXT_EGL );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_CONTEXT_FLAGS );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_SHARE_WITH_CURRENT_CONTEXT );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_FRAMEBUFFER_SRGB_CAPABLE );
	case REF_GL_CONTEXT_PROFILE_MASK:
#if !XASH_WIN32
#ifdef SDL_HINT_OPENGL_ES_DRIVER
		if( val == REF_GL_CONTEXT_PROFILE_ES )
		{
			SDL_SetHint( SDL_HINT_OPENGL_ES_DRIVER, "1" );
			SDL_SetHint( "SDL_VIDEO_X11_FORCE_EGL", "1" );
		}
#endif // SDL_HINT_OPENGL_ES_DRIVER
#endif
		return SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, val );
#if SDL_VERSION_ATLEAST( 2, 0, 4 )
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_CONTEXT_RELEASE_BEHAVIOR );
#endif
#if SDL_VERSION_ATLEAST( 2, 0, 6 )
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_CONTEXT_RESET_NOTIFICATION );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_CONTEXT_NO_ERROR );
#endif
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
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_CONTEXT_EGL );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_CONTEXT_FLAGS );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_SHARE_WITH_CURRENT_CONTEXT );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_FRAMEBUFFER_SRGB_CAPABLE );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_CONTEXT_PROFILE_MASK );
#if SDL_VERSION_ATLEAST( 2, 0, 4 )
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_CONTEXT_RELEASE_BEHAVIOR );
#endif
#if SDL_VERSION_ATLEAST( 2, 0, 6 )
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_CONTEXT_RESET_NOTIFICATION );
		MAP_REF_API_ATTRIBUTE_TO_SDL( GL_CONTEXT_NO_ERROR );
#endif
#undef MAP_REF_API_ATTRIBUTE_TO_SDL
	}

	return 0;
}

#ifndef EGL_LIB
#define EGL_LIB NULL
#endif

/*
==================
R_Init_Video
==================
*/
qboolean R_Init_Video( const int type )
{
	string safe;
	qboolean retval;
	SDL_DisplayMode displayMode;
#if SDL_VERSION_ATLEAST( 2, 24, 0 )
	SDL_Point point = { window_xpos.value, window_ypos.value };
	int displayIndex = SDL_GetPointDisplayIndex( &point );
#else
	int displayIndex = 0;
#endif
	SDL_GetCurrentDisplayMode( displayIndex, &displayMode );

	refState.desktopBitsPixel = SDL_BITSPERPIXEL( displayMode.format );

#ifdef SDL_HINT_QTWAYLAND_WINDOW_FLAGS
	SDL_SetHint( SDL_HINT_QTWAYLAND_WINDOW_FLAGS, "OverridesSystemGestures" );
#endif
#ifdef SDL_HINT_QTWAYLAND_CONTENT_ORIENTATION
	SDL_SetHint( SDL_HINT_QTWAYLAND_CONTENT_ORIENTATION, "landscape" );
#endif

	if( Sys_CheckParm( "-egl" ) )
#if XASH_WIN32
		SDL_SetHint( "SDL_OPENGL_ES_DRIVER", "1" );
#else
		SDL_SetHint( "SDL_VIDEO_X11_FORCE_EGL", "1" );

	SDL_SetHint( "SDL_VIDEO_X11_XRANDR", "1" );
	SDL_SetHint( "SDL_VIDEO_X11_XVIDMODE", "1" );
#endif // !XASH_WIN32

	// must be initialized before creating window
#if XASH_WIN32
	WIN_SetDPIAwareness();
#endif

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

		if( SDL_GL_LoadLibrary( EGL_LIB ) < 0 )
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
	SDL_DisplayMode displayMode;

	if( SDL_GetCurrentDisplayMode( 0, &displayMode ) < 0 )
	{
		Con_Printf( S_ERROR "SDL_GetCurrentDisplayMode: %s\n", SDL_GetError( ));
		return rserr_invalid_mode;
	}

	// check our desktop attributes
	refState.desktopBitsPixel = SDL_BITSPERPIXEL( displayMode.format );
	if( window_mode == WINDOW_MODE_BORDERLESS )
	{
		width = displayMode.w;
		height = displayMode.h;
	}

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

		if( SDL_SetWindowFullscreen( host.hWnd, 0 ) < 0 )
		{
			Con_Printf( S_ERROR "SDL_SetWindowFullscreen: %s", SDL_GetError( ));
			return rserr_invalid_fullscreen;
		}
#if SDL_VERSION_ATLEAST( 2, 0, 5 )
		SDL_SetWindowResizable( host.hWnd, SDL_TRUE );
#endif
		SDL_SetWindowBordered( host.hWnd, SDL_TRUE );
		SDL_SetWindowSize( host.hWnd, width, height );

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
#if !defined( DEFAULT_MODE_WIDTH ) || !defined( DEFAULT_MODE_HEIGHT )
		SDL_DisplayMode mode;

		SDL_GetDesktopDisplayMode( 0, &mode );

		iScreenWidth = mode.w;
		iScreenHeight = mode.h;
#else
		iScreenWidth = DEFAULT_MODE_WIDTH;
		iScreenHeight = DEFAULT_MODE_HEIGHT;
#endif
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
	SDL_SysWMinfo wmInfo;

	if( type == REF_WINDOW_TYPE_SDL )
	{
		if( handle )
			*handle = (void *)host.hWnd;
		return REF_WINDOW_TYPE_SDL;
	}

	SDL_VERSION( &wmInfo.version );

	if( SDL_GetWindowWMInfo( host.hWnd, &wmInfo ))
		return REF_WINDOW_TYPE_NULL;

	switch( wmInfo.subsystem )
	{
	case SDL_SYSWM_WINDOWS:
		if( !type || type == REF_WINDOW_TYPE_WIN32 )
		{
#ifdef SDL_VIDEO_DRIVER_WINDOWS
			if( handle )
				*handle = (void *)wmInfo.info.win.window;
			return REF_WINDOW_TYPE_WIN32;
#endif // SDL_VIDEO_DRIVER_WINDOWS
		}
		break;
	case SDL_SYSWM_X11:
		if( !type || type == REF_WINDOW_TYPE_X11 )
		{
#ifdef SDL_VIDEO_DRIVER_X11
			if( handle )
				*handle = (void *)(uintptr_t)wmInfo.info.x11.window;
			return REF_WINDOW_TYPE_X11;
#endif // SDL_VIDEO_DRIVER_X11
		}
		break;
	case SDL_SYSWM_COCOA:
		if( !type || type == REF_WINDOW_TYPE_MACOS )
		{
#ifdef SDL_VIDEO_DRIVER_COCOA
			if( handle )
				*handle = (void *)wmInfo.info.cocoa.window;
			return REF_WINDOW_TYPE_MACOS;
#endif // SDL_VIDEO_DRIVER_COCOA
		}
		break;
	case SDL_SYSWM_WAYLAND:
		if( !type || type == REF_WINDOW_TYPE_WAYLAND )
		{
#ifdef SDL_VIDEO_DRIVER_WAYLAND
			if( handle )
				*handle = (void *)wmInfo.info.wl.surface;
			return REF_WINDOW_TYPE_WAYLAND;
#endif // SDL_VIDEO_DRIVER_WAYLAND
		}
		break;
	}

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
