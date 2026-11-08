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
The engine owns the display and the GPU device (platform/ps4/gnm), like it owns the GL context
on SDL. OpenGL renderers get the device with R_GetWindowHandle( REF_WINDOW_TYPE_GNM ) and draw
through the fixed function OpenGL subset implemented by ref_gl (ref/gl/gnm_shim) over the device.
The screen has a fixed size and there are no windows
*/

#include "platform/platform.h"
#include "client.h"
#include "vid_common.h"
#include "ps4/gnm/gnm_engine.h"

static int ps4_gnm_dummy_window; // host.hWnd must be non-NULL while the context exists
static qboolean ps4_gnm_opened;

void Platform_Minimize_f( void )
{
}

platform_orientation_t Platform_GetDisplayOrientation( void )
{
	return ORIENTATION_LANDSCAPE;
}

static void VID_SaveWindowSize( int width, int height )
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

// OpenGL is implemented by the renderer itself, see ref/gl/gnm_shim
void *GL_GetProcAddress( const char *name )
{
	return NULL;
}

void GL_UpdateSwapInterval( void )
{
	// the backend always waits for vertical sync
	ClearBits( gl_vsync.flags, FCVAR_CHANGED );
}

void GL_SwapBuffers( void )
{
	gnm_dev_present();
	PS4_FramePresented();
}

// the context is always the same, see gnm_shim.c
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
	if( !ps4_gnm_opened )
		return REF_WINDOW_TYPE_NULL;

	if( !type || type == REF_WINDOW_TYPE_GNM )
	{
		if( handle )
			*handle = (void *)gnm_dev_get_api();
		return REF_WINDOW_TYPE_GNM;
	}

	return REF_WINDOW_TYPE_NULL;
}

static void VID_Close( void )
{
	if( !ps4_gnm_opened )
		return;

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
	char debug[16], mips[16];
	int debuglevel = 0, mipmaps = 2, w, h;

	if( ps4_gnm_opened )
		return true;

	// -gnmdebug [level]: GNM backend log, 1 - submits and textures, 2 - every draw
	if( Sys_GetParmFromCmdLine( "-gnmdebug", debug ))
		debuglevel = Q_atoi( debug );
	if( debuglevel <= 0 && Sys_CheckParm( "-gnmdebug" ))
		debuglevel = 1;

	// -gnmmips 0|1|2: no mipmaps, power of two textures only, all textures (default)
	if( Sys_GetParmFromCmdLine( "-gnmmips", mips ))
		mipmaps = bound( 0, Q_atoi( mips ), 2 );

	gnm_dev_configure( debuglevel, Sys_CheckParm( "-gnmstats" ), !Sys_CheckParm( "-gnmnoflip" ), mipmaps );

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
