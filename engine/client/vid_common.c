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

#define WINDOW_NAME			XASH_ENGINE_NAME " Window" // Half-Life
convar_t	*vid_fullscreen;
convar_t	*vid_mode;
convar_t	*vid_brightness;
convar_t	*vid_gamma;
convar_t	*vid_highdpi;

glwstate_t	glw_state;

convar_t	*window_xpos;
convar_t	*window_ypos;

convar_t	*vid_rotate;
convar_t	*vid_scale;

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
	refState.width = 640;
	refState.height = 480;
}

/*
=================
R_SaveVideoMode
=================
*/
void R_SaveVideoMode( int w, int h, int render_w, int render_h )
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
	if( vid_mode < 0 || vid_mode > R_MaxVideoModes() )
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
	if( FBitSet( cl_allow_levelshots->flags, FCVAR_CHANGED ))
	{
		//GL_FreeTexture( cls.loadingBar );
		SCR_RegisterTextures(); // reload 'lambda' image
		ClearBits( cl_allow_levelshots->flags, FCVAR_CHANGED );
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
			Con_Print( S_ERROR "unable to set mode, backend returned null" );
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

	R_ChangeDisplaySettings( w, h, Cvar_VariableInteger( "fullscreen" ) );
}

void VID_Init( void )
{
	// system screen width and height (don't suppose for change from console at all)
	Cvar_Get( "width", "0", FCVAR_RENDERINFO|FCVAR_VIDRESTART, "screen width" );
	Cvar_Get( "height", "0", FCVAR_RENDERINFO|FCVAR_VIDRESTART, "screen height" );

	window_xpos = Cvar_Get( "_window_xpos", "-1", FCVAR_RENDERINFO, "window position by horizontal" );
	window_ypos = Cvar_Get( "_window_ypos", "-1", FCVAR_RENDERINFO, "window position by vertical" );

	vid_gamma = Cvar_Get( "gamma", "2.5", FCVAR_ARCHIVE, "gamma amount" );
	vid_brightness = Cvar_Get( "brightness", "0.0", FCVAR_ARCHIVE, "brightness factor" );
	vid_fullscreen = Cvar_Get( "fullscreen", "0", FCVAR_RENDERINFO|FCVAR_VIDRESTART, "enable fullscreen mode" );
	vid_mode = Cvar_Get( "vid_mode", "0", FCVAR_RENDERINFO, "current video mode index (used just for storage)" );
	vid_highdpi = Cvar_Get( "vid_highdpi", "1", FCVAR_RENDERINFO|FCVAR_VIDRESTART, "enable High-DPI mode" );
	vid_rotate = Cvar_Get( "vid_rotate", "0", FCVAR_RENDERINFO|FCVAR_VIDRESTART, "screen rotation (0-3)" );
	vid_scale = Cvar_Get( "vid_scale", "1.0", FCVAR_RENDERINFO|FCVAR_VIDRESTART, "pixel scale" );

	// a1ba: planned to be named vid_mode for compability
	// but supported mode list is filled by backends, so numbers are not portable any more
	Cmd_AddRestrictedCommand( "vid_setmode", VID_Mode_f, "display video mode" );

	R_Init(); // init renderer
}
