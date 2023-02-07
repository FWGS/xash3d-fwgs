/*
vid_sdl.c - SDL input component
Copyright (C) 2018 a1batross

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#if !XASH_DEDICATED
#include <SDL.h>

#include "common.h"
#include "keydefs.h"
#include "input.h"
#include "client.h"
#include "vgui_draw.h"
#include "events.h"
#include "sound.h"
#include "vid_common.h"

SDL_Joystick *g_joy = NULL;
#if !SDL_VERSION_ATLEAST( 2, 0, 0 )
#define SDL_WarpMouseInWindow( win, x, y ) SDL_WarpMouse( ( x ), ( y ) )
#else
static struct
{
	qboolean initialized;
	SDL_Cursor *cursors[dc_last];
} cursors;
#endif

/*
=============
Platform_GetMousePos

=============
*/
void GAME_EXPORT Platform_GetMousePos( int *x, int *y )
{
	SDL_GetMouseState( x, y );
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
#if SDL_VERSION_ATLEAST( 2, 0, 0 )
	int textLength;
	char *sdlbuffer = SDL_GetClipboardText();

	if( !sdlbuffer )
		return 0;

	if (buffer && size > 0)
	{
		textLength = Q_strncpy( buffer, sdlbuffer, size );
	}
	else {
		textLength = Q_strlen( sdlbuffer );
	}
	SDL_free( sdlbuffer );
	return textLength;
#else // SDL_VERSION_ATLEAST( 2, 0, 0 )
	buffer[0] = 0;
#endif // SDL_VERSION_ATLEAST( 2, 0, 0 )
	return 0;
}

/*
=============
Platform_SetClipobardText

=============
*/
void Platform_SetClipboardText( const char *buffer )
{
#if SDL_VERSION_ATLEAST( 2, 0, 0 )
	SDL_SetClipboardText( buffer );
#endif // SDL_VERSION_ATLEAST( 2, 0, 0 )
}

/*
=============
Platform_Vibrate

=============
*/
void Platform_Vibrate( float time, char flags )
{
	if( g_joy )
		SDL_JoystickRumble( g_joy, 0xFFFF, 0xFFFF, time * 1000.0f );
}

/*
=============
SDLash_EnableTextInput

=============
*/
void Platform_EnableTextInput( qboolean enable )
{
#if SDL_VERSION_ATLEAST( 2, 0, 0 )
	enable ? SDL_StartTextInput() : SDL_StopTextInput();
#endif // SDL_VERSION_ATLEAST( 2, 0, 0 )
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

	Con_Reportf( "Joystick: SDL\n" );

	if( SDL_WasInit( SDL_INIT_JOYSTICK ) != SDL_INIT_JOYSTICK &&
		SDL_InitSubSystem( SDL_INIT_JOYSTICK ) )
	{
		Con_Reportf( "Failed to initialize SDL Joysitck: %s\n", SDL_GetError() );
		return 0;
	}

	if( g_joy )
	{
		SDL_JoystickClose( g_joy );
	}

	num = SDL_NumJoysticks();

	if( num > 0 )
		Con_Reportf( "%i joysticks found:\n", num );
	else
	{
		Con_Reportf( "No joystick found.\n" );
		return 0;
	}

#if SDL_VERSION_ATLEAST( 2, 0, 0 )
	for( i = 0; i < num; i++ )
		Con_Reportf( "%i\t: %s\n", i, SDL_JoystickNameForIndex( i ) );
#endif // SDL_VERSION_ATLEAST( 2, 0, 0 )

	Con_Reportf( "Pass +set joy_index N to command line, where N is number, to select active joystick\n" );

	g_joy = SDL_JoystickOpen( numjoy );

	if( !g_joy )
	{
		Con_Reportf( "Failed to select joystick: %s\n", SDL_GetError( ) );
		return 0;
	}

#if SDL_VERSION_ATLEAST( 2, 0, 0 )
	Con_Reportf( "Selected joystick: %s\n"
		"\tAxes: %i\n"
		"\tHats: %i\n"
		"\tButtons: %i\n"
		"\tBalls: %i\n",
		SDL_JoystickName( g_joy ), SDL_JoystickNumAxes( g_joy ), SDL_JoystickNumHats( g_joy ),
		SDL_JoystickNumButtons( g_joy ), SDL_JoystickNumBalls( g_joy ) );

	SDL_GameControllerEventState( SDL_DISABLE );
#endif // SDL_VERSION_ATLEAST( 2, 0, 0 )
	SDL_JoystickEventState( SDL_ENABLE );

	return num;
}

#if SDL_VERSION_ATLEAST( 2, 0, 0 )
/*
=============
SDLash_JoyInit_New

=============
*/
static int SDLash_JoyInit_New( int numjoy )
{
	int count, numJoysticks, i;

	Con_Reportf( "Joystick: SDL GameController API\n" );
	if( SDL_WasInit( SDL_INIT_GAMECONTROLLER ) != SDL_INIT_GAMECONTROLLER &&
		SDL_InitSubSystem( SDL_INIT_GAMECONTROLLER ) )
	{
		Con_Reportf( "Failed to initialize SDL GameController API: %s\n", SDL_GetError() );
		return 0;
	}

	SDL_GameControllerAddMappingsFromFile( "controllermappings.txt" );

	count = 0;
	numJoysticks = SDL_NumJoysticks();
	for ( i = 0; i < numJoysticks; i++ )
		if( SDL_IsGameController( i ) )
			++count;

	return count;
}
#endif // SDL_VERSION_ATLEAST( 2, 0, 0 )

/*
=============
Platform_JoyInit

=============
*/
int Platform_JoyInit( int numjoy )
{
#if SDL_VERSION_ATLEAST( 2, 0, 0 )
	// SDL_Joystick is now an old API
	// SDL_GameController is preferred
	if( !Sys_CheckParm( "-sdl_joy_old_api" ) )
		return SDLash_JoyInit_New(numjoy);
#endif // SDL_VERSION_ATLEAST( 2, 0, 0 )
	return SDLash_JoyInit_Old(numjoy);
}

/*
========================
SDLash_InitCursors

========================
*/
void SDLash_InitCursors( void )
{
#if SDL_VERSION_ATLEAST( 2, 0, 0 )
	if( cursors.initialized )
		SDLash_FreeCursors();

	// load up all default cursors
	cursors.cursors[dc_none] = NULL;
	cursors.cursors[dc_arrow] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
	cursors.cursors[dc_ibeam] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_IBEAM);
	cursors.cursors[dc_hourglass] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_WAIT);
	cursors.cursors[dc_crosshair] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
	cursors.cursors[dc_up] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
	cursors.cursors[dc_sizenwse] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENWSE);
	cursors.cursors[dc_sizenesw] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENESW);
	cursors.cursors[dc_sizewe] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
	cursors.cursors[dc_sizens] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENS);
	cursors.cursors[dc_sizeall] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEALL);
	cursors.cursors[dc_no] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NO);
	cursors.cursors[dc_hand] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
	cursors.initialized = true;
#endif
}

/*
========================
SDLash_FreeCursors

========================
*/
void SDLash_FreeCursors( void )
{
#if SDL_VERSION_ATLEAST( 2, 0, 0 )
	int i = 0;

	for( ; i < ARRAYSIZE( cursors.cursors ); i++ )
	{
		if( cursors.cursors[i] )
			SDL_FreeCursor( cursors.cursors[i] );
		cursors.cursors[i] = NULL;
	}

	cursors.initialized = false;
#endif // SDL_VERSION_ATLEAST( 2, 0, 0 )
}

/*
========================
Platform_SetCursorType

========================
*/
void Platform_SetCursorType( VGUI_DefaultCursor type )
{
	qboolean visible;

#if SDL_VERSION_ATLEAST( 2, 0, 0 )
	if( !cursors.initialized )
		return;
#endif

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
	if( !visible && touch_emulate.value )
		return;

	host.mouse_visible = visible;
	VGui_UpdateInternalCursorState( type );

#if SDL_VERSION_ATLEAST( 2, 0, 0 )
	if( host.mouse_visible )
	{
		SDL_SetCursor( cursors.cursors[type] );
		SDL_ShowCursor( true );
	}
	else
	{
		SDL_ShowCursor( false );
	}
#else
	if( host.mouse_visible )
	{
		SDL_ShowCursor( true );
	}
	else
	{
		SDL_ShowCursor( false );
	}
#endif
}

/*
========================
Platform_GetKeyModifiers

========================
*/
key_modifier_t Platform_GetKeyModifiers( void )
{
#if SDL_VERSION_ATLEAST( 2, 0, 0 )
	SDL_Keymod modFlags;
	key_modifier_t resultFlags;

	resultFlags = KeyModifier_None;
	modFlags = SDL_GetModState();
	if( FBitSet( modFlags, KMOD_LCTRL ))
		SetBits( resultFlags, KeyModifier_LeftCtrl );
	if( FBitSet( modFlags, KMOD_RCTRL ))
		SetBits( resultFlags, KeyModifier_RightCtrl );
	if( FBitSet( modFlags, KMOD_RSHIFT ))
		SetBits( resultFlags, KeyModifier_RightShift );
	if( FBitSet( modFlags, KMOD_LSHIFT ))
		SetBits( resultFlags, KeyModifier_LeftShift );
	if( FBitSet( modFlags, KMOD_LALT ))
		SetBits( resultFlags, KeyModifier_LeftAlt );
	if( FBitSet( modFlags, KMOD_RALT ))
		SetBits( resultFlags, KeyModifier_RightAlt );
	if( FBitSet( modFlags, KMOD_NUM ))
		SetBits( resultFlags, KeyModifier_NumLock );
	if( FBitSet( modFlags, KMOD_CAPS ))
		SetBits( resultFlags, KeyModifier_CapsLock );
	if( FBitSet( modFlags, KMOD_RGUI ))
		SetBits( resultFlags, KeyModifier_RightSuper );
	if( FBitSet( modFlags, KMOD_LGUI ))
		SetBits( resultFlags, KeyModifier_LeftSuper );

	return resultFlags;
#else
	return KeyModifier_None;
#endif
}

#endif // XASH_DEDICATED
