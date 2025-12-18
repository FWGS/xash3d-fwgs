/*
host_sdl3.c - SDL3 host
Copyright (C) 2025 Xash3D FWGS contributors

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
#include "ref_common.h"
#include "vid_common.h"
#include "client.h"
#include "input.h"
#include "vgui_draw.h"

static void SDLash_ActiveEvent( qboolean focus )
{
	if( focus )
	{
		host.status = HOST_FRAME;
		if( cls.key_dest == key_game )
			IN_ActivateMouse();

		host.force_draw_version_time = host.realtime + FORCE_DRAW_VERSION_TIME;
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
	}
}

static void SDLash_KeyEvent( const SDL_KeyboardEvent *key )
{
	int keynum = key->scancode;

	if( SDL_TextInputActive( host.hWnd ) && key->down )
	{
		// this is how engine understands ctrl+c, ctrl+v and other hotkeys
		if( cls.key_dest != key_game && FBitSet( SDL_GetModState( ), SDL_KMOD_CTRL ))
		{
			if( keynum >= SDL_SCANCODE_A && keynum <= SDL_SCANCODE_Z )
			{
				keynum = keynum - SDL_SCANCODE_A + 1;
				CL_CharEvent( keynum );
			}

			return;
		}

		// ignore printable keys, they are coming through SDL_EVENT_TEXT_INPUT
		if(( keynum >= SDL_SCANCODE_A && keynum <= SDL_SCANCODE_Z )
			|| ( keynum >= SDL_SCANCODE_1 && keynum <= SDL_SCANCODE_0 )
			|| ( keynum >= SDL_SCANCODE_KP_1 && keynum <= SDL_SCANCODE_KP_0 ))
			return;
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
		qboolean num_lock = FBitSet( SDL_GetModState( ), SDL_KMOD_NUM );

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
		case SDL_SCANCODE_AC_BACK:
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
		case SDL_SCANCODE_KP_1: keynum = num_lock ? '1' : K_KP_END; break;
		case SDL_SCANCODE_KP_2: keynum = num_lock ? '2' : K_KP_DOWNARROW; break;
		case SDL_SCANCODE_KP_3: keynum = num_lock ? '3' : K_KP_PGDN; break;
		case SDL_SCANCODE_KP_4: keynum = num_lock ? '4' : K_KP_LEFTARROW; break;
		case SDL_SCANCODE_KP_5: keynum = num_lock ? '5' : K_KP_5; break;
		case SDL_SCANCODE_KP_6: keynum = num_lock ? '6' : K_KP_RIGHTARROW; break;
		case SDL_SCANCODE_KP_7: keynum = num_lock ? '7' : K_KP_HOME; break;
		case SDL_SCANCODE_KP_8: keynum = num_lock ? '8' : K_KP_UPARROW; break;
		case SDL_SCANCODE_KP_9: keynum = num_lock ? '9' : K_KP_PGUP; break;
		case SDL_SCANCODE_KP_0: keynum = num_lock ? '0' : K_KP_INS; break;
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
		case SDL_SCANCODE_PAUSE: keynum = K_PAUSE; break;
		case SDL_SCANCODE_SCROLLLOCK: keynum = K_SCROLLLOCK; break;
		case SDL_SCANCODE_APPLICATION: keynum = K_WIN; break; // (compose key) ???
		case SDL_SCANCODE_VOLUMEUP: keynum = K_AUX32; break;
		case SDL_SCANCODE_VOLUMEDOWN: keynum = K_AUX31; break;
		// don't console spam on known functional buttons, not used in engine
		case SDL_SCANCODE_MUTE:
		case SDL_SCANCODE_SELECT:
			return;
		case SDL_SCANCODE_UNKNOWN:
		{
			if( key->down )
				Con_Reportf( "%s: Unknown scancode\n", __func__ );
			return;
		}
		default:
			if( key->down )
				Con_Reportf( "%s: Unknown key: %s = %i\n", __func__, SDL_GetScancodeName( keynum ), keynum );
			return;
		}
	}
#undef DECLARE_KEY_RANGE

	Key_Event( keynum, key->down );
}

static void SDLash_TextEvent( const SDL_TextInputEvent *text )
{
	VGui_ReportTextInput( text->text );

	for( const char *s = text->text; *s; s++ )
	{
		int ch = (byte)*s;

		// convert to single byte encoding if game doesn't request UTF-8
		if( !cls.accept_utf8 )
			ch = Con_UtfProcessCharForce( ch );

		if( !ch )
			continue;

		CL_CharEvent( ch );
	}
}

static void SDLash_MouseEvent( const SDL_MouseButtonEvent *button )
{
	if( button->which == SDL_TOUCH_MOUSEID )
		return;

	int down;
	if( !button->down )
		down = 0;
	else if( button->clicks >= 2 )
		down = 2;
	else
		down = 1;

	switch( button->button )
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
	default:
		Con_Printf( "Unknown mouse button ID: %d\n", button->button );
		break;
	}
}

static void SDLash_TouchEvent( const SDL_TouchFingerEvent *touch )
{
	touchEventType type;

	switch( touch->type )
	{
	case SDL_EVENT_FINGER_DOWN:
		type = event_down;
		break;
	case SDL_EVENT_FINGER_UP:
		type = event_up;
		break;
	case SDL_EVENT_FINGER_MOTION:
		type = event_motion;
		break;
	default:
		return;
	}

	IN_TouchEvent( type, touch->fingerID, touch->x, touch->y, touch->dx, touch->dy );
}

static void SDLash_EventHandler( const SDL_Event *ev )
{
	switch( ev->type )
	{
	case SDL_EVENT_QUIT:
		Sys_Quit( "caught SDL_EVENT_QUIT" );
		break;
	// TODO: use SDL_AddEventWatch
	// case SDL_EVENT_TERMINATING:
	//	Host_ShutdownWithReason( "caught SDL_EVENT_TERMINATING" );
	//	break;
	case SDL_EVENT_WINDOW_MOVED:
		if( refState.window_mode == WINDOW_MODE_WINDOWED )
			Cvar_DirectSet( &vid_maximized, "0" );
		break;
	case SDL_EVENT_WINDOW_MINIMIZED:
		host.status = HOST_SLEEP;
		break;
	case SDL_EVENT_WINDOW_RESTORED:
		host.status = HOST_FRAME;
		host.force_draw_version_time = host.realtime + FORCE_DRAW_VERSION_TIME;
		break;
	case SDL_EVENT_WINDOW_FOCUS_GAINED:
		SDLash_ActiveEvent( true );
		break;
	case SDL_EVENT_WINDOW_FOCUS_LOST:
		SDLash_ActiveEvent( false );
		break;
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
		SDLash_KeyEvent( &ev->key );
		break;
	case SDL_EVENT_TEXT_INPUT:
		SDLash_TextEvent( &ev->text );
		break;
	case SDL_EVENT_MOUSE_MOTION:
		if( host.mouse_visible )
			SDL_GetRelativeMouseState( NULL, NULL );
		break;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:
		SDLash_MouseEvent( &ev->button );
		break;
	case SDL_EVENT_MOUSE_WHEEL:
		IN_MWheelEvent( ev->wheel.integer_y );
		break;
	case SDL_EVENT_GAMEPAD_AXIS_MOTION:
	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
	case SDL_EVENT_GAMEPAD_BUTTON_UP:
	case SDL_EVENT_GAMEPAD_ADDED:
	case SDL_EVENT_GAMEPAD_REMOVED:
	case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
	case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:
	case SDL_EVENT_GAMEPAD_TOUCHPAD_UP:
	case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
		// TODO:
		break;
	case SDL_EVENT_FINGER_DOWN:
	case SDL_EVENT_FINGER_UP:
	case SDL_EVENT_FINGER_MOTION:
		SDLash_TouchEvent( &ev->tfinger );
		break;
	}
}

void Platform_RunEvents( void )
{
	SDL_Event ev;

	while( host.status != HOST_CRASHED && !host.shutdown_issued && SDL_PollEvent( &ev ))
		SDLash_EventHandler( &ev );
}

void Platform_PreCreateMove( void )
{
	// TODO
}
