/*
vid_gnm.c - PlayStation 4 video backend
Copyright (C) 2026 Xash3D FWGS contributors

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
The engine owns the display and the GPU device (platform/ps4/gnm), OpenGL renderers
draw through the OpenGL ES 2 subset implemented by ref_gles3compat (ref/gl/ps4) over the device,
SDL video isn't used. The screen has a fixed size and there are no windows
*/

#include "platform/platform.h"
#include "client.h"
#include "vid_common.h"
#include "library.h"
#include "ps4/gnm/gnm_engine.h"

static int ps4_gnm_dummy_window; // host.hWnd must be non-NULL while the context exists
static qboolean ps4_gnm_opened;

// exported by ref_gles3compat, see ref/gl/ps4/gnm_gl.h
static struct
{
	bool ( *init )( const void *api, bool flip_front, int mipmaps );
	void ( *end_frame )( void );
	void ( *shutdown )( void );
	void *( *get_proc_address )( const char *name );
} ps4_gl;

static qboolean PS4_GL_Load( void )
{
	ps4_gl.init = COM_GetProcAddress( ref.hInstance, "PS4_GL_Init" );
	ps4_gl.end_frame = COM_GetProcAddress( ref.hInstance, "PS4_GL_EndFrame" );
	ps4_gl.shutdown = COM_GetProcAddress( ref.hInstance, "PS4_GL_Shutdown" );
	ps4_gl.get_proc_address = COM_GetProcAddress( ref.hInstance, "PS4_GL_GetProcAddress" );

	return ps4_gl.init && ps4_gl.end_frame && ps4_gl.shutdown && ps4_gl.get_proc_address;
}

void Platform_Minimize_f( void )
{
}

platform_orientation_t Platform_GetDisplayOrientation( void )
{
	return ORIENTATION_LANDSCAPE;
}

void VID_RestoreScreenResolution( window_mode_t window_mode )
{
}

void VID_SaveWindowSize( int width, int height )
{
	VID_SetDisplayTransform( &width, &height );
	R_SaveVideoMode( width, height, width, height, false );
}

// software renderer isn't supported
qboolean SW_CreateBuffer( int width, int height, uint *stride, uint *bpp, uint *r, uint *g, uint *b )
{
	return false;
}

void *SW_LockBuffer( void )
{
	return NULL;
}

void SW_UnlockBuffer( void )
{
}

int R_MaxVideoModes( void )
{
	return 0;
}

vidmode_t *R_GetVideoMode( int num )
{
	return NULL;
}

void *GL_GetProcAddress( const char *name )
{
	return ps4_gl.get_proc_address ? ps4_gl.get_proc_address( name ) : NULL;
}

void GL_UpdateSwapInterval( void )
{
	// the backend always waits for vertical sync
	ClearBits( gl_vsync.flags, FCVAR_CHANGED );
}

void GL_SwapBuffers( void )
{
	if( ps4_gl.end_frame )
		ps4_gl.end_frame();

	gnm_dev_present();
	PS4_FramePresented();
}

// the context is always the same, see gnm_gl.c
int GL_SetAttribute( int attr, int val )
{
	return 0;
}

int GL_GetAttribute( int attr, int *val )
{
	switch( attr )
	{
	case REF_GL_RED_SIZE:
	case REF_GL_GREEN_SIZE:
	case REF_GL_BLUE_SIZE:
	case REF_GL_ALPHA_SIZE: *val = 8; return 0;
	case REF_GL_DEPTH_SIZE: *val = 24; return 0;
	case REF_GL_DOUBLEBUFFER: *val = 1; return 0;
	case REF_GL_CONTEXT_MAJOR_VERSION: *val = 2; return 0;
	case REF_GL_CONTEXT_PROFILE_MASK: *val = REF_GL_CONTEXT_PROFILE_ES; return 0;
	}

	*val = 0;
	return 0;
}

ref_window_type_t R_GetWindowHandle( void **handle, ref_window_type_t type )
{
	return REF_WINDOW_TYPE_NULL;
}

static void VID_Close( void )
{
	if( !ps4_gnm_opened )
		return;

	// the renderer frees its GPU memory while the device is still there
	if( ps4_gl.shutdown )
		ps4_gl.shutdown();

	memset( &ps4_gl, 0, sizeof( ps4_gl ));
	gnm_dev_shutdown();
	ps4_gnm_opened = false;
	host.hWnd = NULL;
}

/*
==================
VID_SetMode

Opens the display, its size is the size of the screen
==================
*/
qboolean VID_SetMode( void )
{
	char debug[16];
	int debuglevel = 0, w, h;

	if( ps4_gnm_opened )
		return true;

	// -gnmdebug [level]: GNM backend log, 1 - submits and textures, 2 - every draw
	if( Sys_GetParmFromCmdLine( "-gnmdebug", debug ))
		debuglevel = Q_atoi( debug );
	if( debuglevel <= 0 && Sys_CheckParm( "-gnmdebug" ))
		debuglevel = 1;

	gnm_dev_configure( debuglevel, Sys_CheckParm( "-gnmstats" ));

	if( !gnm_dev_init( ))
		return false;

	gnm_dev_get_size( &w, &h );

	ps4_gnm_opened = true;
	host.hWnd = (void *)&ps4_gnm_dummy_window;
	refState.window_mode = WINDOW_MODE_WINDOWED;
	VID_SaveWindowSize( w, h );
	SetBits( gl_vsync.flags, FCVAR_CHANGED );

	Con_Printf( "Video: " S_GREEN "PS4 GNM" S_DEFAULT ", %dx%d\n", w, h );
	return true;
}

rserr_t R_ChangeDisplaySettings( int width, int height, window_mode_t window_mode )
{
	// screen size is fixed
	return VID_SetMode() ? rserr_ok : rserr_invalid_context;
}

qboolean R_Init_Video( ref_graphic_apis_t type )
{
	if( type != REF_GL )
	{
		Con_Printf( S_ERROR "%s: only OpenGL renderers are supported\n", __func__ );
		return false;
	}

	refState.desktopBitsPixel = 32;
	glw_state.software = false;

	if( !VID_SetMode( ))
		return false;

	if( !PS4_GL_Load( ))
	{
		Con_Printf( S_ERROR "%s: renderer doesn't have the GNM OpenGL layer, use ref_gles3compat\n", __func__ );
		VID_Close();
		return false;
	}

	// -gnmmips 0|1|2: no mipmaps, power of two textures only, all textures (default)
	char mips[16];
	int mipmaps = 2;

	if( Sys_GetParmFromCmdLine( "-gnmmips", mips ))
		mipmaps = bound( 0, Q_atoi( mips ), 2 );

	if( !ps4_gl.init( gnm_dev_get_api(), !Sys_CheckParm( "-gnmnoflip" ), mipmaps ))
	{
		VID_Close();
		return false;
	}

	ref.dllFuncs.GL_InitExtensions();
	host.renderinfo_changed = false;
	return true;
}

void R_Free_Video( void )
{
	VID_Close();
	ref.dllFuncs.GL_ClearExtensions();
}

void VID_Info_f( void )
{
	Con_Printf( "Video: " S_GREEN "PS4 GNM" S_DEFAULT ", %dx%d\n", refState.width, refState.height );
}
