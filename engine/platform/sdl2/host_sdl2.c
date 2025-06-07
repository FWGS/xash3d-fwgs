/*
host_sdl2.c - SDL event system handlers
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
#include "platform_sdl2.h"
#include "sound.h"
#include "vid_common.h"

/*
=============
SDLash_KeyEvent

=============
*/
static void SDLash_KeyEvent( SDL_KeyboardEvent key )
{
	int down = key.state != SDL_RELEASED;
	int keynum = key.keysym.scancode;

#if XASH_ANDROID
	if( keynum == SDL_SCANCODE_VOLUMEUP || keynum == SDL_SCANCODE_VOLUMEDOWN )
	{
		host.force_draw_version_time = host.realtime + FORCE_DRAW_VERSION_TIME;
	}
#endif

	if( SDL_IsTextInputActive( ) && down )
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

		// ignore printable keys, they are coming through SDL_TEXTINPUT
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
		case SDL_SCANCODE_PAUSE: keynum = K_PAUSE; break;
		case SDL_SCANCODE_SCROLLLOCK: keynum = K_SCROLLLOCK; break;
		case SDL_SCANCODE_APPLICATION: keynum = K_WIN; break; // (compose key) ???
		case SDL_SCANCODE_VOLUMEUP: keynum = K_AUX32; break;
		case SDL_SCANCODE_VOLUMEDOWN: keynum = K_AUX31; break;
		// don't console spam on known functional buttons, not used in engine
		case SDL_SCANCODE_MUTE:
		case SDL_SCANCODE_BRIGHTNESSDOWN:
		case SDL_SCANCODE_BRIGHTNESSUP:
		case SDL_SCANCODE_SELECT:
			return;
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

	if( button.which == SDL_TOUCH_MOUSEID )
		return;

	if( button.state == SDL_RELEASED )
		down = 0;
	else if( button.clicks >= 2 )
		down = 2; // special state for double-click in UI
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
	default:
		Con_Printf( "Unknown mouse button ID: %d\n", button.button );
	}
}

/*
=============
SDLash_InputEvent

=============
*/
static void SDLash_InputEvent( SDL_TextInputEvent input )
{
	const char *text;

	VGui_ReportTextInput( input.text );

	for( text = input.text; *text; text++ )
	{
		int ch = (byte)*text;

		// do not pass UTF-8 sequence into the engine, convert it here
		if( !cls.accept_utf8 )
			ch = Con_UtfProcessCharForce( ch );

		if( !ch )
			continue;

		CL_CharEvent( ch );
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
#if TARGET_OS_IPHONE
		{
			// Keep running if ftp server enabled
			void IOS_StartBackgroundTask( void );
			IOS_StartBackgroundTask();
		}
#endif
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
	case SDL_MOUSEWHEEL:
		IN_MWheelEvent( event->wheel.y );
		break;

	/* Touch events */
	case SDL_FINGERDOWN:
	case SDL_FINGERUP:
	case SDL_FINGERMOTION:
	{
		static int scale = 0;
		touchEventType type;
		float x, y, dx, dy;

		if( event->type == SDL_FINGERDOWN )
			type = event_down;
		else if( event->type == SDL_FINGERUP )
			type = event_up ;
		else if( event->type == SDL_FINGERMOTION )
			type = event_motion;
		else break;

		/*
		SDL sends coordinates in [0..width],[0..height] values
		on some devices
		*/
		if( !scale )
		{
			if( ( event->tfinger.x > 0 ) && ( event->tfinger.y > 0 ) )
			{
				if( ( event->tfinger.x > 2 ) && ( event->tfinger.y > 2 ) )
				{
					scale = 2;
					Con_Reportf( "SDL reports screen coordinates, workaround enabled!\n");
				}
				else
				{
					scale = 1;
				}
			}
		}

		x = event->tfinger.x;
		y = event->tfinger.y;
		dx = event->tfinger.dx;
		dy = event->tfinger.dy;

		if( scale == 2 )
		{
			x /= (float)refState.width;
			y /= (float)refState.height;
			dx /= (float)refState.width;
			dy /= (float)refState.height;
		}

		IN_TouchEvent( type, event->tfinger.fingerId, x, y, dx, dy );
		break;
	}

	/* IME */
	case SDL_TEXTINPUT:
		SDLash_InputEvent( event->text );
		break;

	/* GameController API */
	case SDL_CONTROLLERAXISMOTION:
	case SDL_CONTROLLERBUTTONDOWN:
	case SDL_CONTROLLERBUTTONUP:
	case SDL_CONTROLLERDEVICEADDED:
	case SDL_CONTROLLERDEVICEREMOVED:
#if SDL_VERSION_ATLEAST( 2, 0, 14 )
	case SDL_CONTROLLERTOUCHPADDOWN:
	case SDL_CONTROLLERTOUCHPADMOTION:
	case SDL_CONTROLLERTOUCHPADUP:
	case SDL_CONTROLLERSENSORUPDATE:
#endif
		SDLash_HandleGameControllerEvent( event );
		break;

	case SDL_WINDOWEVENT:
		if( event->window.windowID != SDL_GetWindowID( host.hWnd ) )
			return;

		if( host.status == HOST_SHUTDOWN || Host_IsDedicated() )
			break; // no need to activate

		switch( event->window.event )
		{
		case SDL_WINDOWEVENT_MOVED:
		{
			char val[32];

			Q_snprintf( val, sizeof( val ), "%d", event->window.data1 );
			Cvar_DirectSet( &window_xpos, val );

			Q_snprintf( val, sizeof( val ), "%d", event->window.data2 );
			Cvar_DirectSet( &window_ypos, val );

			if ( vid_fullscreen.value == WINDOW_MODE_WINDOWED )
				Cvar_DirectSet( &vid_maximized, "0" );
			break;
		}
		case SDL_WINDOWEVENT_MINIMIZED:
			host.status = HOST_SLEEP;
			Cvar_DirectSet( &vid_maximized, "0" );
			VID_RestoreScreenResolution( );
			break;
		case SDL_WINDOWEVENT_RESTORED:
			host.status = HOST_FRAME;
			host.force_draw_version_time = host.realtime + FORCE_DRAW_VERSION_TIME;
			Cvar_DirectSet( &vid_maximized, "0" );
			if( vid_fullscreen.value == WINDOW_MODE_FULLSCREEN )
				VID_SetMode();
			break;
		case SDL_WINDOWEVENT_FOCUS_GAINED:
			SDLash_ActiveEvent( true );
			break;
		case SDL_WINDOWEVENT_FOCUS_LOST:
			SDLash_ActiveEvent( false );
			break;
		case SDL_WINDOWEVENT_RESIZED:
#if !XASH_MOBILE_PLATFORM
			if( vid_fullscreen.value == WINDOW_MODE_WINDOWED )
#endif
			{
				SDL_Window *wnd = SDL_GetWindowFromID( event->window.windowID );
				VID_SaveWindowSize( event->window.data1, event->window.data2,
					FBitSet( SDL_GetWindowFlags( wnd ), SDL_WINDOW_MAXIMIZED ) != 0 );
			}
			break;
		case SDL_WINDOWEVENT_MAXIMIZED:
			Cvar_DirectSet( &vid_maximized, "1" );
			break;
		default:
			break;
		}
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

#if XASH_PSVITA
	PSVita_InputUpdate();
#endif
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
