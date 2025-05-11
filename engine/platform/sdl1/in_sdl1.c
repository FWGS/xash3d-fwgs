/*
in_sdl1.c - SDL input component
Copyright (C) 2018-2025 a1batross

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
#include "keydefs.h"
#include "input.h"
#include "client.h"
#include "vgui_draw.h"
#include "platform_sdl1.h"
#include "sound.h"
#include "vid_common.h"

#define SDL_WarpMouseInWindow( win, x, y ) SDL_WarpMouse( ( x ), ( y ) )

/*
=============
Platform_GetMousePos

=============
*/
void GAME_EXPORT Platform_GetMousePos( int *x, int *y )
{
	SDL_GetMouseState( x, y );

	if( x && window_width.value && window_width.value != refState.width )
	{
		float factor = refState.width / window_width.value;
		*x = *x * factor;
	}

	if( y && window_height.value && window_height.value != refState.height )
	{
		float factor = refState.height / window_height.value;
		*y = *y * factor;
	}
}

/*
=============
Platform_SetMousePos

============
*/
void GAME_EXPORT Platform_SetMousePos( int x, int y )
{
	SDL_WarpMouseInWindow( host.hWnd, x, y );
}

/*
========================
Platform_MouseMove

========================
*/
void Platform_MouseMove( float *x, float *y )
{
	int m_x, m_y;
	SDL_GetRelativeMouseState( &m_x, &m_y );
	*x = (float)m_x;
	*y = (float)m_y;
}

/*
=============
Platform_GetClipobardText

=============
*/
int Platform_GetClipboardText( char *buffer, size_t size )
{
	buffer[0] = 0;
	return 0;
}

/*
=============
Platform_SetClipobardText

=============
*/
void Platform_SetClipboardText( const char *buffer )
{
}

/*
========================
Platform_SetCursorType

========================
*/
void Platform_SetCursorType( VGUI_DefaultCursor type )
{
	qboolean visible;

	switch( type )
	{
		case dc_user:
		case dc_none:
			visible = false;
			break;
		default:
			visible = true;
			break;
	}

	// never disable cursor in touch emulation mode
	if( !visible && Touch_WantVisibleCursor( ))
		return;

	host.mouse_visible = visible;
	VGui_UpdateInternalCursorState( type );

	if( host.mouse_visible )
	{
		SDL_ShowCursor( true );
	}
	else
	{
		SDL_ShowCursor( false );
	}
}

/*
========================
Platform_GetMouseGrab
========================
*/
qboolean Platform_GetMouseGrab( void )
{
	return SDL_WM_GrabInput( SDL_GRAB_QUERY ) != SDL_GRAB_OFF;
}

/*
========================
Platform_SetMouseGrab
========================
*/
void Platform_SetMouseGrab( qboolean enable )
{
	SDL_WM_GrabInput( enable ? SDL_GRAB_ON : SDL_GRAB_OFF );
}
