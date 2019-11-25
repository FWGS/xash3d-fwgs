/*
events.c - SDL event system handlers
Copyright (C) 2015-2017 a1batross

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#if defined( XASH_SDL ) && !XASH_DEDICATED
#include <SDL.h>
#include <ctype.h>

#include "common.h"
#include "keydefs.h"
#include "input.h"
#include "client.h"
#include "vgui_draw.h"
#include "events.h"
#include "sound.h"
#include "vid_common.h"

static int wheelbutton;

#if ! SDL_VERSION_ATLEAST( 2, 0, 0 )
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
#endif

/*
=============
SDLash_KeyEvent

=============
*/
static void SDLash_KeyEvent( SDL_KeyboardEvent key )
{
	int down = key.state != SDL_RELEASED;
#if SDL_VERSION_ATLEAST( 2, 0, 0 )
	int keynum = key.keysym.scancode;
#else
	int keynum = key.keysym.sym;
#endif
	qboolean numLock = SDL_GetModState() & KMOD_NUM;

	if( SDL_IsTextInputActive() && down )
	{
		if( SDL_GetModState() & KMOD_CTRL )
		{
			if( keynum >= SDL_SCANCODE_A && keynum <= SDL_SCANCODE_Z )
			{
				keynum = keynum - SDL_SCANCODE_A + 1;
				CL_CharEvent( keynum );
			}

			return;
		}

#if !SDL_VERSION_ATLEAST( 2, 0, 0 )
		if( keynum >= SDLK_KP0 && keynum <= SDLK_KP9 )
			keynum -= SDLK_KP0 + '0';

		if( isprint( keynum ) )
		{
			if( SDL_GetModState() & KMOD_SHIFT )
			{
				keynum = Key_ToUpper( keynum );
			}

			CL_CharEvent( keynum );
			return;
		}
#endif
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
			host.force_draw_version = true;
			host.force_draw_version_time = host.realtime + FORCE_DRAW_VERSION_TIME;
			break;
		}
#if SDL_VERSION_ATLEAST( 2, 0, 0 )
		case SDL_SCANCODE_APPLICATION: keynum = K_WIN; break; // (compose key) ???
		// don't console spam on known functional buttons, but not used in engine
		case SDL_SCANCODE_MUTE:
		case SDL_SCANCODE_VOLUMEUP:
		case SDL_SCANCODE_VOLUMEDOWN:
		case SDL_SCANCODE_BRIGHTNESSDOWN:
		case SDL_SCANCODE_BRIGHTNESSUP:
			return;
#endif // SDL_VERSION_ATLEAST( 2, 0, 0 )
		case SDL_SCANCODE_UNKNOWN:
		{
			if( down ) Con_Reportf( "SDLash_KeyEvent: Unknown scancode\n" );
			return;
		}
		default:
			if( down ) Con_Reportf( "SDLash_KeyEvent: Unknown key: %s = %i\n", SDL_GetScancodeName( keynum ), keynum );
			return;
		}
	}

#undef DECLARE_KEY_RANGE

	Key_Event( keynum, down );
}

static void SDLash_MouseKey( int key, int down, int istouch )
{
	if( CVAR_TO_BOOL( touch_emulate ) )
	{
		Touch_KeyEvent( key, down );
	}
	else if( in_mouseinitialized && !m_ignore->value && !istouch )
	{
		Key_Event( key, down );
	}
}

/*
=============
SDLash_MouseEvent

=============
*/
static void SDLash_MouseEvent( SDL_MouseButtonEvent button )
{
	int down = button.state != SDL_RELEASED;
	qboolean istouch;

#if SDL_VERSION_ATLEAST( 2, 0, 0 )
	istouch = button.which == SDL_TOUCH_MOUSEID;
#else // SDL_VERSION_ATLEAST( 2, 0, 0 )
	istouch = false;
#endif // SDL_VERSION_ATLEAST( 2, 0, 0 )

	switch( button.button )
	{
	case SDL_BUTTON_LEFT:
		SDLash_MouseKey( K_MOUSE1, down, istouch );
		break;
	case SDL_BUTTON_RIGHT:
		SDLash_MouseKey( K_MOUSE2, down, istouch );
		break;
	case SDL_BUTTON_MIDDLE:
		SDLash_MouseKey( K_MOUSE3, down, istouch );
		break;
	case SDL_BUTTON_X1:
		SDLash_MouseKey( K_MOUSE4, down, istouch );
		break;
	case SDL_BUTTON_X2:
		SDLash_MouseKey( K_MOUSE5, down, istouch );
		break;
#if ! SDL_VERSION_ATLEAST( 2, 0, 0 )
	case SDL_BUTTON_WHEELUP:
		Key_Event( K_MWHEELUP, down );
		break;
	case SDL_BUTTON_WHEELDOWN:
		Key_Event( K_MWHEELDOWN, down );
		break;
#endif // ! SDL_VERSION_ATLEAST( 2, 0, 0 )
	default:
		Con_Printf( "Unknown mouse button ID: %d\n", button.button );
	}
}

/*
=============
SDLash_InputEvent

=============
*/
#if SDL_VERSION_ATLEAST( 2, 0, 0 )
static void SDLash_InputEvent( SDL_TextInputEvent input )
{
	char *text;
	for( text = input.text; *text; text++ )
	{
		int ch;

		if( !Q_stricmp( cl_charset->string, "utf-8" ) )
			ch = (unsigned char)*text;
		else
			ch = Con_UtfProcessCharForce( (unsigned char)*text );

		if( !ch )
			continue;

		CL_CharEvent( ch );
	}
}
#endif // SDL_VERSION_AT_LEAST( 2, 0, 0 )

static void SDLash_ActiveEvent( int gain )
{
	if( gain )
	{
		host.status = HOST_FRAME;
		IN_ActivateMouse(true);
		if( snd_mute_losefocus->value )
		{
			SNDDMA_Activate( true );
		}
		host.force_draw_version = true;
		host.force_draw_version_time = host.realtime + FORCE_DRAW_VERSION_TIME;
		if( vid_fullscreen->value )
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
		IN_DeactivateMouse();
		if( snd_mute_losefocus->value )
		{
			SNDDMA_Activate( false );
		}
		host.force_draw_version = true;
		host.force_draw_version_time = host.realtime + 2;
		VID_RestoreScreenResolution();
	}
}

/*
=============
SDLash_EventFilter

=============
*/
static void SDLash_EventFilter( SDL_Event *event )
{
	static int mdown;

	if( wheelbutton )
	{
		Key_Event( wheelbutton, false );
		wheelbutton = 0;
	}

	switch ( event->type )
	{
	/* Mouse events */
	case SDL_MOUSEMOTION:
		if( !host.mouse_visible
#if SDL_VERSION_ATLEAST( 2, 0, 0 )
		    && event->motion.which != SDL_TOUCH_MOUSEID )
#else
		    )
#endif
			IN_MouseEvent();
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

	/* Joystick events */
	case SDL_JOYAXISMOTION:
		Joy_AxisMotionEvent( event->jaxis.axis, event->jaxis.value );
		break;

	case SDL_JOYBALLMOTION:
		Joy_BallMotionEvent( event->jball.ball, event->jball.xrel, event->jball.yrel );
		break;

	case SDL_JOYHATMOTION:
		Joy_HatMotionEvent( event->jhat.hat, event->jhat.value );
		break;

	case SDL_JOYBUTTONDOWN:
	case SDL_JOYBUTTONUP:
		Joy_ButtonEvent( event->jbutton.button, event->jbutton.state );
		break;

	case SDL_QUIT:
		Sys_Quit();
		break;
#if SDL_VERSION_ATLEAST( 2, 0, 0 )
	case SDL_MOUSEWHEEL:
		wheelbutton = event->wheel.y < 0 ? K_MWHEELDOWN : K_MWHEELUP;
		Key_Event( wheelbutton, true );
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

	case SDL_JOYDEVICEADDED:
		Joy_AddEvent();
		break;
	case SDL_JOYDEVICEREMOVED:
		Joy_RemoveEvent();
		break;

	/* GameController API */
	case SDL_CONTROLLERAXISMOTION:
		if( event->caxis.axis == (Uint8)SDL_CONTROLLER_AXIS_INVALID )
			break;

		// Swap axis to follow default axis binding:
		// LeftX, LeftY, RightX, RightY, TriggerRight, TriggerLeft
		if( event->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT )
			event->caxis.axis = SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
		else if( event->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT )
			event->caxis.axis = SDL_CONTROLLER_AXIS_TRIGGERLEFT;

		Joy_AxisMotionEvent( event->caxis.axis, event->caxis.value );
		break;

	case SDL_CONTROLLERBUTTONDOWN:
	case SDL_CONTROLLERBUTTONUP:
	{
		static int sdlControllerButtonToEngine[] =
		{
			K_AUX16, // invalid
			K_A_BUTTON, K_B_BUTTON, K_X_BUTTON,	K_Y_BUTTON,
			K_BACK_BUTTON, K_MODE_BUTTON, K_START_BUTTON,
			K_LSTICK, K_RSTICK,
			K_L1_BUTTON, K_R1_BUTTON,
			K_UPARROW, K_DOWNARROW, K_LEFTARROW, K_RIGHTARROW
		};

		// TODO: Use joyinput funcs, for future multiple gamepads support
		if( Joy_IsActive() )
			Key_Event( sdlControllerButtonToEngine[event->cbutton.button], event->cbutton.state );
		break;
	}

	case SDL_CONTROLLERDEVICEADDED:
		Joy_AddEvent( );
		break;

	case SDL_CONTROLLERDEVICEREMOVED:
		Joy_RemoveEvent( );
		break;

	case SDL_WINDOWEVENT:
		if( event->window.windowID != SDL_GetWindowID( host.hWnd ) )
			return;

		if( host.status == HOST_SHUTDOWN || Host_IsDedicated() )
			break; // no need to activate

		switch( event->window.event )
		{
		case SDL_WINDOWEVENT_MOVED:
			if( !vid_fullscreen->value )
			{
				Cvar_SetValue( "_window_xpos", (float)event->window.data1 );
				Cvar_SetValue( "_window_ypos", (float)event->window.data2 );
			}
			break;
		case SDL_WINDOWEVENT_MINIMIZED:
			host.status = HOST_SLEEP;
			VID_RestoreScreenResolution( );
			break;
		case SDL_WINDOWEVENT_RESTORED:
			host.status = HOST_FRAME;
			host.force_draw_version = true;
			host.force_draw_version_time = host.realtime + FORCE_DRAW_VERSION_TIME;
			if( vid_fullscreen->value )
				VID_SetMode();
			break;
		case SDL_WINDOWEVENT_FOCUS_GAINED:
			SDLash_ActiveEvent( true );
			break;
		case SDL_WINDOWEVENT_FOCUS_LOST:
			SDLash_ActiveEvent( false );
			break;
		case SDL_WINDOWEVENT_RESIZED:
		{
			if( vid_fullscreen->value )
				break;

			VID_SaveWindowSize( event->window.data1, event->window.data2 );
			break;
		}
		default:
			break;
		}
#else
	case SDL_VIDEORESIZE:
		VID_SaveWindowSize( event->resize.w, event->resize.h );
		break;
	case SDL_ACTIVEEVENT:
		SDLash_ActiveEvent( event->active.gain );
		break;
#endif
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

	while( !host.crashed && !host.shutdown_issued && SDL_PollEvent( &event ) )
		SDLash_EventFilter( &event );
}

void* Platform_GetNativeObject( const char *name )
{
	return NULL; // SDL don't have it
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
	if( CVAR_TO_BOOL( m_ignore ) )
	{
		SDL_GetRelativeMouseState( NULL, NULL );
		SDL_ShowCursor( SDL_TRUE );
	}
}

#endif //  defined( XASH_SDL ) && !XASH_DEDICATED
