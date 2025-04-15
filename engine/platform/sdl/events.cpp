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
#include <sky/sky.h>

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
#define SDL_JoystickID Uint8
#endif

static int SDLash_GameControllerButtonMapping[] =
{
#if XASH_NSWITCH // devkitPro/SDL has inverted Nintendo layout for SDL_GameController
	K_B_BUTTON, K_A_BUTTON, K_Y_BUTTON, K_X_BUTTON,
#else
	K_A_BUTTON, K_B_BUTTON, K_X_BUTTON, K_Y_BUTTON,
#endif
	K_BACK_BUTTON, K_MODE_BUTTON, K_START_BUTTON,
	K_LSTICK, K_RSTICK,
	K_L1_BUTTON, K_R1_BUTTON,
	K_DPAD_UP, K_DPAD_DOWN, K_DPAD_LEFT, K_DPAD_RIGHT,
	K_MISC_BUTTON,
	K_PADDLE1_BUTTON, K_PADDLE2_BUTTON, K_PADDLE3_BUTTON, K_PADDLE4_BUTTON,
	K_TOUCHPAD,
};

// Swap axis to follow default axis binding:
// LeftX, LeftY, RightX, RightY, TriggerRight, TriggerLeft
static int SDLash_GameControllerAxisMapping[] =
{
	JOY_AXIS_SIDE, // SDL_CONTROLLER_AXIS_LEFTX,
	JOY_AXIS_FWD, // SDL_CONTROLLER_AXIS_LEFTY,
	JOY_AXIS_PITCH, // SDL_CONTROLLER_AXIS_RIGHTX,
	JOY_AXIS_YAW, // SDL_CONTROLLER_AXIS_RIGHTY,
	JOY_AXIS_LT, // SDL_CONTROLLER_AXIS_TRIGGERLEFT,
	JOY_AXIS_RT, // SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
};

static qboolean SDLash_IsInstanceIDAGameController( SDL_JoystickID joyId )
{
#if !SDL_VERSION_ATLEAST( 2, 0, 4 )
	// HACKHACK: if we're not initialized g_joy, then we're probably using gamecontroller api
	// so return true
	if( !g_joy )
		return true;
	return false;
#else
	if( SDL_GameControllerFromInstanceID( joyId ) )
		return true;
	return false;
#endif
}

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

	if( SDL_IsTextInputActive() && down && cls.key_dest != key_game )
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
			host.force_draw_version_time = host.realtime + FORCE_DRAW_VERSION_TIME;
			break;
		}
		case SDL_SCANCODE_PAUSE: keynum = K_PAUSE; break;
		case SDL_SCANCODE_SCROLLLOCK: keynum = K_SCROLLOCK; break;
#if SDL_VERSION_ATLEAST( 2, 0, 0 )
		case SDL_SCANCODE_APPLICATION: keynum = K_WIN; break; // (compose key) ???
		// don't console spam on known functional buttons, not used in engine
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
			if( down ) Con_Reportf( "SDLash_KeyEvent: Unknown key: %s = %i\n", SDL_GetScancodeName( (SDL_Scancode)keynum ), keynum );
			return;
		}
	}

#undef DECLARE_KEY_RANGE

	Key_Event( keynum, down );
}

static void Sky_ButtonEvent(const SDL_Event& event)
{
	static const std::unordered_map<uint8_t, Platform::Input::Mouse::Button> ButtonMap = {
		{ SDL_BUTTON_LEFT, Platform::Input::Mouse::Button::Left },
		{ SDL_BUTTON_MIDDLE, Platform::Input::Mouse::Button::Middle },
		{ SDL_BUTTON_RIGHT, Platform::Input::Mouse::Button::Right },
	};

	static const std::unordered_map<uint32_t, Platform::Input::Mouse::ButtonEvent::Type> TypeMap = {
		{ SDL_MOUSEBUTTONDOWN, Platform::Input::Mouse::ButtonEvent::Type::Pressed },
		{ SDL_MOUSEBUTTONUP, Platform::Input::Mouse::ButtonEvent::Type::Released }
	};

	if (!ButtonMap.contains(event.button.button))
		return;

	sky::Emit(Platform::Input::Mouse::ButtonEvent{
		.type = TypeMap.at(event.type),
		.button = ButtonMap.at(event.button.button),
		.pos = {
			(int)((float)event.button.x * gScale),//PLATFORM->getScale()),
			(int)((float)event.button.y * gScale)//PLATFORM->getScale())
		}
	});
}
/*
=============
SDLash_MouseEvent

=============
*/
static void SDLash_MouseEvent( SDL_MouseButtonEvent button )
{
	int down;

#if SDL_VERSION_ATLEAST( 2, 0, 0 )
	if( button.which == SDL_TOUCH_MOUSEID )
		return;
#endif

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
#if ! SDL_VERSION_ATLEAST( 2, 0, 0 )
	case SDL_BUTTON_WHEELUP:
		IN_MWheelEvent( -1 );
		break;
	case SDL_BUTTON_WHEELDOWN:
		IN_MWheelEvent( 1 );
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
	VGui_ReportTextInput( input.text );
	for( text = input.text; *text; text++ )
	{
		int ch;

		if( !Q_stricmp( cl_charset.string, "utf-8" ) )
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
		if( cls.key_dest == key_game )
		{
			IN_ActivateMouse( );
		}

		if( dma.initialized && snd_mute_losefocus.value )
		{
			SNDDMA_Activate( true );
		}
		host.force_draw_version_time = host.realtime + FORCE_DRAW_VERSION_TIME;
		if( vid_fullscreen.value )
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
			IN_DeactivateMouse();
		}

		if( dma.initialized && snd_mute_losefocus.value )
		{
			SNDDMA_Activate( false );
		}
		host.force_draw_version_time = host.realtime + 2.0;
		VID_RestoreScreenResolution();
	}
}

#if SDL_VERSION_ATLEAST( 2, 0, 0 )
static size_t num_open_game_controllers = 0;

static void SDLash_GameController_Add( int index )
{
	extern convar_t joy_enable; // private to input system
	SDL_GameController *controller;

	if( !joy_enable.value )
		return;

	controller = SDL_GameControllerOpen( index );
	if( !controller )
	{
		Con_Reportf( "Failed to open SDL GameController %d: %s\n", index, SDL_GetError( ) );
		SDL_ClearError( );
		return;
	}
#if SDL_VERSION_ATLEAST( 2, 0, 6 )
	Con_Reportf( "Added controller: %s (%i:%i:%i)\n",
		SDL_GameControllerName( controller ),
		SDL_GameControllerGetVendor( controller ),
		SDL_GameControllerGetProduct( controller ),
		SDL_GameControllerGetProductVersion( controller ));
#endif // SDL_VERSION_ATLEAST( 2, 0, 6 )

	++num_open_game_controllers;
	if( num_open_game_controllers == 1 )
		Joy_AddEvent( );
}


static void SDLash_GameController_Remove( SDL_JoystickID joystick_id )
{
	Con_Reportf( "Removed controller %i\n", joystick_id );

	// `Joy_RemoveEvent` sets `joy_found` to `0`.
	// We only want to do this when all the game controllers have been removed.
	--num_open_game_controllers;
	if( num_open_game_controllers == 0 )
		Joy_RemoveEvent( );
}
#endif

/*
=============
SDLash_EventFilter

=============
*/
static void SDLash_EventFilter( SDL_Event *event )
{
	using namespace Platform;

	static const std::unordered_map<SDL_Keycode, Input::Keyboard::Key> KeyMap = {
		{ SDLK_BACKSPACE, Input::Keyboard::Key::Backspace },
		{ SDLK_TAB, Input::Keyboard::Key::Tab },
		{ SDLK_RETURN, Input::Keyboard::Key::Enter },
		{ SDLK_LSHIFT, Input::Keyboard::Key::LeftShift },
		{ SDLK_RSHIFT, Input::Keyboard::Key::RightShift },
		{ SDLK_LCTRL, Input::Keyboard::Key::LeftCtrl },
		{ SDLK_RCTRL, Input::Keyboard::Key::RightCtrl },
		{ SDLK_LALT, Input::Keyboard::Key::LeftAlt },
		{ SDLK_RALT, Input::Keyboard::Key::RightAlt },
		{ SDLK_PAUSE, Input::Keyboard::Key::Pause },
		{ SDLK_CAPSLOCK, Input::Keyboard::Key::CapsLock },
		{ SDLK_ESCAPE, Input::Keyboard::Key::Escape },
		{ SDLK_SPACE, Input::Keyboard::Key::Space },
		{ SDLK_PAGEUP, Input::Keyboard::Key::PageUp },
		{ SDLK_PAGEDOWN, Input::Keyboard::Key::PageDown },
		{ SDLK_END, Input::Keyboard::Key::End },
		{ SDLK_HOME, Input::Keyboard::Key::Home },
		{ SDLK_LEFT, Input::Keyboard::Key::Left },
		{ SDLK_UP, Input::Keyboard::Key::Up },
		{ SDLK_RIGHT, Input::Keyboard::Key::Right },
		{ SDLK_DOWN, Input::Keyboard::Key::Down },
		{ SDLK_PRINTSCREEN, Input::Keyboard::Key::PrintScreen },
		{ SDLK_INSERT, Input::Keyboard::Key::Insert },
		{ SDLK_DELETE, Input::Keyboard::Key::Delete },
				
		{ SDLK_a, Input::Keyboard::Key::A },
		{ SDLK_b, Input::Keyboard::Key::B },
		{ SDLK_c, Input::Keyboard::Key::C },
		{ SDLK_d, Input::Keyboard::Key::D },
		{ SDLK_e, Input::Keyboard::Key::E },
		{ SDLK_f, Input::Keyboard::Key::F },
		{ SDLK_g, Input::Keyboard::Key::G },
		{ SDLK_h, Input::Keyboard::Key::H },
		{ SDLK_i, Input::Keyboard::Key::I },
		{ SDLK_j, Input::Keyboard::Key::J },
		{ SDLK_k, Input::Keyboard::Key::K },
		{ SDLK_l, Input::Keyboard::Key::L },
		{ SDLK_m, Input::Keyboard::Key::M },
		{ SDLK_n, Input::Keyboard::Key::N },
		{ SDLK_o, Input::Keyboard::Key::O },
		{ SDLK_p, Input::Keyboard::Key::P },
		{ SDLK_q, Input::Keyboard::Key::Q },
		{ SDLK_r, Input::Keyboard::Key::R },
		{ SDLK_s, Input::Keyboard::Key::S },
		{ SDLK_t, Input::Keyboard::Key::T },
		{ SDLK_u, Input::Keyboard::Key::U },
		{ SDLK_v, Input::Keyboard::Key::V },
		{ SDLK_w, Input::Keyboard::Key::W },
		{ SDLK_x, Input::Keyboard::Key::X },
		{ SDLK_y, Input::Keyboard::Key::Y },
		{ SDLK_z, Input::Keyboard::Key::Z },

		{ SDLK_KP_0, Input::Keyboard::Key::NumPad0 },
		{ SDLK_KP_1, Input::Keyboard::Key::NumPad1 },
		{ SDLK_KP_2, Input::Keyboard::Key::NumPad2 },
		{ SDLK_KP_3, Input::Keyboard::Key::NumPad3 },
		{ SDLK_KP_4, Input::Keyboard::Key::NumPad4 },
		{ SDLK_KP_5, Input::Keyboard::Key::NumPad5 },
		{ SDLK_KP_6, Input::Keyboard::Key::NumPad6 },
		{ SDLK_KP_7, Input::Keyboard::Key::NumPad7 },
		{ SDLK_KP_8, Input::Keyboard::Key::NumPad8 },
		{ SDLK_KP_9, Input::Keyboard::Key::NumPad9 },

		{ SDLK_KP_MULTIPLY, Input::Keyboard::Key::Multiply },
		{ SDLK_KP_PLUS, Input::Keyboard::Key::Add },
		{ SDLK_KP_MINUS, Input::Keyboard::Key::Subtract },
		{ SDLK_KP_DECIMAL, Input::Keyboard::Key::Decimal },
		{ SDLK_KP_DIVIDE, Input::Keyboard::Key::Divide },

		{ SDLK_F1, Input::Keyboard::Key::F1 },
		{ SDLK_F2, Input::Keyboard::Key::F2 },
		{ SDLK_F3, Input::Keyboard::Key::F3 },
		{ SDLK_F4, Input::Keyboard::Key::F4 },
		{ SDLK_F5, Input::Keyboard::Key::F5 },
		{ SDLK_F6, Input::Keyboard::Key::F6 },
		{ SDLK_F7, Input::Keyboard::Key::F7 },
		{ SDLK_F8, Input::Keyboard::Key::F8 },
		{ SDLK_F9, Input::Keyboard::Key::F9 },
		{ SDLK_F10, Input::Keyboard::Key::F10 },
		{ SDLK_F11, Input::Keyboard::Key::F11 },
		{ SDLK_F12, Input::Keyboard::Key::F12 },

		{ SDLK_BACKQUOTE, Input::Keyboard::Key::Tilde },
	};

	static const std::unordered_map<int, Input::Keyboard::Event::Type> TypeMap = {
		{ SDL_KEYDOWN, Input::Keyboard::Event::Type::Pressed },
		{ SDL_KEYUP, Input::Keyboard::Event::Type::Released }
	};

	switch ( event->type )
	{
	/* Mouse events */
	case SDL_MOUSEMOTION:
		if( host.mouse_visible )
			SDL_GetRelativeMouseState( NULL, NULL );

		sky::Emit(Platform::Input::Mouse::MoveEvent{
			.pos = {
				(int)((float)event->motion.x * gScale),
				(int)((float)event->motion.y * gScale)
			}
		});

		break;

	case SDL_MOUSEBUTTONUP:
	case SDL_MOUSEBUTTONDOWN:
		Sky_ButtonEvent(*event);
		SDLash_MouseEvent( event->button );
		break;

	/* Keyboard events */
	case SDL_KEYDOWN:
	case SDL_KEYUP:
		SDLash_KeyEvent( event->key );

		if (KeyMap.contains(event->key.keysym.sym))
		{
			sky::Emit(Input::Keyboard::Event{
				.type = TypeMap.at(event->type),
				.key = KeyMap.at(event->key.keysym.sym)
			});
		}
		break;

	/* Joystick events */
	case SDL_JOYAXISMOTION:
		if ( !SDLash_IsInstanceIDAGameController( event->jaxis.which ))
			Joy_AxisMotionEvent( event->jaxis.axis, event->jaxis.value );
		break;

	case SDL_JOYBALLMOTION:
		if ( !SDLash_IsInstanceIDAGameController( event->jball.which ))
			Joy_BallMotionEvent( event->jball.ball, event->jball.xrel, event->jball.yrel );
		break;

	case SDL_JOYHATMOTION:
		if ( !SDLash_IsInstanceIDAGameController( event->jhat.which ))
			Joy_HatMotionEvent( event->jhat.hat, event->jhat.value );
		break;

	case SDL_JOYBUTTONDOWN:
	case SDL_JOYBUTTONUP:
		if ( !SDLash_IsInstanceIDAGameController( event->jbutton.which ))
			Joy_ButtonEvent( event->jbutton.button, event->jbutton.state );
		break;

	case SDL_QUIT:
		Sys_Quit();
		break;
#if SDL_VERSION_ATLEAST( 2, 0, 0 )
	case SDL_MOUSEWHEEL: {
		IN_MWheelEvent( event->wheel.y );

		int x = 0;
		int y = 0;

		SDL_GetMouseState(&x, &y);

		sky::Emit(Input::Mouse::ScrollEvent{
			.pos = {
				(int)((float)x * gScale),
				(int)((float)y * gScale)
			},
			.scroll = {
				event->wheel.preciseX,
				event->wheel.preciseY
			}
		});

		break;
	}
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

		auto width = PLATFORM->getWidth();
		auto height = PLATFORM->getHeight();

		auto sky_pos = glm::ivec2{
			(int)(width * event->tfinger.x),// * gScale),
			(int)(height * event->tfinger.y)// * gScale)
		};

		if (type == event_down)
		{
			sky::Emit(Platform::Input::Touch::Event{
				.type = Platform::Input::Touch::Event::Type::Begin,
				.pos = sky_pos
			});
		}
		else if (type == event_up)
		{
			sky::Emit(Platform::Input::Touch::Event{
				.type = Platform::Input::Touch::Event::Type::End,
				.pos = sky_pos
			});
		}
		else if (type == event_motion)
		{
			sky::Emit(Platform::Input::Touch::Event{
				.type = Platform::Input::Touch::Event::Type::Continue,
				.pos = sky_pos
			});
		}

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
		sky::Emit(Platform::Input::Keyboard::CharEvent{
			.codepoint = *(char32_t*)&event->text.text
		});
		break;

	case SDL_JOYDEVICEADDED:
		Joy_AddEvent();
		break;
	case SDL_JOYDEVICEREMOVED:
		Joy_RemoveEvent();
		break;

	/* GameController API */
	case SDL_CONTROLLERAXISMOTION:
	{
		if( !Joy_IsActive( ))
			break;

		if( event->caxis.axis >= 0 && event->caxis.axis < ARRAYSIZE( SDLash_GameControllerAxisMapping ))
		{
			Joy_KnownAxisMotionEvent( (engineAxis_t)SDLash_GameControllerAxisMapping[event->caxis.axis], event->caxis.value );
		}
		break;
	}

	case SDL_CONTROLLERBUTTONDOWN:
	case SDL_CONTROLLERBUTTONUP:
	{
		if( !Joy_IsActive( ))
			break;

		// TODO: Use joyinput funcs, for future multiple gamepads support
		if( event->cbutton.button >= 0 && event->cbutton.button < ARRAYSIZE( SDLash_GameControllerButtonMapping ))
		{
			Key_Event( SDLash_GameControllerButtonMapping[event->cbutton.button], event->cbutton.state );
		}
		break;
	}

	case SDL_CONTROLLERDEVICEADDED:
		SDLash_GameController_Add( event->cdevice.which );
		break;

	case SDL_CONTROLLERDEVICEREMOVED:
		SDLash_GameController_Remove( event->cdevice.which );
		break;

	case SDL_WINDOWEVENT:
		if( event->window.windowID != SDL_GetWindowID( (SDL_Window*)host.hWnd ) )
			return;

		if( host.status == HOST_SHUTDOWN || Host_IsDedicated() )
			break; // no need to activate

		switch( event->window.event )
		{
		case SDL_WINDOWEVENT_MOVED:
			if( !vid_fullscreen.value )
			{
				char val[32];

				Q_snprintf( val, sizeof( val ), "%d", event->window.data1 );
				Cvar_DirectSet( &window_xpos, val );

				Q_snprintf( val, sizeof( val ), "%d", event->window.data2 );
				Cvar_DirectSet( &window_ypos, val );
			}
			break;
		case SDL_WINDOWEVENT_MINIMIZED:
			host.status = HOST_SLEEP;
			VID_RestoreScreenResolution( );
			break;
		case SDL_WINDOWEVENT_RESTORED:
			host.status = HOST_FRAME;
			host.force_draw_version_time = host.realtime + FORCE_DRAW_VERSION_TIME;
			if( vid_fullscreen.value )
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
			if( vid_fullscreen.value )
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

#if XASH_PSVITA
	PSVita_InputUpdate();
#endif
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
	if( m_ignore.value )
	{
		SDL_GetRelativeMouseState( NULL, NULL );
		SDL_ShowCursor( SDL_TRUE );
	}
}

#endif //  defined( XASH_SDL ) && !XASH_DEDICATED
