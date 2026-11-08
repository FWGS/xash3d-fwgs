/*
vid_sailfish.c - Sailfish OS window orientation and keyboard panning
Copyright (C) 2026 Alibek Omarov

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
#include <wayland-client.h>
#include "common.h"
#include "client.h"
#include "platform/platform.h"

// it breaks definitions in net_api.h when it's included before
#include <SDL_syswm.h>

// SDL's ID for touches from qt_touch_extension, Lipstick sends them there instead of wl_touch once SDL binds it
#define QT_TOUCH_DEVICE 1

static struct
{
	struct wl_surface *surface;
	int rotation;

	// qt_touch_extension normalizes touches to the whole panel
	int panel_w, panel_h;

	// window size without the keyboard
	int width, height;
	int render_w, render_h;

	// what the keyboard leaves of it
	int visible_h;

	// edited text, in render coordinates
	int text_y, text_h;

	int pan;
} s_vid;

static int Sailfish_Pan( void )
{
	int hidden = s_vid.render_h - s_vid.visible_h;

	if( hidden <= 0 || s_vid.text_h <= 0 )
		return 0;

	// just enough to bring the bottom of the edited text above the keyboard, but never past its top
	int pan = Q_min( s_vid.text_y + s_vid.text_h - s_vid.visible_h, s_vid.text_y );

	return bound( 0, pan, hidden );
}

/*
========================
Sailfish_SetOrientation

========================
*/
void Sailfish_SetOrientation( int rotate )
{
	static const enum wl_output_transform transforms[] =
	{
		[REF_ROTATE_NONE] = WL_OUTPUT_TRANSFORM_NORMAL,
		[REF_ROTATE_CW] = WL_OUTPUT_TRANSFORM_90,
		[REF_ROTATE_UD] = WL_OUTPUT_TRANSFORM_180,
		[REF_ROTATE_CCW] = WL_OUTPUT_TRANSFORM_270,
	};
	SDL_SysWMinfo info;

	SDL_VERSION( &info.version );

	if( !host.hWnd || !SDL_GetWindowWMInfo( host.hWnd, &info ) || info.subsystem != SDL_SYSWM_WAYLAND )
		return;

	// Lipstick rotates the window by the buffer transform, requests a landscape size for it
	// and takes the keyboard height off the edge that is at the bottom of the picture
	if( s_vid.surface != info.info.wl.surface || s_vid.rotation != rotate )
	{
		wl_surface_set_buffer_transform( info.info.wl.surface, transforms[rotate] );
		s_vid.surface = info.info.wl.surface;
		s_vid.rotation = rotate;
	}

	SDL_DisplayMode mode;

	if( SDL_GetDesktopDisplayMode( SDL_GetWindowDisplayIndex( host.hWnd ), &mode ) == 0 )
	{
		s_vid.panel_w = mode.w;
		s_vid.panel_h = mode.h;
	}
}

/*
========================
Sailfish_OrientationAngle

========================
*/
int Sailfish_OrientationAngle( void )
{
	// the same angle Qt reports for the content orientation Lipstick derives from the buffer transform
	return s_vid.rotation * 90;
}

/*
========================
Sailfish_KeepWindowSize

========================
*/
void Sailfish_KeepWindowSize( int *width, int *height, int *render_w, int *render_h )
{
	// Lipstick shrinks the window under the keyboard, the picture keeps its size and gets panned instead
	// the keyboard only takes the height, anything else is a real resize
	if( *render_w != s_vid.render_w || *render_h >= s_vid.render_h )
	{
		s_vid.width = *width;
		s_vid.height = *height;
		s_vid.render_w = *render_w;
		s_vid.render_h = *render_h;
	}

	s_vid.visible_h = *render_h;

	*width = s_vid.width;
	*height = s_vid.height;
	*render_w = s_vid.render_w;
	*render_h = s_vid.render_h;
}

/*
========================
Sailfish_GetDisplayOffset

========================
*/
void Sailfish_GetDisplayOffset( int *x, int *y )
{
	s_vid.pan = Sailfish_Pan();

	// the picture is taller than the window under the keyboard, its bottom goes below the window's bottom
	*x = 0;
	*y = s_vid.visible_h + s_vid.pan - s_vid.render_h;
}

/*
========================
Sailfish_SetTextInputArea

========================
*/
void Sailfish_SetTextInputArea( int y, int h )
{
	s_vid.text_y = y;
	s_vid.text_h = h;

	if( !ref.initialized || Sailfish_Pan() == s_vid.pan )
		return;

	int offset_x, offset_y;

	Sailfish_GetDisplayOffset( &offset_x, &offset_y );
	ref.dllFuncs.R_SetDisplayTransform( ref.rotation, offset_x, offset_y, 1.0f, 1.0f );
}

/*
========================
Sailfish_MapTouch

========================
*/
void Sailfish_MapTouch( int64_t device, float *x, float *y, float *dx, float *dy )
{
	float wx, wy, wdx, wdy;

	if( s_vid.render_w <= 0 || s_vid.render_h <= 0 )
		return;

	// to window pixels
	if( device == QT_TOUCH_DEVICE && s_vid.panel_w > 0 && s_vid.panel_h > 0 )
	{
		float px = *x * s_vid.panel_w;
		float py = *y * s_vid.panel_h;
		float pdx = *dx * s_vid.panel_w;
		float pdy = *dy * s_vid.panel_h;

		// the panel is portrait, Lipstick puts the window at its right edge for 90 degrees and at its left edge for 270,
		// the room it leaves along the panel is assumed to be all taken by the top cutout
		switch( s_vid.rotation )
		{
		case REF_ROTATE_CW:
			wx = py - ( s_vid.panel_h - s_vid.render_w );
			wy = s_vid.panel_w - px;
			wdx = pdy;
			wdy = -pdx;
			break;
		case REF_ROTATE_CCW:
			wx = s_vid.panel_h - py;
			wy = px;
			wdx = -pdy;
			wdy = pdx;
			break;
		default:
			wx = px;
			wy = py - ( s_vid.panel_h - s_vid.render_h );
			wdx = pdx;
			wdy = pdy;
			break;
		}
	}
	else
	{
		wx = *x * s_vid.render_w;
		wy = *y * s_vid.visible_h;
		wdx = *dx * s_vid.render_w;
		wdy = *dy * s_vid.visible_h;
	}

	// to the panned picture
	*x = wx / s_vid.render_w;
	*y = ( wy + s_vid.pan ) / s_vid.render_h;
	*dx = wdx / s_vid.render_w;
	*dy = wdy / s_vid.render_h;
}
