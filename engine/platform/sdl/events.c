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

#ifdef XASH_SDL
#include <SDL.h>

#include "common.h"
#include "keydefs.h"
#include "input.h"
#include "client.h"
#include "vgui_draw.h"
#include "events.h"
#include "sound.h"
#include "vid_common.h"
#include "gl_local.h"

extern convar_t *vid_fullscreen;
extern convar_t *snd_mute_losefocus;
static int wheelbutton;
static SDL_Joystick *joy;
static SDL_GameController *gamecontroller;

void R_ChangeDisplaySettingsFast( int w, int h );

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
			if( down ) MsgDev( D_INFO, "SDLash_KeyEvent: Unknown scancode\n" );
			return;
		}
		default:
			if( down ) MsgDev( D_INFO, "SDLash_KeyEvent: Unknown key: %s = %i\n", SDL_GetScancodeName( keynum ), keynum );
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
	if( in_mouseinitialized && !m_ignore->value && button.which != SDL_TOUCH_MOUSEID )
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
	for( char *text = input.text; *text; text++ )
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
SDLash_EnableTextInput

=============
*/
void SDLash_EnableTextInput( qboolean enable )
{
	enable ? SDL_StartTextInput() : SDL_StopTextInput();
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
#ifdef TOUCHEMU
		if( mdown )
			IN_TouchEvent( event_motion, 0,
						   event->motion.x/scr_width->value,
						   event->motion.y/scr_height->value,
						   event->motion.xrel/scr_width->value,
						   event->motion.yrel/scr_height->value );
		SDL_ShowCursor( true );
#endif
		break;

	case SDL_MOUSEBUTTONUP:
	case SDL_MOUSEBUTTONDOWN:
#ifdef TOUCHEMU
		mdown = event->button.state != SDL_RELEASED;
		IN_TouchEvent( event_down, 0,
					   event->button.x/scr_width->value,
					   event->button.y/scr_height->value, 0, 0);
#else
		SDLash_MouseEvent( event->button );
#endif
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
		touchEventType type;
		static int scale = 0;
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
					MsgDev( D_INFO, "SDL reports screen coordinates, workaround enabled!\n");
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
			x /= (float)glState.width;
			y /= (float)glState.height;
			dx /= (float)glState.width;
			dy /= (float)glState.height;
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
		Joy_AxisMotionEvent( event->jaxis.which, event->jaxis.axis, event->jaxis.value );
		break;

	case SDL_JOYBALLMOTION:
		Joy_BallMotionEvent( event->jball.which, event->jball.ball, event->jball.xrel, event->jball.yrel );
		break;

	case SDL_JOYHATMOTION:
		Joy_HatMotionEvent( event->jhat.which, event->jhat.hat, event->jhat.value );
		break;

	case SDL_JOYBUTTONDOWN:
	case SDL_JOYBUTTONUP:
		Joy_ButtonEvent( event->jbutton.which, event->jbutton.button, event->jbutton.state );
		break;

	case SDL_JOYDEVICEADDED:
		Joy_AddEvent( event->jdevice.which );
		break;
	case SDL_JOYDEVICEREMOVED:
		Joy_RemoveEvent( event->jdevice.which );
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

		Joy_AxisMotionEvent( event->caxis.which, event->caxis.axis, event->caxis.value );
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
		Joy_AddEvent( event->cdevice.which );
		break;

	case SDL_CONTROLLERDEVICEREMOVED:
		Joy_RemoveEvent( event->cdevice.which );
		break;

	case SDL_QUIT:
		Sys_Quit();
		break;

	case SDL_WINDOWEVENT:
		if( event->window.windowID != SDL_GetWindowID( host.hWnd ) )
			return;

		if( ( host.status == HOST_SHUTDOWN ) ||
			( host.type  == HOST_DEDICATED ) )
			break; // no need to activate
		switch( event->window.event )
		{
		case SDL_WINDOWEVENT_MOVED:
			if( !vid_fullscreen->value )
			{
				Cvar_SetValue( "_window_xpos", (float)event->window.data1 );
				Cvar_SetValue( "_window_ypos", (float)event->window.data1 );
			}
			break;
		case SDL_WINDOWEVENT_RESTORED:
			host.status = HOST_FRAME;
			host.force_draw_version = true;
			host.force_draw_version_time = host.realtime + 2;
			if( vid_fullscreen->value )
				VID_SetMode();
			break;
		case SDL_WINDOWEVENT_FOCUS_GAINED:
			host.status = HOST_FRAME;
			IN_ActivateMouse(true);
			if( snd_mute_losefocus->value )
			{
				S_Activate( true );
			}
			host.force_draw_version = true;
			host.force_draw_version_time = host.realtime + 2;
			if( vid_fullscreen->value )
				VID_SetMode();
			break;
		case SDL_WINDOWEVENT_MINIMIZED:
			host.status = HOST_SLEEP;
			VID_RestoreScreenResolution();
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
				S_Activate( false );
			}
			host.force_draw_version = true;
			host.force_draw_version_time = host.realtime + 1;
			VID_RestoreScreenResolution();
			break;
		case SDL_WINDOWEVENT_CLOSE:
			Sys_Quit();
			break;
		case SDL_WINDOWEVENT_RESIZED:
			if( vid_fullscreen->value ) break;
			Cvar_SetValue( "vid_mode",  VID_NOMODE ); // no mode
			R_ChangeDisplaySettingsFast( event->window.data1,
										 event->window.data2 );
			break;
		case SDL_WINDOWEVENT_MAXIMIZED:
		{
			int w, h;
			if( vid_fullscreen->value ) break;
			Cvar_SetValue( "vid_mode", VID_NOMODE ); // no mode

			SDL_GL_GetDrawableSize( host.hWnd, &w, &h );
			R_ChangeDisplaySettingsFast( w, h );
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
void SDLash_RunEvents( void )
{
	SDL_Event event;

	while( !host.crashed && !host.shutdown_issued && SDL_PollEvent( &event ) )
		SDLash_EventFilter( &event );
}

/*
=============
SDLash_JoyInit_Old

=============
*/
static int SDLash_JoyInit_Old( int numjoy )
{
	int num;
	int i;

	MsgDev( D_INFO, "Joystick: SDL\n" );

	if( SDL_WasInit( SDL_INIT_JOYSTICK ) != SDL_INIT_JOYSTICK &&
		SDL_InitSubSystem( SDL_INIT_JOYSTICK ) )
	{
		MsgDev( D_INFO, "Failed to initialize SDL Joysitck: %s\n", SDL_GetError() );
		return 0;
	}

	if( joy )
	{
		SDL_JoystickClose( joy );
	}

	num = SDL_NumJoysticks();

	if( num > 0 )
		MsgDev( D_INFO, "%i joysticks found:\n", num );
	else
	{
		MsgDev( D_INFO, "No joystick found.\n" );
		return 0;
	}

	for( i = 0; i < num; i++ )
		MsgDev( D_INFO, "%i\t: %s\n", i, SDL_JoystickNameForIndex( i ) );

	MsgDev( D_INFO, "Pass +set joy_index N to command line, where N is number, to select active joystick\n" );

	joy = SDL_JoystickOpen( numjoy );

	if( !joy )
	{
		MsgDev( D_INFO, "Failed to select joystick: %s\n", SDL_GetError( ) );
		return 0;
	}

	MsgDev( D_INFO, "Selected joystick: %s\n"
		"\tAxes: %i\n"
		"\tHats: %i\n"
		"\tButtons: %i\n"
		"\tBalls: %i\n",
		SDL_JoystickName( joy ), SDL_JoystickNumAxes( joy ), SDL_JoystickNumHats( joy ),
		SDL_JoystickNumButtons( joy ), SDL_JoystickNumBalls( joy ) );

	SDL_GameControllerEventState( SDL_DISABLE );
	SDL_JoystickEventState( SDL_ENABLE );

	return num;
}

/*
=============
SDLash_JoyInit_New

=============
*/
static int SDLash_JoyInit_New( int numjoy )
{
	int temp, num;
	int i;

	MsgDev( D_INFO, "Joystick: SDL GameController API\n" );

	if( SDL_WasInit( SDL_INIT_GAMECONTROLLER ) != SDL_INIT_GAMECONTROLLER &&
		SDL_InitSubSystem( SDL_INIT_GAMECONTROLLER ) )
	{
		MsgDev( D_INFO, "Failed to initialize SDL GameController API: %s\n", SDL_GetError() );
		return 0;
	}

	// chance to add mappings from file
	SDL_GameControllerAddMappingsFromFile( "controllermappings.txt" );

	if( gamecontroller )
	{
		SDL_GameControllerClose( gamecontroller );
	}

	temp = SDL_NumJoysticks();
	num = 0;

	for( i = 0; i < temp; i++ )
	{
		if( SDL_IsGameController( i ))
			num++;
	}

	if( num > 0 )
		MsgDev( D_INFO, "%i joysticks found:\n", num );
	else
	{
		MsgDev( D_INFO, "No joystick found.\n" );
		return 0;
	}

	for( i = 0; i < num; i++ )
		MsgDev( D_INFO, "%i\t: %s\n", i, SDL_GameControllerNameForIndex( i ) );

	MsgDev( D_INFO, "Pass +set joy_index N to command line, where N is number, to select active joystick\n" );

	gamecontroller = SDL_GameControllerOpen( numjoy );

	if( !gamecontroller )
	{
		MsgDev( D_INFO, "Failed to select joystick: %s\n", SDL_GetError( ) );
		return 0;
	}
// was added in SDL2-2.0.6, allow build with earlier versions just in case
#if SDL_MAJOR_VERSION > 2 || SDL_MINOR_VERSION > 0 || SDL_PATCHLEVEL >= 6
	MsgDev( D_INFO, "Selected joystick: %s (%i:%i:%i)\n",
		SDL_GameControllerName( gamecontroller ),
		SDL_GameControllerGetVendor( gamecontroller ),
		SDL_GameControllerGetProduct( gamecontroller ),
		SDL_GameControllerGetProductVersion( gamecontroller ));
#endif
	SDL_GameControllerEventState( SDL_ENABLE );
	SDL_JoystickEventState( SDL_DISABLE );

	return num;
}

/*
=============
SDLash_JoyInit

=============
*/
int SDLash_JoyInit( int numjoy )
{
	// SDL_Joystick is now an old API
	// SDL_GameController is preferred
	if( Sys_CheckParm( "-sdl_joy_old_api" ) )
		return SDLash_JoyInit_Old(numjoy);

	return SDLash_JoyInit_New(numjoy);
}


#endif // XASH_SDL
