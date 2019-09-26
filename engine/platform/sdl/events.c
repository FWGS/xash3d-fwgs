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

#if defined( XASH_SDL ) && !defined( XASH_DEDICATED )
#include <SDL.h>

#include "common.h"
#include "keydefs.h"
#include "input.h"
#include "client.h"
#include "vgui_draw.h"
#include "events.h"
#include "sound.h"
#include "vid_common.h"

static int wheelbutton;

/*
=============
SDLash_KeyEvent

=============
*/
static void SDLash_KeyEvent( SDL_KeyboardEvent key )
{
	int down = key.state != SDL_RELEASED;
	int keynum = key.keysym.scancode;
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
	}

#define DECLARE_KEY_RANGE( min, max, repl ) \
	if( keynum >= (min) && keynum <= (max) ) \
	{ \
		keynum = keynum - (min) + (repl); \
	}

	DECLARE_KEY_RANGE( SDL_SCANCODE_A, SDL_SCANCODE_Z, 'a' )
	else DECLARE_KEY_RANGE( SDL_SCANCODE_1, SDL_SCANCODE_9, '1' )
	else DECLARE_KEY_RANGE( SDL_SCANCODE_F1, SDL_SCANCODE_F12, K_F1 )
#undef DECLARE_KEY_RANGE
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
		case SDL_SCANCODE_APPLICATION: keynum = K_WIN; break; // (compose key) ???
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
		// don't console spam on known functional buttons, but not used in engine
		case SDL_SCANCODE_MUTE:
		case SDL_SCANCODE_VOLUMEUP:
		case SDL_SCANCODE_VOLUMEDOWN:
		case SDL_SCANCODE_BRIGHTNESSDOWN:
		case SDL_SCANCODE_BRIGHTNESSUP:
			return;
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

	Key_Event( keynum, down );
}

/*
=============
SDLash_MouseEvent

=============
*/
static void SDLash_MouseEvent( SDL_MouseButtonEvent button )
{
	int down = button.state != SDL_RELEASED;

	if( CVAR_TO_BOOL( touch_emulate ) )
	{
		Touch_KeyEvent( K_MOUSE1 - 1 + button.button, down );
	}
	else if( in_mouseinitialized && !m_ignore->value && button.which != SDL_TOUCH_MOUSEID )
	{
		Key_Event( K_MOUSE1 - 1 + button.button, down );
	}
}

/*
=============
SDLash_InputEvent

=============
*/
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
		if( !host.mouse_visible && event->motion.which != SDL_TOUCH_MOUSEID )
			IN_MouseEvent();
		break;

	case SDL_MOUSEBUTTONUP:
	case SDL_MOUSEBUTTONDOWN:

		SDLash_MouseEvent( event->button );
		break;

	case SDL_MOUSEWHEEL:
		wheelbutton = event->wheel.y < 0 ? K_MWHEELDOWN : K_MWHEELUP;
		Key_Event( wheelbutton, true );
		break;

	/* Keyboard events */
	case SDL_KEYDOWN:
	case SDL_KEYUP:
		SDLash_KeyEvent( event->key );
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
		else if(event->type == SDL_FINGERMOTION )
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

	case SDL_QUIT:
		Sys_Quit();
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
			break;
		case SDL_WINDOWEVENT_FOCUS_LOST:
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
			break;
		case SDL_WINDOWEVENT_RESIZED:
		case SDL_WINDOWEVENT_MAXIMIZED:
		{
			int w = VID_MIN_WIDTH, h = VID_MIN_HEIGHT;
			if( vid_fullscreen->value )
				break;

			SDL_GL_GetDrawableSize( host.hWnd, &w, &h );
			R_SaveVideoMode( w, h );
			SCR_VidInit(); // tell the client.dll what vid_mode has changed
			break;
		}
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

#endif //  defined( XASH_SDL ) && !defined( XASH_DEDICATED )
