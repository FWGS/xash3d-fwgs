/*
platform_sdl3.h - SDL3 platform definitions
Copyright (C) 2025 Er2off
Copyright (C) 2025 Alibek Omarov

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include "platform_sdl3.h"
#include "eiface.h" // ARRAYSIZE
#include "vid_common.h" // window_{width,height}
#include "client.h" // refState
#include "input.h" // Touch_WantVisibleCursor
#include "vgui_draw.h" // VGui_UpdateInternalCursorState

static struct
{
	qboolean initialized;
	SDL_Cursor *cursors[dc_last];
} cursors;

static struct
{
	float x, y;
	qboolean pushed;
} in_visible_cursor_pos;

void SDLash_InitCursors( void )
{
	if( cursors.initialized )
		SDLash_FreeCursors();

	// load up all default cursors
	cursors.cursors[dc_none] = NULL;
	cursors.cursors[dc_arrow] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_DEFAULT );
	cursors.cursors[dc_ibeam] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_TEXT );
	cursors.cursors[dc_hourglass] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_WAIT );
	cursors.cursors[dc_crosshair] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_CROSSHAIR );
	cursors.cursors[dc_up] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_DEFAULT );
	cursors.cursors[dc_sizenwse] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_NWSE_RESIZE );
	cursors.cursors[dc_sizenesw] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_NESW_RESIZE );
	cursors.cursors[dc_sizewe] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_EW_RESIZE );
	cursors.cursors[dc_sizens] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_NS_RESIZE );
	cursors.cursors[dc_sizeall] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_MOVE );
	cursors.cursors[dc_no] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_NOT_ALLOWED );
	cursors.cursors[dc_hand] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_POINTER );
	cursors.initialized = true;
}

void SDLash_FreeCursors( void )
{
	if( !cursors.initialized )
		return;

	for( int i = 0; i < ARRAYSIZE( cursors.cursors ); i++ )
	{
		if( cursors.cursors[i] )
		{
			SDL_DestroyCursor( cursors.cursors[i] );
			cursors.cursors[i] = NULL;
		}
	}

	cursors.initialized = false;
}

qboolean Platform_GetMouseGrab( void )
{
	return SDL_GetWindowMouseGrab( host.hWnd );
}

void Platform_SetMouseGrab( qboolean enable )
{
	SDL_SetWindowMouseGrab( host.hWnd, enable );
}

void Platform_MouseMove( float *x, float *y )
{
	SDL_GetRelativeMouseState( x, y );
}

void GAME_EXPORT Platform_SetMousePos( int x, int y )
{
	SDL_WarpMouseInWindow( host.hWnd, x, y );
}

void GAME_EXPORT Platform_GetMousePos( int *x, int *y )
{
	float m_x = 0.0f, m_y = 0.0f;

	SDL_GetMouseState( &m_x, &m_y );

	if( x )
	{
		if( window_width.value && window_width.value != refState.width )
		{
			float factor = refState.width / window_width.value;
			*x = m_x * factor;
		}
		else *x = m_x;
	}

	if( y )
	{
		if( window_height.value && window_height.value != refState.height )
		{
			float factor = refState.height / window_height.value;
			*y = m_y * factor;
		}
		else *y = m_y;
	}
}

void Platform_SetCursorType( VGUI_DefaultCursor type )
{
	qboolean visible = type != dc_user && type != dc_none;

	// never disable cursor in touch emulation mode
	if( !visible && Touch_WantVisibleCursor( ))
		return;

	host.mouse_visible = visible;
	VGui_UpdateInternalCursorState( type );

	if( visible )
	{
		if( cursors.initialized )
			SDL_SetCursor( cursors.cursors[type] );

		SDL_ShowCursor();

		// restore the last mouse position
		if( in_visible_cursor_pos.pushed )
		{
			SDL_WarpMouseInWindow( host.hWnd, in_visible_cursor_pos.x, in_visible_cursor_pos.y );
			in_visible_cursor_pos.pushed = false;
		}
	}
	else
	{
		// save last mouse position and warp it to the center
		if( !in_visible_cursor_pos.pushed )
		{
			SDL_GetMouseState( &in_visible_cursor_pos.x, &in_visible_cursor_pos.y );
			SDL_WarpMouseInWindow( host.hWnd, host.window_center_x, host.window_center_y );
			in_visible_cursor_pos.pushed = true;
		}

		SDL_HideCursor();
	}
}

void Platform_EnableTextInput( qboolean enable )
{
	enable ? SDL_StartTextInput( host.hWnd ) : SDL_StopTextInput( host.hWnd );
}

int Platform_GetClipboardText( char *buffer, size_t size )
{
	int len;
	char *text = SDL_GetClipboardText();

	if( !text )
		return 0;

	if( buffer && size > 0 )
		len = Q_strncpy( buffer, text, size );
	else
		len = Q_strlen( text );

	SDL_free( text );

	return len;
}

void Platform_SetClipboardText( const char *buffer )
{
	SDL_SetClipboardText( buffer );
}

key_modifier_t Platform_GetKeyModifiers( void )
{
	SDL_Keymod mod_flags = SDL_GetModState();
	key_modifier_t result_flags = KeyModifier_None;

	if( FBitSet( mod_flags, SDL_KMOD_LCTRL ))
		SetBits( result_flags, KeyModifier_LeftCtrl );
	if( FBitSet( mod_flags, SDL_KMOD_RCTRL ))
		SetBits( result_flags, KeyModifier_RightCtrl );
	if( FBitSet( mod_flags, SDL_KMOD_RSHIFT ))
		SetBits( result_flags, KeyModifier_RightShift );
	if( FBitSet( mod_flags, SDL_KMOD_LSHIFT ))
		SetBits( result_flags, KeyModifier_LeftShift );
	if( FBitSet( mod_flags, SDL_KMOD_LALT ))
		SetBits( result_flags, KeyModifier_LeftAlt );
	if( FBitSet( mod_flags, SDL_KMOD_RALT ))
		SetBits( result_flags, KeyModifier_RightAlt );
	if( FBitSet( mod_flags, SDL_KMOD_NUM ))
		SetBits( result_flags, KeyModifier_NumLock );
	if( FBitSet( mod_flags, SDL_KMOD_CAPS ))
		SetBits( result_flags, KeyModifier_CapsLock );
	if( FBitSet( mod_flags, SDL_KMOD_RGUI ))
		SetBits( result_flags, KeyModifier_RightSuper );
	if( FBitSet( mod_flags, SDL_KMOD_LGUI ))
		SetBits( result_flags, KeyModifier_LeftSuper );

	return result_flags;
}
