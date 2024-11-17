/*
vid_common.c - common vid component
Copyright (C) 2018 a1batross, Uncle Mike

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
#include "mod_local.h"
#include "input.h"
#include "vid_common.h"
#include "platform/platform.h"

static CVAR_DEFINE_AUTO( vid_mode, "0", FCVAR_RENDERINFO, "current video mode index (used only for storage)" );
static CVAR_DEFINE_AUTO( vid_rotate, "0", FCVAR_RENDERINFO|FCVAR_VIDRESTART, "screen rotation (0-3)" );
static CVAR_DEFINE_AUTO( vid_scale, "1.0", FCVAR_RENDERINFO|FCVAR_VIDRESTART, "pixel scale" );

CVAR_DEFINE_AUTO( vid_highdpi, "1",  FCVAR_RENDERINFO|FCVAR_VIDRESTART, "enable High-DPI mode" );
CVAR_DEFINE_AUTO( vid_maximized, "0", FCVAR_RENDERINFO, "window maximized state, read-only" );
CVAR_DEFINE( vid_fullscreen, "fullscreen", DEFAULT_FULLSCREEN, FCVAR_RENDERINFO|FCVAR_VIDRESTART, "fullscreen state (0 windowed, 1 fullscreen, 2 borderless)" );
CVAR_DEFINE( window_width, "width", "0", FCVAR_RENDERINFO|FCVAR_VIDRESTART, "screen width" );
CVAR_DEFINE( window_height, "height", "0", FCVAR_RENDERINFO|FCVAR_VIDRESTART, "screen height" );
CVAR_DEFINE( window_xpos, "_window_xpos", "-1", FCVAR_RENDERINFO, "window position by horizontal" );
CVAR_DEFINE( window_ypos, "_window_ypos", "-1", FCVAR_RENDERINFO, "window position by vertical" );

glwstate_t	glw_state;

/*
=================
VID_InitDefaultResolution
=================
*/
void VID_InitDefaultResolution( void )
{
	// we need to have something valid here
	// until video subsystem initialized
	refState.width = 640;
	refState.height = 480;
}

/*
=================
R_SaveVideoMode
=================
*/
void R_SaveVideoMode( int w, int h, int render_w, int render_h, qboolean maximized )
{
	if( !w || !h || !render_w || !render_h )
	{
		host.renderinfo_changed = false;
		return;
	}

	host.window_center_x = w / 2;
	host.window_center_y = h / 2;

	Cvar_SetValue( "width", w );
	Cvar_SetValue( "height", h );
	Cvar_DirectSet( &vid_maximized, maximized ? "1" : "0" );
	
	// immediately drop changed state or we may trigger
	// video subsystem to reapply settings
	host.renderinfo_changed = false;

	if( refState.width == render_w && refState.height == render_h )
		return;

	refState.width = render_w;
	refState.height = render_h;

	// check for 4:3 or 5:4
	if( render_w * 3 != render_h * 4 && render_w * 4 != render_h * 5 )
		refState.wideScreen = true;
	else refState.wideScreen = false;

	SCR_VidInit(); // tell client.dll that vid_mode has changed
}

/*
=================
VID_GetModeString
=================
*/
const char *VID_GetModeString( int vid_mode )
{
	vidmode_t *vidmode;
	if( vid_mode < 0 || vid_mode >= R_MaxVideoModes() )
		return NULL;

	if( !( vidmode = R_GetVideoMode( vid_mode ) ) )
		return NULL;

	return vidmode->desc;
}

/*
==================
VID_CheckChanges

check vid modes and fullscreen
==================
*/
void VID_CheckChanges( void )
{
	if( FBitSet( cl_allow_levelshots.flags, FCVAR_CHANGED ))
	{
		//GL_FreeTexture( cls.loadingBar );
		SCR_RegisterTextures(); // reload 'lambda' image
		ClearBits( cl_allow_levelshots.flags, FCVAR_CHANGED );
	}

	if( host.renderinfo_changed )
	{
		if( VID_SetMode( ))
		{
			SCR_VidInit(); // tell the client.dll what vid_mode has changed
		}
		else
		{
			Sys_Error( "Can't re-initialize video subsystem\n" );
		}
		host.renderinfo_changed = false;
	}
}

/*
===============
VID_SetDisplayTransform

notify ref dll about screen transformations
===============
*/
void VID_SetDisplayTransform( int *render_w, int *render_h )
{
	uint rotate = vid_rotate.value;

	if( ref.dllFuncs.R_SetDisplayTransform( rotate, 0, 0, vid_scale.value, vid_scale.value ))
	{
		if( rotate & 1 )
		{
			int swap = *render_w;

			*render_w = *render_h;
			*render_h = swap;
		}

		*render_h /= vid_scale.value;
		*render_w /= vid_scale.value;
	}
	else
	{
		Con_Printf( S_WARN "failed to setup screen transform\n" );
	}
}

static void VID_Mode_f( void )
{
	int w, h;

	switch( Cmd_Argc() )
	{
	case 2:
	{
		vidmode_t *vidmode;

		vidmode = R_GetVideoMode( Q_atoi( Cmd_Argv( 1 )) );
		if( !vidmode )
		{
			Con_Printf( S_ERROR "unable to set mode, backend returned null" );
			return;
		}

		w = vidmode->width;
		h = vidmode->height;
		break;
	}
	case 3:
	{
		w = Q_atoi( Cmd_Argv( 1 ));
		h = Q_atoi( Cmd_Argv( 2 ));
		break;
	}
	default:
		Msg( S_USAGE "vid_mode <modenum>|<width height>\n" );
		return;
	}

	R_ChangeDisplaySettings( w, h, bound( 0, vid_fullscreen.value, WINDOW_MODE_COUNT - 1 ));
}

void VID_Init( void )
{
	// system screen width and height (don't suppose for change from console at all)
	Cvar_RegisterVariable( &window_width );
	Cvar_RegisterVariable( &window_height );

	Cvar_RegisterVariable( &vid_mode );
	Cvar_RegisterVariable( &vid_highdpi );
	Cvar_RegisterVariable( &vid_rotate );
	Cvar_RegisterVariable( &vid_scale );
	Cvar_RegisterVariable( &vid_fullscreen );
	Cvar_RegisterVariable( &vid_maximized );
	Cvar_RegisterVariable( &window_xpos );
	Cvar_RegisterVariable( &window_ypos );

	// a1ba: planned to be named vid_mode for compability
	// but supported mode list is filled by backends, so numbers are not portable any more
	Cmd_AddRestrictedCommand( "vid_setmode", VID_Mode_f, "display video mode" );

	V_Init(); // init gamma
	R_Init(); // init renderer
}
