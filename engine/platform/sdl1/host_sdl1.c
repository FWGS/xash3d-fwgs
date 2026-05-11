/*
host_sdl1.c - SDL event system handlers
Copyright (C) 2015-2025 a1batross

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
#include <ctype.h>

#include "common.h"
#include "keydefs.h"
#include "input.h"
#include "client.h"
#include "vgui_draw.h"
#include "platform_sdl1.h"
#include "sound.h"
#include "vid_common.h"

#define SDL_SCANCODE_A SDLK_a
#define SDL_SCANCODE_Z SDLK_z
#define SDL_SCANCODE_1 SDLK_1
#define SDL_SCANCODE_9 SDLK_9
#define SDL_SCANCODE_F1 SDLK_F1
#define SDL_SCANCODE_F12 SDLK_F12
#define SDL_SCANCODE_GRAVE SDLK_BACKQUOTE
#define SDL_SCANCODE_0 SDLK_0
#define SDL_SCANCODE_BACKSLASH SDLK_BACKSLASH
#define SDL_SCANCODE_LEFTBRACKET SDLK_LEFTBRACKET
#define SDL_SCANCODE_RIGHTBRACKET SDLK_RIGHTBRACKET
#define SDL_SCANCODE_EQUALS SDLK_EQUALS
#define SDL_SCANCODE_MINUS SDLK_MINUS
#define SDL_SCANCODE_TAB SDLK_TAB
#define SDL_SCANCODE_RETURN SDLK_RETURN
#define SDL_SCANCODE_ESCAPE SDLK_ESCAPE
#define SDL_SCANCODE_SPACE SDLK_SPACE
#define SDL_SCANCODE_BACKSPACE SDLK_BACKSPACE
#define SDL_SCANCODE_UP SDLK_UP
#define SDL_SCANCODE_LEFT SDLK_LEFT
#define SDL_SCANCODE_DOWN SDLK_DOWN
#define SDL_SCANCODE_RIGHT SDLK_RIGHT
#define SDL_SCANCODE_LALT SDLK_LALT
#define SDL_SCANCODE_RALT SDLK_RALT
#define SDL_SCANCODE_LCTRL SDLK_LCTRL
#define SDL_SCANCODE_RCTRL SDLK_RCTRL
#define SDL_SCANCODE_LSHIFT SDLK_LSHIFT
#define SDL_SCANCODE_RSHIFT SDLK_RSHIFT
#define SDL_SCANCODE_LGUI SDLK_LMETA
#define SDL_SCANCODE_RGUI SDLK_RMETA
#define SDL_SCANCODE_INSERT SDLK_INSERT
#define SDL_SCANCODE_DELETE SDLK_DELETE
#define SDL_SCANCODE_PAGEDOWN SDLK_PAGEDOWN
#define SDL_SCANCODE_PAGEUP SDLK_PAGEUP
#define SDL_SCANCODE_HOME SDLK_HOME
#define SDL_SCANCODE_END SDLK_END
#define SDL_SCANCODE_KP_1 SDLK_KP1
#define SDL_SCANCODE_KP_2 SDLK_KP2
#define SDL_SCANCODE_KP_3 SDLK_KP3
#define SDL_SCANCODE_KP_4 SDLK_KP4
#define SDL_SCANCODE_KP_5 SDLK_KP5
#define SDL_SCANCODE_KP_6 SDLK_KP6
#define SDL_SCANCODE_KP_7 SDLK_KP7
#define SDL_SCANCODE_KP_8 SDLK_KP8
#define SDL_SCANCODE_KP_9 SDLK_KP9
#define SDL_SCANCODE_KP_0 SDLK_KP0
#define SDL_SCANCODE_KP_PERIOD SDLK_KP_PERIOD
#define SDL_SCANCODE_KP_ENTER SDLK_KP_ENTER
#define SDL_SCANCODE_KP_PLUS SDLK_KP_PLUS
#define SDL_SCANCODE_KP_MINUS SDLK_KP_MINUS
#define SDL_SCANCODE_KP_DIVIDE SDLK_KP_DIVIDE
#define SDL_SCANCODE_KP_MULTIPLY SDLK_KP_MULTIPLY
#define SDL_SCANCODE_NUMLOCKCLEAR SDLK_NUMLOCK
#define SDL_SCANCODE_CAPSLOCK SDLK_CAPSLOCK
#define SDL_SCANCODE_SLASH SDLK_SLASH
#define SDL_SCANCODE_PERIOD SDLK_PERIOD
#define SDL_SCANCODE_SEMICOLON SDLK_SEMICOLON
#define SDL_SCANCODE_APOSTROPHE SDLK_QUOTE
#define SDL_SCANCODE_COMMA SDLK_COMMA
#define SDL_SCANCODE_PRINTSCREEN SDLK_PRINT
#define SDL_SCANCODE_UNKNOWN SDLK_UNKNOWN
#define SDL_GetScancodeName( x ) "unknown"

/*
=============
SDLash_KeyEvent

=============
*/
static void SDLash_KeyEvent( SDL_KeyboardEvent key )
{
	int down = key.state != SDL_RELEASED;
	int keynum = key.keysym.sym;

	if( host.textmode && down )
	{
		// this is how engine understands ctrl+c, ctrl+v and other hotkeys
		if( cls.key_dest != key_game && FBitSet( SDL_GetModState(), KMOD_CTRL ))
		{
			if( keynum >= SDL_SCANCODE_A && keynum <= SDL_SCANCODE_Z )
			{
				keynum = keynum - SDL_SCANCODE_A + 1;
				CL_CharEvent( keynum );
			}

			return;
		}

		if( keynum >= SDLK_KP0 && keynum <= SDLK_KP9 )
			keynum -= SDLK_KP0 + '0';

		if( isprint( keynum ))
		{
			if( FBitSet( SDL_GetModState(), KMOD_SHIFT ))
				keynum = Key_ToUpper( keynum );

			CL_CharEvent( keynum );
		}
	}

#define DECLARE_KEY_RANGE( min, max, repl ) \
	if( keynum >= (min) && keynum <= (max) ) \
	{ \
		keynum = keynum - (min) + (repl); \
	}

	DECLARE_KEY_RANGE( SDL_SCANCODE_A, SDL_SCANCODE_Z, 'a' )
	else DECLARE_KEY_RANGE( SDL_SCANCODE_1, SDL_SCANCODE_9, '1' )
	else DECLARE_KEY_RANGE( SDL_SCANCODE_F1, SDL_SCANCODE_F12, K_F1 )
	else
	{
		qboolean numLock = FBitSet( SDL_GetModState(), KMOD_NUM );

		switch( keynum )
		{
		case SDL_SCANCODE_GRAVE: keynum = '`'; break;
		case SDL_SCANCODE_0: keynum = '0'; break;
		case SDL_SCANCODE_BACKSLASH: keynum = '\\'; break;
		case SDL_SCANCODE_LEFTBRACKET: keynum = '['; break;
		case SDL_SCANCODE_RIGHTBRACKET: keynum = ']'; break;
		case SDL_SCANCODE_EQUALS: keynum = '='; break;
		case SDL_SCANCODE_MINUS: keynum = '-'; break;
		case SDL_SCANCODE_TAB: keynum = K_TAB; break;
		case SDL_SCANCODE_RETURN: keynum = K_ENTER; break;
		case SDL_SCANCODE_ESCAPE: keynum = K_ESCAPE; break;
		case SDL_SCANCODE_SPACE: keynum = K_SPACE; break;
		case SDL_SCANCODE_BACKSPACE: keynum = K_BACKSPACE; break;
		case SDL_SCANCODE_UP: keynum = K_UPARROW; break;
		case SDL_SCANCODE_LEFT: keynum = K_LEFTARROW; break;
		case SDL_SCANCODE_DOWN: keynum = K_DOWNARROW; break;
		case SDL_SCANCODE_RIGHT: keynum = K_RIGHTARROW; break;
		case SDL_SCANCODE_LALT:
		case SDL_SCANCODE_RALT: keynum = K_ALT; break;
		case SDL_SCANCODE_LCTRL:
		case SDL_SCANCODE_RCTRL: keynum = K_CTRL; break;
		case SDL_SCANCODE_LSHIFT:
		case SDL_SCANCODE_RSHIFT: keynum = K_SHIFT; break;
		case SDL_SCANCODE_LGUI:
		case SDL_SCANCODE_RGUI: keynum = K_WIN; break;
		case SDL_SCANCODE_INSERT: keynum = K_INS; break;
		case SDL_SCANCODE_DELETE: keynum = K_DEL; break;
		case SDL_SCANCODE_PAGEDOWN: keynum = K_PGDN; break;
		case SDL_SCANCODE_PAGEUP: keynum = K_PGUP; break;
		case SDL_SCANCODE_HOME: keynum = K_HOME; break;
		case SDL_SCANCODE_END: keynum = K_END; break;
		case SDL_SCANCODE_KP_1: keynum = numLock ? '1' : K_KP_END; break;
		case SDL_SCANCODE_KP_2: keynum = numLock ? '2' : K_KP_DOWNARROW; break;
		case SDL_SCANCODE_KP_3: keynum = numLock ? '3' : K_KP_PGDN; break;
		case SDL_SCANCODE_KP_4: keynum = numLock ? '4' : K_KP_LEFTARROW; break;
		case SDL_SCANCODE_KP_5: keynum = numLock ? '5' : K_KP_5; break;
		case SDL_SCANCODE_KP_6: keynum = numLock ? '6' : K_KP_RIGHTARROW; break;
		case SDL_SCANCODE_KP_7: keynum = numLock ? '7' : K_KP_HOME; break;
		case SDL_SCANCODE_KP_8: keynum = numLock ? '8' : K_KP_UPARROW; break;
		case SDL_SCANCODE_KP_9: keynum = numLock ? '9' : K_KP_PGUP; break;
		case SDL_SCANCODE_KP_0: keynum = numLock ? '0' : K_KP_INS; break;
		case SDL_SCANCODE_KP_PERIOD: keynum = K_KP_DEL; break;
		case SDL_SCANCODE_KP_ENTER: keynum = K_KP_ENTER; break;
		case SDL_SCANCODE_KP_PLUS: keynum = K_KP_PLUS; break;
		case SDL_SCANCODE_KP_MINUS: keynum = K_KP_MINUS; break;
		case SDL_SCANCODE_KP_DIVIDE: keynum = K_KP_SLASH; break;
		case SDL_SCANCODE_KP_MULTIPLY: keynum = '*'; break;
		case SDL_SCANCODE_NUMLOCKCLEAR: keynum = K_KP_NUMLOCK; break;
		case SDL_SCANCODE_CAPSLOCK: keynum = K_CAPSLOCK; break;
		case SDL_SCANCODE_SLASH: keynum = '/'; break;
		case SDL_SCANCODE_PERIOD: keynum = '.'; break;
		case SDL_SCANCODE_SEMICOLON: keynum = ';'; break;
		case SDL_SCANCODE_APOSTROPHE: keynum = '\''; break;
		case SDL_SCANCODE_COMMA: keynum = ','; break;
		case SDL_SCANCODE_PRINTSCREEN:
		{
			host.force_draw_version_time = host.realtime + FORCE_DRAW_VERSION_TIME;
			break;
		}
		case SDL_SCANCODE_UNKNOWN:
		{
			if( down ) Con_Reportf( "%s: Unknown scancode\n", __func__ );
			return;
		}
		default:
			if( down ) Con_Reportf( "%s: Unknown key: %s = %i\n", __func__, SDL_GetScancodeName( keynum ), keynum );
			return;
		}
	}

#undef DECLARE_KEY_RANGE

	Key_Event( keynum, down );
}

/*
=============
SDLash_MouseEvent

=============
*/
static void SDLash_MouseEvent( SDL_MouseButtonEvent button )
{
	int down;

	if( button.state == SDL_RELEASED )
		down = 0;
	else
		down = 1;

	switch( button.button )
	{
	case SDL_BUTTON_LEFT:
		IN_MouseEvent( 0, down );
		break;
	case SDL_BUTTON_RIGHT:
		IN_MouseEvent( 1, down );
		break;
	case SDL_BUTTON_MIDDLE:
		IN_MouseEvent( 2, down );
		break;
	case SDL_BUTTON_X1:
		IN_MouseEvent( 3, down );
		break;
	case SDL_BUTTON_X2:
		IN_MouseEvent( 4, down );
		break;
	case SDL_BUTTON_WHEELUP:
		IN_MWheelEvent( -1 );
		break;
	case SDL_BUTTON_WHEELDOWN:
		IN_MWheelEvent( 1 );
		break;
	default:
		Con_Printf( "Unknown mouse button ID: %d\n", button.button );
	}
}

static void SDLash_ActiveEvent( int gain )
{
	if( gain )
	{
		host.status = HOST_FRAME;
		if( cls.key_dest == key_game )
			IN_ActivateMouse( );

		host.force_draw_version_time = host.realtime + FORCE_DRAW_VERSION_TIME;
		if( vid_fullscreen.value == WINDOW_MODE_FULLSCREEN )
			VID_SetMode();
	}
	else
	{
		host.status = HOST_NOFOCUS;

		if( cls.key_dest == key_game )
		{
			Key_ClearStates();
			IN_DeactivateMouse();
		}

		host.force_draw_version_time = host.realtime + 2.0;
		VID_RestoreScreenResolution();
	}
}

/*
=============
SDLash_EventFilter

=============
*/
static void SDLash_EventHandler( SDL_Event *event )
{
	switch ( event->type )
	{
	/* Mouse events */
	case SDL_MOUSEMOTION:
		if( host.mouse_visible )
			SDL_GetRelativeMouseState( NULL, NULL );
		break;

	case SDL_MOUSEBUTTONUP:
	case SDL_MOUSEBUTTONDOWN:
		SDLash_MouseEvent( event->button );
		break;

	/* Keyboard events */
	case SDL_KEYDOWN:
	case SDL_KEYUP:
		SDLash_KeyEvent( event->key );
		break;

	case SDL_QUIT:
		Sys_Quit( "caught SDL_QUIT" );
		break;
	case SDL_VIDEORESIZE:
		VID_SaveWindowSize( event->resize.w, event->resize.h, false );
		break;
	case SDL_ACTIVEEVENT:
		SDLash_ActiveEvent( event->active.gain );
		break;
	}
}

/*
=============
SDLash_RunEvents

=============
*/
void Platform_RunEvents( void )
{
	SDL_Event event;

	while( host.status != HOST_CRASHED && !host.shutdown_issued && SDL_PollEvent( &event ) )
		SDLash_EventHandler( &event );
}

/*
========================
Platform_PreCreateMove

this should disable mouse look on client when m_ignore enabled
TODO: kill mouse in win32 clients too
========================
*/
void Platform_PreCreateMove( void )
{
	if( m_ignore.value )
	{
		SDL_GetRelativeMouseState( NULL, NULL );
		SDL_ShowCursor( SDL_TRUE );
	}
}
